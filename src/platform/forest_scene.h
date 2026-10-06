#pragma once

#include "platform/camera_space.h"
#include "platform/tree_instances.h"

#include <raylib.h>

#include <memory>
#include <vector>

namespace rally::platform {

// Modos de la escena de medida "bosque denso" (B en la demo). Solo para medir.
enum class ForestMode {
    Off,
    Single,          // un DrawMesh por árbol: 20 000 draw calls
    RaylibInstanced, // DrawMeshInstanced de raylib tal cual
    Persistent,      // instanciado con el VBO de transformaciones subido una vez
};

const char* forest_mode_name(ForestMode mode);
ForestMode next_forest_mode(ForestMode mode);

// kForestTrees árboles low-poly de colores planos (forest_layout) alrededor del recinto y un
// suelo bajo ellos. Las transformaciones se guardan una vez, relativas al centro del
// recinto; en cada fotograma solo cambia el desplazamiento centro − ojo, que entra como
// matriz de modelo (render relativo a la cámara sin recalcular 20 000 matrices).
//
// - Single: DrawMesh con el shader por defecto de raylib (colores de vértice).
// - RaylibInstanced: DrawMeshInstanced; en raylib 6.0 reserva las 20 000 matrices (RL_CALLOC)
//   y crea y destruye un VBO con ellas en cada llamada.
// - Persistent: un VAO propio con los VBO de la malla y uno de transformaciones subido al
//   crear la escena; un glDrawArraysInstanced por fotograma (platform/tree_instances, el mismo
//   que dibuja los árboles de la carretera de prueba).
// Los dos instanciados usan el mismo shader. Necesita la ventana abierta.
class ForestScene {
public:
    ForestScene();
    ~ForestScene();
    ForestScene(const ForestScene&) = delete;
    ForestScene& operator=(const ForestScene&) = delete;

    // Entre BeginMode3D y EndMode3D. center: centro del recinto respecto al ojo.
    void draw(ForestMode mode, const Vec3f& center) const;

private:
    Mesh single_mesh_{};    // para DrawMesh
    Mesh instanced_mesh_{}; // para DrawMeshInstanced, que cambia los atributos de su VAO
    Material default_material_{};
    Material instanced_material_{}; // con el shader de instancias
    std::vector<Matrix> transforms_;
    std::unique_ptr<TreeInstances> persistent_;
};

} // namespace rally::platform
