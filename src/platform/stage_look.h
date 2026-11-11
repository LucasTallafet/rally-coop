#pragma once

#include "sim/stage_ground.h"
#include "sim/surface.h"

#include <array>

namespace rally::platform {

// Color del aspecto (`StageTriangle::look`) del tramo (tarea tramo-dibujo-2, lote 6). Una sola
// fuente para la malla lejana (stage_mesh) y la rejilla cercana (NearGrid con índices nuevos):
// el muro es de roca y la cuneta y el talud, de tierra algo más oscura. La calzada y el arcén
// siguen con el color de su superficie.
//
// Roca: gris azulado frío. Sombreada se separa del asfalto (96, 99, 106) por matiz (B - R = 38,
// frente a 10 del asfalto) y de la grava y la tierra por frío; un gris casi neutro cae encima
// del asfalto con el sombreado del muro (cara a 80°: x 0,84).
inline constexpr std::array<unsigned char, 3> kStageRockRgb = {100, 112, 138};
// Cuneta y talud: la tierra x este factor, para que no se confundan con el terreno lejano.
inline constexpr float kStageDitchDarken = 0.82f;

// Índices de color de la rejilla cercana: 0..4 son sim::Surface (como en ZoneDraw); los nuevos
// empiezan en kSurfaceCount.
inline constexpr int kStageColorRock = sim::kSurfaceCount;
inline constexpr int kStageColorDitch = sim::kSurfaceCount + 1;
inline constexpr int kStageColorCount = sim::kSurfaceCount + 2;

// Índice de color de un triángulo del núcleo. La cuneta y el talud solo se oscurecen si son de
// tierra (lo son siempre en el formato 2).
inline int stage_color_index(sim::Surface surface, sim::StageLook look) {
    switch (look) {
    case sim::StageLook::Wall:
        return kStageColorRock;
    case sim::StageLook::Ditch:
    case sim::StageLook::Slope:
        if (surface == sim::Surface::Dirt) {
            return kStageColorDitch;
        }
        break;
    case sim::StageLook::Road:
    case sim::StageLook::Shoulder:
        break;
    }
    return static_cast<int>(surface);
}

} // namespace rally::platform
