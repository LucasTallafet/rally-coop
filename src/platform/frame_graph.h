#pragma once

#include <cstdint>

namespace rally::platform {

class FrameHistory;

// Gráfica de ritmo de fotogramas para el contador en pantalla (pasará al contador de
// rendimiento de la tarea 6). Arriba, una barra por fotograma con su duración medida
// (0–50 ms) y una marca blanca con el tiempo de juego aplicado; líneas de referencia en 1 y
// 2 periodos. Debajo, los pasos de física de cada fotograma. Colores: gris verdoso con los
// pasos previstos, amarillo para ruido de medida (~1 periodo con otros pasos), rojo para
// fotogramas perdidos (≥ 1,5 periodos), gris si no hay pasos previstos.
//
// frame_period: duración prevista del fotograma en unidades del reloj (0 si no se conoce).
// debt: deuda del ajuste de FixedStepClock. Dibuja entre BeginDrawing y EndDrawing.
// Devuelve la altura ocupada en píxeles.
int draw_frame_graph(const FrameHistory& history, std::int64_t frame_period, std::int64_t debt,
                     bool snap_enabled, int x, int y);

} // namespace rally::platform
