#pragma once

#include "platform/interpolation.h"
#include "sim/stage_track.h"

namespace rally::platform {

// Cámara «todo el tramo» (tarea tramo-modo, lote 5): la vista fija de la zona, pero encuadrando la
// caja del eje (con sus prolongaciones y la franja de terreno de ±60 m). Sin raylib.
struct StageOverview {
    Vec3d eye;
    Vec3d forward; // no unitario
    double fov_y_deg = 45.0;
    // La mayor distancia en planta del ojo a lo que se ve: la distancia de dibujo de StageDraw.
    double draw_distance = 0.0;
    // Distancia del ojo al punto más lejano del encuadre, para compararla con el plano lejano.
    double far_distance = 0.0;
};

// Mira hacia abajo con cabeceo de -75° (casi desde arriba) y gira la cámara 90° si así cabe más
// cerca; encuadra la caja en una pantalla de `aspect` (ancho / alto) con un 5 % de margen.
StageOverview stage_overview(const sim::StageTrack& track, double fov_y_deg, double aspect);

} // namespace rally::platform
