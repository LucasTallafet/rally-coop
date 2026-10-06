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
    for (const TreePlacement& tree : trees) {
        const Matrix transform =
            MatrixMultiply(MatrixMultiply(MatrixScale(tree.scale, tree.scale, tree.scale),
                                          MatrixRotateY(tree.yaw)),
                           MatrixTranslate(tree.x, 0.0f, tree.z));
        transforms_.push_back(transform);
    }

    single_mesh_ = upload_tree_mesh();
    instanced_mesh_ = upload_tree_mesh();
    default_material_ = LoadMaterialDefault();
    instanced_material_ = LoadMaterialDefault();
    instanced_material_.shader =
        LoadShaderFromMemory(kInstancedVertexShader, kInstancedFragmentShader);
    persistent_ = std::make_unique<TreeInstances>(transforms_);
}

ForestScene::~ForestScene() {
    persistent_.reset();
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
    case ForestMode::Persistent:
        persistent_->draw(Vec3f{});
        break;
    }
    rlPopMatrix();
}

} // namespace rally::platform
