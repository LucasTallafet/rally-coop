#include "platform/near_grid.h"

#include <SDL3/SDL_video.h>
#include <raymath.h>
#include <rlgl.h>

#include <chrono>
#include <cstring>

namespace rally::platform {

namespace {

// Shader del suelo: color por vértice y recorte por la distancia horizontal al ojo.
constexpr const char* kVertexShader = R"(#version 330
in vec3 vertexPosition;
in vec4 vertexColor;
uniform mat4 mvp;
uniform mat4 matModel;
out vec4 fragColor;
out vec2 fragXZ;
void main() {
    fragColor = vertexColor;
    fragXZ = (matModel * vec4(vertexPosition, 1.0)).xz;
    gl_Position = mvp * vec4(vertexPosition, 1.0);
}
)";

constexpr const char* kFragmentShader = R"(#version 330
in vec4 fragColor;
in vec2 fragXZ;
uniform float radius;
uniform float side;
uniform vec2 eye;
out vec4 finalColor;
void main() {
    if ((length(fragXZ - eye) - radius) * side < 0.0) discard;
    finalColor = fragColor;
}
)";

// Rejilla cercana: altura de la textura (geometría + h apagado hacia el borde) y sombreado
// por la normal de los vecinos. La textura está en anillo: la celda del vértice (i, j) es
// ((anchorSlot + (i, j)) mod cells). Una celda con alfa < 1 es un hueco: el fragmento
// (valor interpolado) se descarta.
// Debe coincidir con el tamaño de `surfaceColors` del shader de vértices.
constexpr int kMaxColors = 8;

constexpr const char* kNearVertexShader = R"(#version 330
in vec3 vertexPosition;
uniform mat4 mvp;
uniform sampler2D texture0;
uniform ivec2 anchorSlot;
uniform int cells;
uniform float spacing;
uniform vec2 eyeGrid;
uniform float fadeStart;
uniform float radius;
uniform vec3 surfaceColors[8];
uniform vec3 light;
out vec4 fragColor;
out vec2 fragXZ;
out float fragValid;
float fadeAt(vec2 p) {
    float t = clamp((radius - length(p - eyeGrid)) / (radius - fadeStart), 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}
vec4 texel(int i, int j) {
    i = clamp(i, 0, cells - 1);
    j = clamp(j, 0, cells - 1);
    return texelFetch(texture0, ivec2((anchorSlot.x + i) % cells, (anchorSlot.y + j) % cells), 0);
}
float heightAt(int i, int j) {
    vec4 t = texel(i, j);
    return t.r + t.g * fadeAt(vec2(float(i), float(j)) * spacing);
}
void main() {
    int i = int(round(vertexPosition.x / spacing));
    int j = int(round(vertexPosition.z / spacing));
    vec4 t = texel(i, j);
    float h = t.r + t.g * fadeAt(vertexPosition.xz);
    vec3 n = normalize(vec3(heightAt(i - 1, j) - heightAt(i + 1, j), 2.0 * spacing,
                            heightAt(i, j - 1) - heightAt(i, j + 1)));
    float shade = 0.5 + 0.5 * max(0.0, dot(n, light));
    fragColor = vec4(surfaceColors[int(t.b + 0.5)] * shade, 1.0);
    fragXZ = vertexPosition.xz - eyeGrid;
    fragValid = t.a;
    gl_Position = mvp * vec4(vertexPosition.x, h, vertexPosition.z, 1.0);
}
)";

constexpr const char* kNearFragmentShader = R"(#version 330
in vec4 fragColor;
in vec2 fragXZ;
in float fragValid;
uniform float radius;
out vec4 finalColor;
void main() {
    if (fragValid < 0.999 || length(fragXZ) > radius) discard;
    finalColor = fragColor;
}
)";

// Luz fija, la misma que las cajas de la demo.
const Vector3 kLight = Vector3Normalize(Vector3{0.35f, 1.0f, 0.55f});

} // namespace

PolygonOffset::PolygonOffset() {
    offset_ = reinterpret_cast<Offset>(SDL_GL_GetProcAddress("glPolygonOffset"));
    enable_ = reinterpret_cast<Capability>(SDL_GL_GetProcAddress("glEnable"));
    disable_ = reinterpret_cast<Capability>(SDL_GL_GetProcAddress("glDisable"));
}

void PolygonOffset::set(float factor, float units) const {
    if (available()) {
        enable_(0x8037); // GL_POLYGON_OFFSET_FILL
        offset_(factor, units);
    }
}

void PolygonOffset::off() const {
    if (disable_ != nullptr) {
        disable_(0x8037);
    }
}

ClipShader::ClipShader() {
    const Shader shader = LoadShaderFromMemory(kVertexShader, kFragmentShader);
    loc_radius_ = GetShaderLocation(shader, "radius");
    loc_side_ = GetShaderLocation(shader, "side");
    loc_eye_ = GetShaderLocation(shader, "eye");
    material_ = LoadMaterialDefault();
    material_.shader = shader;
}

ClipShader::~ClipShader() {
    UnloadMaterial(material_); // descarga también el shader
}

void ClipShader::draw(const Mesh& mesh, const Vec3d& origin, float radius, float side,
                      const Vec3d& eye) {
    const Vec3f eye_rel = relative_to_camera(eye, origin);
    const float eye_xz[2] = {eye_rel.x, eye_rel.z};
    SetShaderValue(material_.shader, loc_radius_, &radius, SHADER_UNIFORM_FLOAT);
    SetShaderValue(material_.shader, loc_side_, &side, SHADER_UNIFORM_FLOAT);
    SetShaderValue(material_.shader, loc_eye_, eye_xz, SHADER_UNIFORM_VEC2);
    const Vec3f shift = relative_to_camera(Vec3d{}, origin);
    DrawMesh(mesh, material_, MatrixTranslate(shift.x, shift.y, shift.z));
}

NearGrid::NearGrid(double radius, double fade_start, double spacing,
                   const std::array<Color, sim::kSurfaceCount>& colors, const Color* extra_colors,
                   int extra_count)
    : ring_(radius, spacing), spacing_(spacing) {
    // Malla fija (vértice (i, j) en (i, 0, j) × spacing) y textura.
    const int n = ring_.cells();
    near_.vertexCount = n * n;
    near_.triangleCount = 2 * (n - 1) * (n - 1);
    near_.vertices = static_cast<float*>(MemAlloc(static_cast<unsigned int>(n * n * 12)));
    near_.indices = static_cast<unsigned short*>(
        MemAlloc(static_cast<unsigned int>(near_.triangleCount * 3 * 2)));
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            float* v = near_.vertices + 3 * (j * n + i);
            v[0] = static_cast<float>(i * spacing);
            v[1] = 0.0f;
            v[2] = static_cast<float>(j * spacing);
        }
    }
    int k = 0;
    for (int j = 0; j < n - 1; ++j) {
        for (int i = 0; i < n - 1; ++i) {
            const auto a = static_cast<unsigned short>(j * n + i);
            const auto b = static_cast<unsigned short>(j * n + i + 1);
            const auto c = static_cast<unsigned short>((j + 1) * n + i + 1);
            const auto d = static_cast<unsigned short>((j + 1) * n + i);
            // Hacia arriba (contrario a las agujas del reloj visto desde +y): a, d, c y a, c, b.
            near_.indices[k++] = a;
            near_.indices[k++] = d;
            near_.indices[k++] = c;
            near_.indices[k++] = a;
            near_.indices[k++] = c;
            near_.indices[k++] = b;
        }
    }
    UploadMesh(&near_, false);

    texture_ = rlLoadTexture(ring_.texels(), n, n, RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32, 1);
    rlTextureParameters(texture_, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_NEAREST);
    rlTextureParameters(texture_, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_NEAREST);

    shader_ = LoadShaderFromMemory(kNearVertexShader, kNearFragmentShader);
    loc_anchor_ = GetShaderLocation(shader_, "anchorSlot");
    loc_eye_ = GetShaderLocation(shader_, "eyeGrid");
    const int cells = n;
    const auto spacing_f = static_cast<float>(spacing);
    const auto fade_start_f = static_cast<float>(fade_start);
    const auto radius_f = static_cast<float>(radius);
    SetShaderValue(shader_, GetShaderLocation(shader_, "cells"), &cells, SHADER_UNIFORM_INT);
    SetShaderValue(shader_, GetShaderLocation(shader_, "spacing"), &spacing_f,
                   SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader_, GetShaderLocation(shader_, "fadeStart"), &fade_start_f,
                   SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader_, GetShaderLocation(shader_, "radius"), &radius_f, SHADER_UNIFORM_FLOAT);
    const float light[3] = {kLight.x, kLight.y, kLight.z};
    SetShaderValue(shader_, GetShaderLocation(shader_, "light"), light, SHADER_UNIFORM_VEC3);
    // Los de las superficies (índices 0..4) y, detrás, los extra (índices >= 5; ZoneDraw no los
    // usa).
    const int color_count = sim::kSurfaceCount + extra_count;
    float rgb[3 * kMaxColors];
    for (int s = 0; s < color_count && s < kMaxColors; ++s) {
        const Color c = s < sim::kSurfaceCount ? colors[static_cast<std::size_t>(s)]
                                               : extra_colors[s - sim::kSurfaceCount];
        rgb[3 * s + 0] = c.r / 255.0f;
        rgb[3 * s + 1] = c.g / 255.0f;
        rgb[3 * s + 2] = c.b / 255.0f;
    }
    SetShaderValueV(shader_, GetShaderLocation(shader_, "surfaceColors"), rgb, SHADER_UNIFORM_VEC3,
                    color_count < kMaxColors ? color_count : kMaxColors);
    material_ = LoadMaterialDefault();
    material_.shader = shader_;
    material_.maps[MATERIAL_MAP_DIFFUSE].texture =
        Texture2D{texture_, n, n, 1, PIXELFORMAT_UNCOMPRESSED_R32G32B32A32};
}

NearGrid::~NearGrid() {
    UnloadMesh(near_);
    // La textura se descarga aparte: el material no la descarga.
    material_.maps[MATERIAL_MAP_DIFFUSE].texture = Texture2D{};
    UnloadMaterial(material_);
    rlUnloadTexture(texture_);
}

void NearGrid::upload(NearStats& stats) {
    using Clock = std::chrono::steady_clock;
    const int n = ring_.cells();
    const int format = RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32;
    for (const NearRing::Upload& u : ring_.pending()) {
        const Clock::time_point t0 = Clock::now();
        switch (u.kind) {
        case NearRing::Upload::Kind::Full:
            rlUpdateTexture(texture_, 0, 0, n, n, format, ring_.texels());
            break;
        case NearRing::Upload::Kind::Row:
            rlUpdateTexture(texture_, 0, u.slot, n, 1, format,
                            ring_.texels() + static_cast<std::size_t>(4) * u.slot * n);
            break;
        case NearRing::Upload::Kind::Column:
            rlUpdateTexture(texture_, u.slot, 0, 1, n, format, ring_.column(u.slot));
            break;
        }
        stats.upload_ms += std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        ++stats.uploads;
    }
}

void NearGrid::draw_mesh(const Vec3d& eye, const Vec3d& origin) {
    const int anchor_slot[2] = {ring_.slot(ring_.anchor_x()), ring_.slot(ring_.anchor_z())};
    const Vec3d corner{static_cast<double>(ring_.anchor_x()) * spacing_, 0.0,
                       static_cast<double>(ring_.anchor_z()) * spacing_};
    const float eye_grid[2] = {static_cast<float>(eye.x - corner.x),
                               static_cast<float>(eye.z - corner.z)};
    SetShaderValue(shader_, loc_anchor_, anchor_slot, SHADER_UNIFORM_IVEC2);
    SetShaderValue(shader_, loc_eye_, eye_grid, SHADER_UNIFORM_VEC2);
    const Vec3f shift = relative_to_camera(corner, origin);
    DrawMesh(near_, material_, MatrixTranslate(shift.x, shift.y, shift.z));
}

} // namespace rally::platform
