#pragma once

#include "platform/camera_space.h"

#include <raylib.h>

#include <vector>

namespace rally::platform {

// Árboles low-poly de colores planos (forest_layout) dibujados con instancias: un VAO propio
// con los VBO de la malla del árbol y uno de transformaciones subido una sola vez al crearlo;
// un glDrawArraysInstanced por fotograma (el modo persistente del bosque de medida de la fase
// 1). Las transformaciones son relativas a un centro; en cada fotograma solo cambia el
// desplazamiento centro − ojo. Necesita la ventana abierta.
class TreeInstances {
public:
    explicit TreeInstances(const std::vector<Matrix>& transforms);
    ~TreeInstances();
    TreeInstances(const TreeInstances&) = delete;
    TreeInstances& operator=(const TreeInstances&) = delete;

    int count() const { return count_; }

    // Entre BeginMode3D y EndMode3D. center: el centro de las transformaciones respecto al ojo.
    void draw(const Vec3f& center) const;

private:
    Mesh mesh_{};
    Material material_{}; // con el shader de instancias
    unsigned int vao_ = 0;
    unsigned int vbo_ = 0;
    int count_ = 0;
};

// Malla del árbol subida a la GPU (la descarga UnloadMesh).
Mesh upload_tree_mesh();

} // namespace rally::platform
