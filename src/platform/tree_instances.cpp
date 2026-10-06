#include "platform/tree_instances.h"

#include "platform/forest_layout.h"

#include <raymath.h>
#include <rlgl.h>

#include <array>
#include <cstddef>

namespace rally::platform {

namespace {

// Colores de vértice y la transformación de cada instancia en el atributo instanceTransform
// (nombre y posición por defecto de raylib).
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

} // namespace

Mesh upload_tree_mesh() {
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

TreeInstances::TreeInstances(const std::vector<Matrix>& transforms)
    : count_(static_cast<int>(transforms.size())) {
    std::vector<float16> packed;
    packed.reserve(transforms.size());
    for (const Matrix& transform : transforms) {
        packed.push_back(MatrixToFloatV(transform));
    }
    mesh_ = upload_tree_mesh();
    material_ = LoadMaterialDefault();
    material_.shader = LoadShaderFromMemory(kInstancedVertexShader, kInstancedFragmentShader);
    const Shader& shader = material_.shader;
    vao_ = rlLoadVertexArray();
    rlEnableVertexArray(vao_);
    rlEnableVertexBuffer(mesh_.vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_POSITION]);
    rlSetVertexAttribute(shader.locs[SHADER_LOC_VERTEX_POSITION], 3, RL_FLOAT, false, 0, 0);
    rlEnableVertexAttribute(shader.locs[SHADER_LOC_VERTEX_POSITION]);
    rlEnableVertexBuffer(mesh_.vboId[RL_DEFAULT_SHADER_ATTRIB_LOCATION_COLOR]);
    rlSetVertexAttribute(shader.locs[SHADER_LOC_VERTEX_COLOR], 4, RL_UNSIGNED_BYTE, true, 0, 0);
    rlEnableVertexAttribute(shader.locs[SHADER_LOC_VERTEX_COLOR]);
    vbo_ =
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

TreeInstances::~TreeInstances() {
    rlUnloadVertexArray(vao_);
    rlUnloadVertexBuffer(vbo_);
    UnloadMaterial(material_); // también descarga el shader de instancias
    UnloadMesh(mesh_);
}

void TreeInstances::draw(const Vec3f& center) const {
    if (count_ == 0) {
        return;
    }
    // Lo mismo que hace DrawMeshInstanced con las matrices, sin subir las transformaciones.
    rlDrawRenderBatchActive();
    rlPushMatrix();
    rlTranslatef(center.x, center.y, center.z);
    const Shader& shader = material_.shader;
    rlEnableShader(shader.id);
    const Matrix model_view = MatrixMultiply(rlGetMatrixTransform(), rlGetMatrixModelview());
    rlSetUniformMatrix(shader.locs[SHADER_LOC_MATRIX_MVP],
                       MatrixMultiply(model_view, rlGetMatrixProjection()));
    rlEnableVertexArray(vao_);
    rlDrawVertexArrayInstanced(0, kTreeVertices, count_);
    rlDisableVertexArray();
    rlDisableShader();
    rlPopMatrix();
}

} // namespace rally::platform
