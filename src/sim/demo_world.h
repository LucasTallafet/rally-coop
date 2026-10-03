#pragma once

#include "sim/sim_world.h"
#include "sim/surface.h"

namespace rally::sim {

// Mundo de la demo de la caja (temporal hasta el coche, tarea 2.4). Lo construyen la
// partida y los tests con el mismo código; rally_replay no lo necesita, porque la
// descripción va entera en la grabación.

// Índice del cuerpo controlado en demo_arena.
inline constexpr int kDemoControlled = 5;

// Suelo de 30 × 30 m, cuatro muros de 2 m, la caja controlada apoyada en el centro (su
// salida al recolocar) y dos cajas sueltas para empujar. Todo desplazado `offset` en x y z.
WorldDesc demo_arena(double offset);

// Índice del cuerpo controlado en zone_world.
inline constexpr int kZoneControlled = 0;

// Zona de pruebas (sim/test_zone.h) con la caja controlada apoyada en la salida de la recta
// (su primer punto de recolocación) y la pista ciega de `blind_surface`.
WorldDesc zone_world(Surface blind_surface);

} // namespace rally::sim
