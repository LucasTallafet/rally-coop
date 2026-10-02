#pragma once

#include "sim/sim_world.h"

namespace rally::sim {

// Mundo de la demo de la caja (temporal hasta el coche, tarea 2.4). Lo construyen la
// partida y los tests con el mismo código; rally_replay no lo necesita, porque la
// descripción va entera en la grabación.

// Índice del cuerpo controlado en demo_arena.
inline constexpr int kDemoControlled = 5;

// Suelo de 30 × 30 m, cuatro muros de 2 m, la caja controlada apoyada en el centro (su
// salida al recolocar) y dos cajas sueltas para empujar. Todo desplazado `offset` en x y z.
WorldDesc demo_arena(double offset);

} // namespace rally::sim
