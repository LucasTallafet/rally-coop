#include "platform/forest_scene.h"

#include "platform/forest_layout.h"

#include <raymath.h>
#include <rlgl.h>

#include <array>
#include <cstddef>

namespace rally::platform {

namespace {

constexpr std::uint64_t kForestSeed = 0x666f'7265'7374ULL;
constexpr float kGroundHalf = kForestOuterRadius + 10.0f;
constexpr float kGroundY = -0.02f; // bajo el suelo del recinto, sin pelearse con él
constexpr Color kGround{84, 96, 70, 255};

// Shader de los dos modos instanciados: colores de vértice y la transformación de cada
// instancia en el atributo instanceTransform (nombre y posición por defecto de raylib).
constexpr const char* kInstancedVertexShader = R"(#version 330
in vec3 vertexPosition;
in vec4 vertexColor;
in mat4 instanceTransform;
uniform mat4 mvp;
out vec4 fragColor;
void main() {
    fragColor = vertexColor;
    gl_Position = mvp * instanceTransform * vec4(vertexPosition, 1.0);
}
)";

constexpr const char* kInstancedFragmentShader = R"(#version 330
in vec4 fragColor;
out vec4 finalColor;
void main() {
    finalColor = fragColor;
}
)";

Mesh upload_tree() {
    const std::array<TreeVertex, kTreeVertices> tree = build_tree();
    Mesh mesh{};
    mesh.vertexCount = kTreeVertices;
    mesh.triangleCount = kTreeTriangles;
    // UnloadMesh libera estos arrays con RL_FREE.
    mesh.vertices = static_cast<float*>(MemAlloc(kTreeVertices * 3 * sizeof(float)));
    mesh.colors = static_cast<unsigned char*>(MemAlloc(kTreeVertices * 4));
    for (std::size_t i = 0; i < tree.size(); ++i) {
        mesh.vertices[3 * i + 0] = tree[i].x;
        mesh.vertices[3 * i + 1] = tree[i].y;
        mesh.vertices[3 * i + 2] = tree[i].z;
        mesh.colors[4 * i + 0] = tree[i].r;
        mesh.colors[4 * i + 1] = tree[i].g;
        mesh.colors[4 * i + 2] = tree[i].b;
        mesh.colors[4 * i + 3] = tree[i].a;
    }
    UploadMesh(&mesh, false);
    return mesh;
}

void draw_ground() {
    rlBegin(RL_QUADS);
    rlColor4ub(kGround.r, kGround.g, kGround.b, kGround.a);
    rlVertex3f(-kGroundHalf, kGroundY, -kGroundHalf);
    rlVertex3f(-kGroundHalf, kGroundY, kGroundHalf);
    rlVertex3f(kGroundHalf, kGroundY, kGroundHalf);
    rlVertex3f(kGroundHalf, kGroundY, -kGroundHalf);
    rlEnd();
}

} // namespace

const char* forest_mode_name(ForestMode mode) {
    switch (mode) {
    case ForestMode::Off:
        return "sin bosque";
    case ForestMode::Single:
        return "uno a uno (DrawMesh)";
    case ForestMode::RaylibInstanced:
        return "DrawMeshInstanced de raylib";
    case ForestMode::Persistent:
        return "instanciado persistente";
    }
    return "";
}

ForestMode next_forest_mode(ForestMode mode) {
    switch (mode) {
    case ForestMode::Off:
        return ForestMode::Single;
    case ForestMode::Single:
        return ForestMode::RaylibInstanced;
    case ForestMode::RaylibInstanced:
        return ForestMode::Persistent;
    case ForestMode::Persistent:
        return ForestMode::Off;
    }
    return ForestMode::Off;
}

ForestScene::ForestScene() {
    std::vector<TreePlacement> trees(kForestTrees);
    place_trees(trees, kForestSeed, kForestInnerRadius, kForestOuterRadius);
    transforms_.reserve(trees.size());
    std::vector<float16> packed;
    packed.reserve(trees.size());
    for (const TreePlacement& tree : trees) {
        const Matrix transform =
            MatrixMultiply(MatrixMultiply(MatrixScale(tree.scale, tree.scale, tree.scale),
                                          MatrixRotateY(tree.yaw)),
                           MatrixTranslate(tree.x, 0.0f, tree.z));
        transforms_.push_back(transform);
        packed.push_back(MatrixToFloatV(transform));
    }

    single_mesh_ = upload_tree();
    instanced_mesh_ = upload_tree();
    default_material_ = LoadMaterialDefault();
    instanced_material_ = LoadMaterialDefault();
    instanced_material_.shader =
        LoadShaderFromMemory(kInstancedVertexShader, kInstancedFragmentShader);
    const Shader& shader = instanced_material_.shader;

    // VAO persistente: posiciones y colores de single_mesh_ y las transformaciones, que no
    // vuelven a subirse.
    persistent_vao_ = rlLoadVertexArray();
    rlEnableVertexArray(persistent_vao_);
    rlEnableVertexBuffer(single_mesh_.vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION]);
    rlSetVertexAttribute(shader.locs[SHADER_LOC_VERTEX_POSITION], 3, RL_FLOAT, false, 0, 0);
    rlEnableVertexAttribute(shader.locs[SHADER_LOC_VERTEX_POSITION]);
    rlEnableVertexBuffer(single_mesh_.vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_COLOR]);
    rlSetVertexAttribute(shader.locs[SHADER_LOC_VERTEX_COLOR], 4, RL_UNSIGNED_BYTE, true, 0, 0);
    rlEnableVertexAttribute(shader.locs[SHADER_LOC_VERTEX_COLOR]);
    persistent_vbo_ =
        rlLoadVertexBuffer(packed.data(), static_cast<int>(packed.size() * sizeof(float16)), false);
    for (int column = 0; column < 4; ++column) {
        const auto location =
            static_cast<unsigned int>(shader.locs[SHADER_LOC_VERTEX_INSTANCETRANSFORM] + column);
        rlEnableVertexAttribute(location);
        rlSetVertexAttribute(location, 4, RL_FLOAT, false, sizeof(float16),
                             column * static_cast<int>(sizeof(Vector4)));
        rlSetVertexAttributeDivisor(location, 1);
    }
    rlDisableVertexBuffer();
    rlDisableVertexArray();
}

ForestScene::~ForestScene() {
    rlUnloadVertexArray(persistent_vao_);
    rlUnloadVertexBuffer(persistent_vbo_);
    UnloadMaterial(instanced_material_); // también descarga el shader de instancias
    UnloadMaterial(default_material_);
    UnloadMesh(instanced_mesh_);
    UnloadMesh(single_mesh_);
}

void ForestScene::draw(ForestMode mode, const Vec3f& center) const {
    if (mode == ForestMode::Off) {
        return;
    }
    rlPushMatrix();
    rlTranslatef(center.x, center.y, center.z);
    draw_ground();
    switch (mode) {
    case ForestMode::Off:
        break;
    case ForestMode::Single:
        for (const Matrix& transform : transforms_) {
            DrawMesh(single_mesh_, default_material_, transform);
        }
        break;
    case ForestMode::RaylibInstanced:
        DrawMeshInstanced(instanced_mesh_, instanced_material_, transforms_.data(),
                          static_cast<int>(transforms_.size()));
        break;
    case ForestMode::Persistent: {
        // Lo mismo que hace DrawMeshInstanced con las matrices, sin subir las transformaciones.
        rlDrawRenderBatchActive();
        const Shader& shader = instanced_material_.shader;
        rlEnableShader(shader.id);
        const Matrix model_view = MatrixMultiply(rlGetMatrixTransform(), rlGetMatrixModelview());
        rlSetUniformMatrix(shader.locs[SHADER_LOC_MATRIX_MVP],
                           MatrixMultiply(model_view, rlGetMatrixProjection()));
        rlEnableVertexArray(persistent_vao_);
        rlDrawVertexArrayInstanced(0, kTreeVertices, static_cast<int>(transforms_.size()));
        rlDisableVertexArray();
        rlDisableShader();
        break;
    }
    }
    rlPopMatrix();
}

} // namespace rally::platform
