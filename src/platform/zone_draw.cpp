#include "platform/zone_draw.h"

#include <SDL3/SDL_video.h>
#include <raymath.h>
#include <rlgl.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>

namespace rally::platform {

namespace {

using sim::AreaId;
using sim::TestZone;
using sim::ZoneArea;
namespace zone = sim::zone;

constexpr double kPi = 3.14159265358979323846;

// Desplazamiento de polígonos de OpenGL, que rlgl no expone: se pide a SDL. Sin él (no
// debería pasar con OpenGL 3.3) las áreas se dibujan igual, con riesgo de parpadeo lejos.
constexpr unsigned int kGlPolygonOffsetFill = 0x8037;
using GlPolygonOffset = void(__stdcall*)(float, float);
using GlCapability = void(__stdcall*)(unsigned int);
GlPolygonOffset gl_polygon_offset = nullptr;
GlCapability gl_enable = nullptr;
GlCapability gl_disable = nullptr;

// Shader del suelo: color por vértice y recorte por la distancia horizontal al ojo. side = 1
// descarta lo que está a menos de `radius` (lejos); side = -1, lo que está a más (cerca).
// Con radius = 0 y side = 1 no descarta nada.
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
// ((anchorSlot + (i, j)) mod cells).
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
uniform vec3 surfaceColors[5];
uniform vec3 light;
out vec4 fragColor;
out vec2 fragXZ;
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
    gl_Position = mvp * vec4(vertexPosition.x, h, vertexPosition.z, 1.0);
}
)";

constexpr const char* kNearFragmentShader = R"(#version 330
in vec4 fragColor;
in vec2 fragXZ;
uniform float radius;
out vec4 finalColor;
void main() {
    if (length(fragXZ) > radius) discard;
    finalColor = fragColor;
}
)";

// Luz fija, la misma que las cajas de la demo.
const Vector3 kLight = Vector3Normalize(Vector3{0.35f, 1.0f, 0.55f});

constexpr Color kLineWhite{236, 236, 228, 255};
constexpr Color kLineYellow{232, 196, 64, 255};
constexpr Color kCone{240, 112, 32, 255};
constexpr Color kPost{236, 232, 220, 255};
constexpr Color kWall{78, 80, 86, 255};
constexpr double kLineLift = 0.03; // las líneas pintadas, por encima del microrrelieve
constexpr float kLineWidth = 0.15f;

Color shaded(Color c, float nx, float ny, float nz) {
    const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
    const float dot = (nx * kLight.x + ny * kLight.y + nz * kLight.z) / length;
    const float shade = 0.5f + 0.5f * std::max(0.0f, dot);
    return Color{static_cast<unsigned char>(c.r * shade), static_cast<unsigned char>(c.g * shade),
                 static_cast<unsigned char>(c.b * shade), c.a};
}

// Triángulos sueltos (sin índices), con el sombreado plano de cada cara en el color de sus
// vértices. Cada triángulo se orienta hacia arriba.
class TriangleList {
public:
    void triangle(Vector3 a, Vector3 b, Vector3 c, Color color) {
        Vector3 n = Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a));
        if (n.y < 0.0f) {
            std::swap(b, c);
            n = Vector3Negate(n);
        }
        const Color face = shaded(color, n.x, n.y, n.z);
        for (const Vector3& v : {a, b, c}) {
            positions_.push_back(v.x);
            positions_.push_back(v.y);
            positions_.push_back(v.z);
            colors_.push_back(face.r);
            colors_.push_back(face.g);
            colors_.push_back(face.b);
            colors_.push_back(face.a);
        }
    }
    void quad(Vector3 a, Vector3 b, Vector3 c, Vector3 d, Color color) {
        triangle(a, b, c, color);
        triangle(a, c, d, color);
    }

    // Sube la lista a la GPU en mallas de como mucho 3 × 2^16 vértices.
    void upload(std::vector<Mesh>& out) const {
        constexpr std::size_t kMaxVertices = 3 * 65536;
        const std::size_t total = positions_.size() / 3;
        for (std::size_t first = 0; first < total; first += kMaxVertices) {
            const std::size_t count = std::min(kMaxVertices, total - first);
            Mesh mesh{};
            mesh.vertexCount = static_cast<int>(count);
            mesh.triangleCount = static_cast<int>(count / 3);
            mesh.vertices = static_cast<float*>(MemAlloc(static_cast<unsigned int>(count * 12)));
            mesh.colors =
                static_cast<unsigned char*>(MemAlloc(static_cast<unsigned int>(count * 4)));
            std::memcpy(mesh.vertices, positions_.data() + first * 3, count * 12);
            std::memcpy(mesh.colors, colors_.data() + first * 4, count * 4);
            UploadMesh(&mesh, false);
            out.push_back(mesh);
        }
    }

private:
    std::vector<float> positions_;
    std::vector<unsigned char> colors_;
};

Vector3 v3(double x, double y, double z) {
    return Vector3{static_cast<float>(x), static_cast<float>(y), static_cast<float>(z)};
}

struct Rect {
    double x0, x1, z0, z1;
};

// r menos el agujero h, en hasta cuatro rectángulos.
void subtract(const Rect& r, const Rect& h, std::vector<Rect>& out) {
    if (h.x1 <= r.x0 || h.x0 >= r.x1 || h.z1 <= r.z0 || h.z0 >= r.z1) {
        out.push_back(r);
        return;
    }
    if (h.z0 > r.z0) {
        out.push_back({r.x0, r.x1, r.z0, h.z0});
    }
    if (h.z1 < r.z1) {
        out.push_back({r.x0, r.x1, h.z1, r.z1});
    }
    const double z0 = std::max(r.z0, h.z0);
    const double z1 = std::min(r.z1, h.z1);
    if (h.x0 > r.x0) {
        out.push_back({r.x0, h.x0, z0, z1});
    }
    if (h.x1 < r.x1) {
        out.push_back({h.x1, r.x1, z0, z1});
    }
}

// Franjas con relieve (baches, rampa y cresta) con sus taludes: se dibujan con la rejilla
// del suelo de Jolt, ajustada hacia fuera a sus muestras.
Rect snapped_footprint(const ZoneArea& a, double margin) {
    const auto down = [](double v) {
        return zone::kGroundOrigin +
               std::floor((v - zone::kGroundOrigin) / zone::kGroundSpacing) * zone::kGroundSpacing;
    };
    const auto up = [](double v) {
        return zone::kGroundOrigin +
               std::ceil((v - zone::kGroundOrigin) / zone::kGroundSpacing) * zone::kGroundSpacing;
    };
    return {down(a.min_x - margin), up(a.max_x + margin), down(a.min_z), up(a.max_z)};
}

bool is_road(AreaId id) { return id == AreaId::Road || id == AreaId::RoadShoulder; }

bool is_relief(AreaId id) {
    return id == AreaId::BumpsAsphalt || id == AreaId::BumpsGravel || id == AreaId::Ramp ||
           id == AreaId::Crest;
}

// Margen de los taludes de cada franja con relieve.
double relief_margin(AreaId id) {
    switch (id) {
    case AreaId::Ramp:
        return 0.15 * zone::kRampRun + 1.0;
    case AreaId::Crest:
        return zone::kCrestHeight + 1.0;
    default:
        return 1.0;
    }
}

} // namespace

Color surface_color(sim::Surface surface) {
    switch (surface) {
    case sim::Surface::Asphalt:
        return Color{96, 99, 106, 255};
    case sim::Surface::Gravel:
        return Color{198, 182, 142, 255};
    case sim::Surface::Dirt:
        return Color{164, 124, 84, 255};
    case sim::Surface::Snow:
        return Color{240, 243, 247, 255};
    case sim::Surface::Sand:
        return Color{236, 222, 160, 255};
    }
    return Color{255, 0, 255, 255};
}

ZoneDraw::ZoneDraw() {
    shader_ = LoadShaderFromMemory(kVertexShader, kFragmentShader);
    loc_radius_ = GetShaderLocation(shader_, "radius");
    loc_side_ = GetShaderLocation(shader_, "side");
    loc_eye_ = GetShaderLocation(shader_, "eye");
    material_ = LoadMaterialDefault();
    material_.shader = shader_;
    gl_polygon_offset = reinterpret_cast<GlPolygonOffset>(SDL_GL_GetProcAddress("glPolygonOffset"));
    gl_enable = reinterpret_cast<GlCapability>(SDL_GL_GetProcAddress("glEnable"));
    gl_disable = reinterpret_cast<GlCapability>(SDL_GL_GetProcAddress("glDisable"));

    // Rejilla cercana: malla fija (vértice (i, j) en (i, 0, j) × kNearSpacing) y textura.
    near_cells_ = static_cast<int>(2.0 * kNearRadius / kNearSpacing) + 1;
    const int n = near_cells_;
    near_.vertexCount = n * n;
    near_.triangleCount = 2 * (n - 1) * (n - 1);
    near_.vertices = static_cast<float*>(MemAlloc(static_cast<unsigned int>(n * n * 12)));
    near_.indices = static_cast<unsigned short*>(
        MemAlloc(static_cast<unsigned int>(near_.triangleCount * 3 * 2)));
    for (int j = 0; j < n; ++j) {
        for (int i = 0; i < n; ++i) {
            float* v = near_.vertices + 3 * (j * n + i);
            v[0] = static_cast<float>(i * kNearSpacing);
            v[1] = 0.0f;
            v[2] = static_cast<float>(j * kNearSpacing);
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

    texels_.assign(static_cast<std::size_t>(n * n * 4), 0.0f);
    column_.assign(static_cast<std::size_t>(n * 4), 0.0f);
    near_texture_ =
        rlLoadTexture(texels_.data(), n, n, RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32, 1);
    rlTextureParameters(near_texture_, RL_TEXTURE_MIN_FILTER, RL_TEXTURE_FILTER_NEAREST);
    rlTextureParameters(near_texture_, RL_TEXTURE_MAG_FILTER, RL_TEXTURE_FILTER_NEAREST);

    near_shader_ = LoadShaderFromMemory(kNearVertexShader, kNearFragmentShader);
    loc_near_anchor_ = GetShaderLocation(near_shader_, "anchorSlot");
    loc_near_eye_ = GetShaderLocation(near_shader_, "eyeGrid");
    const int cells = n;
    const auto spacing = static_cast<float>(kNearSpacing);
    const auto fade_start = static_cast<float>(kNearFadeStart);
    const auto radius = static_cast<float>(kNearRadius);
    SetShaderValue(near_shader_, GetShaderLocation(near_shader_, "cells"), &cells,
                   SHADER_UNIFORM_INT);
    SetShaderValue(near_shader_, GetShaderLocation(near_shader_, "spacing"), &spacing,
                   SHADER_UNIFORM_FLOAT);
    SetShaderValue(near_shader_, GetShaderLocation(near_shader_, "fadeStart"), &fade_start,
                   SHADER_UNIFORM_FLOAT);
    SetShaderValue(near_shader_, GetShaderLocation(near_shader_, "radius"), &radius,
                   SHADER_UNIFORM_FLOAT);
    const float light[3] = {kLight.x, kLight.y, kLight.z};
    SetShaderValue(near_shader_, GetShaderLocation(near_shader_, "light"), light,
                   SHADER_UNIFORM_VEC3);
    float colors[3 * sim::kSurfaceCount];
    for (int s = 0; s < sim::kSurfaceCount; ++s) {
        const Color c = surface_color(static_cast<sim::Surface>(s));
        colors[3 * s + 0] = c.r / 255.0f;
        colors[3 * s + 1] = c.g / 255.0f;
        colors[3 * s + 2] = c.b / 255.0f;
    }
    SetShaderValueV(near_shader_, GetShaderLocation(near_shader_, "surfaceColors"), colors,
                    SHADER_UNIFORM_VEC3, sim::kSurfaceCount);
    near_material_ = LoadMaterialDefault();
    near_material_.shader = near_shader_;
    near_material_.maps[MATERIAL_MAP_DIFFUSE].texture =
        Texture2D{near_texture_, n, n, 1, PIXELFORMAT_UNCOMPRESSED_R32G32B32A32};
    history_.assign(kStatsFrames, 0.0);
}

ZoneDraw::~ZoneDraw() {
    unload_meshes();
    UnloadMesh(near_);
    UnloadMaterial(material_); // descarga también el shader
    // La textura de la rejilla cercana se descarga aparte: el material no la descarga.
    near_material_.maps[MATERIAL_MAP_DIFFUSE].texture = Texture2D{};
    UnloadMaterial(near_material_);
    rlUnloadTexture(near_texture_);
}

void ZoneDraw::unload_meshes() {
    trees_.reset();
    for (std::vector<Mesh>* layer : {&ground_, &decals_, &lines_}) {
        for (Mesh& mesh : *layer) {
            UnloadMesh(mesh);
        }
        layer->clear();
    }
}

void ZoneDraw::build(const sim::TestZone& zone, bool blind) {
    unload_meshes();
    zone_ = &zone;
    blind_ = blind;
    near_valid_ = false; // la superficie de la pista ciega puede haber cambiado
    const auto color_of = [blind](sim::Surface surface) {
        return blind ? kBlindColor : surface_color(surface);
    };
    // Altura de lo que se pinta encima del suelo: la geometría y, fuera del modo a ciegas, h.
    const auto painted_y = [&zone, blind](double x, double z) {
        return zone.ground_height(x, z) + (blind ? 0.0 : zone.micro_height(x, z)) + kLineLift;
    };

    // Suelo: el cuadrado de Jolt menos las franjas con relieve, y esas franjas con su rejilla.
    TriangleList ground;
    // Fondo llano hasta el borde del suelo.
    const double lo = zone::kGroundOrigin;
    const double hi = -zone::kGroundOrigin;
    std::vector<Rect> background{{lo, hi, lo, hi}};
    for (const ZoneArea& a : zone.areas()) {
        if (!is_relief(a.id) || is_road(a.id)) {
            continue;
        }
        const Rect hole = snapped_footprint(a, relief_margin(a.id));
        std::vector<Rect> next;
        for (const Rect& r : background) {
            subtract(r, hole, next);
        }
        background = std::move(next);

        const double s = zone::kGroundSpacing;
        for (double z = hole.z0; z < hole.z1 - 0.5 * s; z += s) {
            for (double x = hole.x0; x < hole.x1 - 0.5 * s; x += s) {
                const Vector3 p00 = v3(x, zone.ground_height(x, z), z);
                const Vector3 p10 = v3(x + s, zone.ground_height(x + s, z), z);
                const Vector3 p11 = v3(x + s, zone.ground_height(x + s, z + s), z + s);
                const Vector3 p01 = v3(x, zone.ground_height(x, z + s), z + s);
                // Superficie de la celda por su centro, como el material de Jolt.
                const Color c = color_of(zone.surface_at(x + 0.5 * s, z + 0.5 * s));
                ground.triangle(p00, p01, p11, c);
                ground.triangle(p00, p11, p10, c);
            }
        }
    }
    {
        // Carretera: su caja, por filas de la rejilla de Jolt. Una celda con alguna esquina a
        // otra altura que 0 o con otra superficie que la tierra va con su relieve; las seguidas
        // que no, en una tira llana.
        const sim::TestRoad& road = zone.road();
        const double s = zone::kGroundSpacing;
        const auto down = [s](double v) {
            return zone::kGroundOrigin + std::floor((v - zone::kGroundOrigin) / s) * s;
        };
        const auto up = [s](double v) {
            return zone::kGroundOrigin + std::ceil((v - zone::kGroundOrigin) / s) * s;
        };
        const Rect box{down(road.min_x()), up(road.max_x()), down(road.min_z()), up(road.max_z())};
        std::vector<Rect> next;
        for (const Rect& r : background) {
            subtract(r, box, next);
        }
        background = std::move(next);
        const int nx = static_cast<int>(std::lround((box.x1 - box.x0) / s));
        const int nz = static_cast<int>(std::lround((box.z1 - box.z0) / s));
        std::vector<float> y(static_cast<std::size_t>(nx + 1) * (nz + 1));
        for (int j = 0; j <= nz; ++j) {
            for (int i = 0; i <= nx; ++i) {
                y[static_cast<std::size_t>(j) * (nx + 1) + i] =
                    static_cast<float>(zone.ground_height(box.x0 + i * s, box.z0 + j * s));
            }
        }
        const Color dirt = color_of(sim::Surface::Dirt);
        for (int j = 0; j < nz; ++j) {
            const double z = box.z0 + j * s;
            int flat_from = -1;
            for (int i = 0; i <= nx; ++i) {
                bool relief = false;
                sim::Surface surface = sim::Surface::Dirt;
                if (i < nx) {
                    const std::size_t a = static_cast<std::size_t>(j) * (nx + 1) + i;
                    const std::size_t c = a + static_cast<std::size_t>(nx + 1);
                    relief = y[a] != 0.0f || y[a + 1] != 0.0f || y[c] != 0.0f || y[c + 1] != 0.0f;
                    if (!relief) {
                        surface = zone.surface_at(box.x0 + (i + 0.5) * s, z + 0.5 * s);
                        relief = surface != sim::Surface::Dirt;
                    }
                }
                if ((relief || i == nx) && flat_from >= 0) {
                    const double x0 = box.x0 + flat_from * s;
                    const double x1 = box.x0 + i * s;
                    ground.quad(v3(x0, 0.0, z), v3(x0, 0.0, z + s), v3(x1, 0.0, z + s),
                                v3(x1, 0.0, z), dirt);
                    flat_from = -1;
                }
                if (i == nx) {
                    break;
                }
                if (!relief) {
                    if (flat_from < 0) {
                        flat_from = i;
                    }
                    continue;
                }
                const double x = box.x0 + i * s;
                const std::size_t a = static_cast<std::size_t>(j) * (nx + 1) + i;
                const std::size_t c = a + static_cast<std::size_t>(nx + 1);
                const Vector3 p00 = v3(x, y[a], z);
                const Vector3 p10 = v3(x + s, y[a + 1], z);
                const Vector3 p11 = v3(x + s, y[c + 1], z + s);
                const Vector3 p01 = v3(x, y[c], z + s);
                const Color color = color_of(zone.surface_at(x + 0.5 * s, z + 0.5 * s));
                ground.triangle(p00, p01, p11, color);
                ground.triangle(p00, p11, p10, color);
            }
        }
    }
    for (const Rect& r : background) {
        ground.quad(v3(r.x0, 0.0, r.z0), v3(r.x0, 0.0, r.z1), v3(r.x1, 0.0, r.z1),
                    v3(r.x1, 0.0, r.z0), color_of(sim::Surface::Dirt));
    }
    // Muros perimetrales: cajas grises con colores planos (las cinco caras visibles).
    for (const sim::WallBox& wall : zone.walls()) {
        const double x0 = wall.cx - wall.half_x;
        const double x1 = wall.cx + wall.half_x;
        const double z0 = wall.cz - wall.half_z;
        const double z1 = wall.cz + wall.half_z;
        const double y0 = wall.cy - wall.half_y;
        const double y1 = wall.cy + wall.half_y;
        ground.quad(v3(x0, y1, z0), v3(x0, y1, z1), v3(x1, y1, z1), v3(x1, y1, z0), kWall);
        // Caras verticales con la normal hacia fuera de la caja (para el sombreado).
        ground.quad(v3(x0, y0, z0), v3(x0, y1, z0), v3(x1, y1, z0), v3(x1, y0, z0), kWall);
        ground.quad(v3(x1, y0, z1), v3(x1, y1, z1), v3(x0, y1, z1), v3(x0, y0, z1), kWall);
        ground.quad(v3(x0, y0, z1), v3(x0, y1, z1), v3(x0, y1, z0), v3(x0, y0, z0), kWall);
        ground.quad(v3(x1, y0, z0), v3(x1, y1, z0), v3(x1, y1, z1), v3(x1, y0, z1), kWall);
    }
    ground.upload(ground_);

    // Áreas llanas: rectángulos y calzadas, en el plano del suelo.
    TriangleList decals;
    for (const ZoneArea& a : zone.areas()) {
        if (is_relief(a.id) || is_road(a.id)) {
            continue;
        }
        const Color c = color_of(a.surface);
        if (!a.road) {
            decals.quad(v3(a.min_x, 0.0, a.min_z), v3(a.min_x, 0.0, a.max_z),
                        v3(a.max_x, 0.0, a.max_z), v3(a.max_x, 0.0, a.min_z), c);
            continue;
        }
        const double length = TestZone::axis_length(a);
        const int steps = static_cast<int>(std::ceil(length / 0.5));
        for (int i = 0; i < steps; ++i) {
            const sim::AxisPoint p = TestZone::axis_point(a, length * i / steps);
            const sim::AxisPoint q = TestZone::axis_point(a, length * (i + 1) / steps);
            const double w = a.half_width;
            decals.quad(v3(p.x - w * std::cos(p.psi), 0.0, p.z - w * std::sin(p.psi)),
                        v3(q.x - w * std::cos(q.psi), 0.0, q.z - w * std::sin(q.psi)),
                        v3(q.x + w * std::cos(q.psi), 0.0, q.z + w * std::sin(q.psi)),
                        v3(p.x + w * std::cos(p.psi), 0.0, p.z + w * std::sin(p.psi)), c);
        }
    }
    decals.upload(decals_);

    // Líneas pintadas: tiras de `width` a lo largo de una polilínea.
    TriangleList lines;
    const auto polyline = [&](const std::vector<std::array<double, 2>>& points, bool closed,
                              float width, Color color) {
        const std::size_t count = points.size();
        const std::size_t segments = closed ? count : count - 1;
        for (std::size_t i = 0; i < segments; ++i) {
            const auto& p = points[i];
            const auto& q = points[(i + 1) % count];
            const double dx = q[0] - p[0];
            const double dz = q[1] - p[1];
            const double length = std::sqrt(dx * dx + dz * dz);
            const double nx = -dz / length * 0.5 * width;
            const double nz = dx / length * 0.5 * width;
            lines.quad(v3(p[0] - nx, painted_y(p[0] - nx, p[1] - nz), p[1] - nz),
                       v3(q[0] - nx, painted_y(q[0] - nx, q[1] - nz), q[1] - nz),
                       v3(q[0] + nx, painted_y(q[0] + nx, q[1] + nz), q[1] + nz),
                       v3(p[0] + nx, painted_y(p[0] + nx, p[1] + nz), p[1] + nz), color);
        }
    };
    const auto circle = [&](double cx, double cz, double radius) {
        std::vector<std::array<double, 2>> points;
        const int steps = static_cast<int>(std::ceil(2.0 * kPi * radius / 0.5));
        for (int i = 0; i < steps; ++i) {
            const double t = 2.0 * kPi * i / steps;
            points.push_back({cx + radius * std::cos(t), cz + radius * std::sin(t)});
        }
        polyline(points, true, 0.2f, kLineWhite);
    };
    const ZoneArea& circle_area = zone.area(AreaId::Circle);
    circle(circle_area.cx, circle_area.cz, zone::kCircleRadius);
    for (const AreaId patch : {AreaId::PatchDirt, AreaId::PatchSnow, AreaId::PatchSand}) {
        const ZoneArea& a = zone.area(patch);
        circle(a.cx, a.cz, zone::kPatchCircleRadius);
    }
    // Bordes de las calzadas (en el modo a ciegas son lo único que marca la pista).
    for (const AreaId road : {AreaId::Hairpin, AreaId::BlindTrack}) {
        const ZoneArea& a = zone.area(road);
        const double length = TestZone::axis_length(a);
        const int steps = static_cast<int>(std::ceil(length / 0.5));
        for (const double side : {-1.0, 1.0}) {
            std::vector<std::array<double, 2>> points;
            const double offset = side * (a.half_width - 0.25);
            for (int i = 0; i < steps + (a.closed ? 0 : 1); ++i) {
                const sim::AxisPoint p = TestZone::axis_point(a, length * i / steps);
                points.push_back({p.x + offset * std::cos(p.psi), p.z + offset * std::sin(p.psi)});
            }
            polyline(points, a.closed, kLineWidth, kLineWhite);
        }
    }
    // Carretera: líneas de salida y meta y, en el modo a ciegas (donde los colores no la
    // marcan), los bordes de la calzada, como la pista ciega.
    for (const sim::FinishLine* line : {&zone.road_start_line(), &zone.road_finish_line()}) {
        const double half = zone::kRoadHalfWidth;
        polyline({{line->x - half * -line->forward_z, line->z - half * line->forward_x},
                  {line->x + half * -line->forward_z, line->z + half * line->forward_x}},
                 false, 0.6f, kLineWhite);
    }
    if (blind) {
        const sim::TestRoad& road = zone.road();
        for (const double side : {-1.0, 1.0}) {
            std::vector<std::array<double, 2>> points;
            const double offset = side * (zone::kRoadHalfWidth - 0.25);
            for (double s = road.first_s(); s <= road.last_s(); s += 0.5) {
                const sim::RoadPoint p = road.point(s);
                points.push_back({p.x + offset * std::cos(p.psi), p.z + offset * std::sin(p.psi)});
            }
            polyline(points, false, kLineWidth, kLineWhite);
        }
    }
    // Línea de meta de la pista ciega.
    const sim::FinishLine& finish = zone.finish_line();
    const double half_road = 0.5 * zone::kBlindRoadWidth;
    polyline({{finish.x - half_road * -finish.forward_z, finish.z - half_road * finish.forward_x},
              {finish.x + half_road * -finish.forward_z, finish.z + half_road * finish.forward_x}},
             false, 0.6f, kLineWhite);
    // Recta: una línea cada 100 m y, en amarillo, el principio de los baches y de la grava.
    const ZoneArea& straight = zone.area(AreaId::Straight);
    for (int i = 0; i <= 10; ++i) {
        const double z = straight.max_z - 100.0 * i;
        polyline({{straight.min_x, z}, {straight.max_x, z}}, false, 0.3f,
                 i == 10 ? kLineYellow : kLineWhite);
    }
    const ZoneArea& gravel_bumps = zone.area(AreaId::BumpsGravel);
    polyline({{gravel_bumps.min_x, gravel_bumps.max_z}, {gravel_bumps.max_x, gravel_bumps.max_z}},
             false, 0.3f, kLineYellow);
    lines.upload(lines_);

    // Conos del eslalon y postes cada 100 m a los lados de la recta.
    // Árboles de la carretera: el del bosque de medida a escala 1 (tronco de 0,25 m, como el
    // cilindro de Jolt), hundido 5 cm y con un giro distinto cada uno (solo se ve).
    {
        const sim::TestRoad& road = zone.road();
        trees_center_ = {0.5 * (road.min_x() + road.max_x()), 0.0,
                         0.5 * (road.min_z() + road.max_z())};
        std::vector<Matrix> transforms;
        transforms.reserve(road.trees().size());
        for (std::size_t i = 0; i < road.trees().size(); ++i) {
            const sim::TreeSpot& t = road.trees()[i];
            const auto yaw = static_cast<float>(2.399963 * static_cast<double>(i));
            transforms.push_back(MatrixMultiply(
                MatrixRotateY(yaw), MatrixTranslate(static_cast<float>(t.x - trees_center_.x),
                                                    static_cast<float>(t.y - 0.05),
                                                    static_cast<float>(t.z - trees_center_.z))));
        }
        trees_ = std::make_unique<TreeInstances>(transforms);
    }
    cones_ = zone.slalom_cones();
    posts_.clear();
    for (int i = 0; i <= 10; ++i) {
        const double z = straight.max_z - 100.0 * i;
        posts_.push_back({straight.min_x - 2.0, 0.0, z});
        posts_.push_back({straight.max_x + 2.0, 0.0, z});
    }
}

void ZoneDraw::update_near(const Vec3d& eye) {
    using Clock = std::chrono::steady_clock;
    const int n = near_cells_;
    const auto floor_index = [](double v) {
        return static_cast<std::int64_t>(std::floor(v / kNearSpacing));
    };
    const std::int64_t ax = floor_index(eye.x) - n / 2;
    const std::int64_t az = floor_index(eye.z) - n / 2;
    if (near_valid_ && ax == anchor_x_ && az == anchor_z_) {
        return; // la rejilla no se ha movido: la textura sirve tal cual
    }
    const std::int64_t dx = ax - anchor_x_;
    const std::int64_t dz = az - anchor_z_;
    const bool full = !near_valid_ || dx <= -n || dx >= n || dz <= -n || dz >= n;
    anchor_x_ = ax;
    anchor_z_ = az;
    const auto slot = [n](std::int64_t g) {
        const std::int64_t m = g % n;
        return static_cast<int>(m < 0 ? m + n : m);
    };
    // Geometría, h y superficie de la celda (gx, gz) del mundo, en su sitio del anillo.
    const auto compute = [this, n](std::int64_t gx, std::int64_t gz, int sx, int sz) {
        ++breakdown_.cells;
        const double x = static_cast<double>(gx) * kNearSpacing;
        const double z = static_cast<double>(gz) * kNearSpacing;
        float* texel = texels_.data() + 4 * (static_cast<std::size_t>(sz) * n + sx);
        const sim::TestZone::MicroSample micro = zone_->micro_sample(x, z);
        texel[0] = static_cast<float>(zone_->ground_height(x, z));
        texel[1] = static_cast<float>(micro.micro);
        texel[2] = static_cast<float>(micro.surface);
    };
    const int format = RL_PIXELFORMAT_UNCOMPRESSED_R32G32B32A32;
    if (full) {
        // Al empezar o tras un salto (recolocar): toda la rejilla.
        for (int j = 0; j < n; ++j) {
            const std::int64_t gz = anchor_z_ + j;
            const int sz = slot(gz);
            for (int i = 0; i < n; ++i) {
                const std::int64_t gx = anchor_x_ + i;
                compute(gx, gz, slot(gx), sz);
            }
        }
        const Clock::time_point t0 = Clock::now();
        rlUpdateTexture(near_texture_, 0, 0, n, n, format, texels_.data());
        breakdown_.upload_ms +=
            std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        ++breakdown_.uploads;
        near_valid_ = true;
        return;
    }
    // Solo entran |dz| filas y |dx| columnas: las filas se suben enteras y las columnas, sin
    // las celdas de las filas nuevas, también enteras.
    const std::int64_t row_first = dz > 0 ? anchor_z_ + n - dz : anchor_z_;
    const std::int64_t row_end = dz > 0 ? anchor_z_ + n : anchor_z_ - dz;
    const std::int64_t col_first = dx > 0 ? anchor_x_ + n - dx : anchor_x_;
    const std::int64_t col_end = dx > 0 ? anchor_x_ + n : anchor_x_ - dx;
    for (std::int64_t gz = row_first; gz < row_end; ++gz) {
        const int sz = slot(gz);
        int sx = slot(anchor_x_);
        for (int i = 0; i < n; ++i) {
            compute(anchor_x_ + i, gz, sx, sz);
            sx = sx + 1 == n ? 0 : sx + 1;
        }
        const Clock::time_point t0 = Clock::now();
        rlUpdateTexture(near_texture_, 0, sz, n, 1, format,
                        texels_.data() + static_cast<std::size_t>(4 * sz * n));
        breakdown_.upload_ms +=
            std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        ++breakdown_.uploads;
    }
    for (std::int64_t gx = col_first; gx < col_end; ++gx) {
        const int sx = slot(gx);
        int sz = slot(anchor_z_);
        for (int j = 0; j < n; ++j) {
            const std::int64_t gz = anchor_z_ + j;
            if (gz < row_first || gz >= row_end) {
                compute(gx, gz, sx, sz);
            }
            sz = sz + 1 == n ? 0 : sz + 1;
        }
        for (int r = 0; r < n; ++r) {
            std::memcpy(column_.data() + 4 * r,
                        texels_.data() + static_cast<std::size_t>(4 * (r * n + sx)),
                        4 * sizeof(float));
        }
        const Clock::time_point t0 = Clock::now();
        rlUpdateTexture(near_texture_, sx, 0, 1, n, format, column_.data());
        breakdown_.upload_ms +=
            std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
        ++breakdown_.uploads;
    }
}

void ZoneDraw::draw_near(const Vec3d& eye, const Vec3d& origin) {
    using Clock = std::chrono::steady_clock;
    const Clock::time_point start = Clock::now();
    update_near(eye);
    breakdown_.near_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    const int n = near_cells_;
    const auto slot = [n](std::int64_t g) {
        const std::int64_t m = g % n;
        return static_cast<int>(m < 0 ? m + n : m);
    };
    const int anchor_slot[2] = {slot(anchor_x_), slot(anchor_z_)};
    const Vec3d corner{static_cast<double>(anchor_x_) * kNearSpacing, 0.0,
                       static_cast<double>(anchor_z_) * kNearSpacing};
    const float eye_grid[2] = {static_cast<float>(eye.x - corner.x),
                               static_cast<float>(eye.z - corner.z)};
    SetShaderValue(near_shader_, loc_near_anchor_, anchor_slot, SHADER_UNIFORM_IVEC2);
    SetShaderValue(near_shader_, loc_near_eye_, eye_grid, SHADER_UNIFORM_VEC2);
    const Vec3f shift = relative_to_camera(corner, origin);
    DrawMesh(near_, near_material_, MatrixTranslate(shift.x, shift.y, shift.z));
}

void ZoneDraw::draw_mesh(const Mesh& mesh, const Vec3d& origin, float radius, float side,
                         const Vec3d& eye) {
    const Vec3f eye_rel = relative_to_camera(eye, origin);
    const float eye_xz[2] = {eye_rel.x, eye_rel.z};
    SetShaderValue(shader_, loc_radius_, &radius, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader_, loc_side_, &side, SHADER_UNIFORM_FLOAT);
    SetShaderValue(shader_, loc_eye_, eye_xz, SHADER_UNIFORM_VEC2);
    const Vec3f shift = relative_to_camera(Vec3d{}, origin);
    DrawMesh(mesh, material_, MatrixTranslate(shift.x, shift.y, shift.z));
}

void ZoneDraw::draw(const Vec3d& eye, const Vec3d& origin) {
    using Clock = std::chrono::steady_clock;
    const Clock::time_point start = Clock::now();
    if (zone_ == nullptr) {
        return;
    }
    breakdown_ = Breakdown{};
    rlDrawRenderBatchActive();
    // Sin la rejilla cercana (modo a ciegas), lo lejano llega hasta el ojo.
    const float far_radius = blind_ ? 0.0f : static_cast<float>(kNearRadius);
    for (const Mesh& mesh : ground_) {
        draw_mesh(mesh, origin, far_radius, 1.0f, eye);
    }
    if (!blind_) {
        draw_near(eye, origin);
    }
    const bool offset = gl_polygon_offset != nullptr && gl_enable != nullptr;
    if (offset) {
        gl_enable(kGlPolygonOffsetFill);
        gl_polygon_offset(-1.0f, -1.0f);
    }
    for (const Mesh& mesh : decals_) {
        draw_mesh(mesh, origin, far_radius, 1.0f, eye);
    }
    if (offset) {
        gl_polygon_offset(-2.0f, -2.0f);
    }
    for (const Mesh& mesh : lines_) {
        draw_mesh(mesh, origin, 0.0f, 1.0f, eye);
    }
    if (offset && gl_disable != nullptr) {
        gl_disable(kGlPolygonOffsetFill);
    }
    // Conos y postes por las dos caras: no dependen del orden de sus vértices.
    rlDisableBackfaceCulling();
    draw_props(origin);
    rlDrawRenderBatchActive();
    rlEnableBackfaceCulling();
    if (trees_) {
        trees_->draw(relative_to_camera(trees_center_, origin));
    }
    record_time(std::chrono::duration<double, std::milli>(Clock::now() - start).count());
}

void ZoneDraw::draw_props(const Vec3d& origin) const {
    const auto ground_y = [this](double x, double z) {
        return zone_->ground_height(x, z) + (blind_ ? 0.0 : zone_->micro_height(x, z));
    };
    rlBegin(RL_TRIANGLES);
    // Conos: pirámides de ocho lados de 0,7 m.
    constexpr int kSides = 8;
    for (const sim::AxisPoint& c : cones_) {
        const Vec3f base = relative_to_camera({c.x, ground_y(c.x, c.z), c.z}, origin);
        const Vector3 tip{base.x, base.y + 0.7f, base.z};
        for (int i = 0; i < kSides; ++i) {
            const double a0 = 2.0 * kPi * i / kSides;
            const double a1 = 2.0 * kPi * (i + 1) / kSides;
            const Vector3 p0{base.x + 0.2f * static_cast<float>(std::cos(a0)), base.y,
                             base.z + 0.2f * static_cast<float>(std::sin(a0))};
            const Vector3 p1{base.x + 0.2f * static_cast<float>(std::cos(a1)), base.y,
                             base.z + 0.2f * static_cast<float>(std::sin(a1))};
            const Vector3 n =
                Vector3CrossProduct(Vector3Subtract(p1, tip), Vector3Subtract(p0, tip));
            const Color face = shaded(kCone, n.x, n.y, n.z);
            rlColor4ub(face.r, face.g, face.b, face.a);
            rlVertex3f(tip.x, tip.y, tip.z);
            rlVertex3f(p1.x, p1.y, p1.z);
            rlVertex3f(p0.x, p0.y, p0.z);
        }
    }
    // Postes de 1,5 m.
    for (const Vec3d& p : posts_) {
        const Vec3f b = relative_to_camera({p.x, 0.0, p.z}, origin);
        constexpr float h = 0.08f;
        const Vector3 corners[4] = {{b.x - h, 0, b.z - h},
                                    {b.x + h, 0, b.z - h},
                                    {b.x + h, 0, b.z + h},
                                    {b.x - h, 0, b.z + h}};
        for (int i = 0; i < 4; ++i) {
            const Vector3 a = corners[i];
            const Vector3 c = corners[(i + 1) % 4];
            const Vector3 n = Vector3CrossProduct(Vector3Subtract(c, a), Vector3{0, 1, 0});
            const Color face = shaded(kPost, n.x, n.y, n.z);
            rlColor4ub(face.r, face.g, face.b, face.a);
            const float y0 = b.y;
            const float y1 = b.y + 1.5f;
            rlVertex3f(a.x, y0, a.z);
            rlVertex3f(c.x, y1, c.z);
            rlVertex3f(c.x, y0, c.z);
            rlVertex3f(a.x, y0, a.z);
            rlVertex3f(a.x, y1, a.z);
            rlVertex3f(c.x, y1, c.z);
        }
    }
    rlEnd();
}

void ZoneDraw::record_time(double ms) {
    last_ms_ = ms;
    history_[history_next_] = ms;
    history_next_ = (history_next_ + 1) % history_.size();
    double sum = 0.0;
    double peak = 0.0;
    for (const double v : history_) {
        sum += v;
        peak = std::max(peak, v);
    }
    mean_ms_ = sum / static_cast<double>(history_.size());
    max_ms_ = peak;
    total_ms_ += ms;
    ++total_frames_;
    total_max_ms_ = std::max(total_max_ms_, ms);
}

void ZoneDraw::reset_totals() {
    total_ms_ = 0.0;
    total_frames_ = 0;
    total_max_ms_ = 0.0;
}

double ZoneDraw::total_mean_ms() const {
    return total_frames_ > 0 ? total_ms_ / static_cast<double>(total_frames_) : 0.0;
}

} // namespace rally::platform
