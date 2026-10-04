#pragma once

#include "sim/car_data.h"
#include "sim/sim_world.h"
#include "sim/surface.h"

#include <cstdint>

namespace rally::sim {

// Mundos de la demo. Los construyen la partida y los tests con el mismo código; rally_replay
// no los necesita, porque la descripción va entera en la grabación (y el coche, en su texto).

// Recinto de la fase 1: suelo de asfalto de 30 × 30 m, cuatro muros de 2 m, el coche parado en
// el centro mirando a -z (su salida al recolocar) y dos cajas sueltas para empujar. Todo
// desplazado `offset` en x y z.
WorldDesc demo_arena(double offset, const CarData& car);

// Dónde sale el coche en la zona: al principio de la recta (lo normal) o en el punto de
// recolocación de la pista ciega (pruebas a ciegas), con la orientación del punto.
enum class ZoneStart : std::uint8_t { Straight, BlindTrack };

// Zona de pruebas (sim/test_zone.h) con el coche parado en la salida elegida y la pista ciega
// de `blind_surface`.
WorldDesc zone_world(Surface blind_surface, const CarData& car,
                     ZoneStart start = ZoneStart::Straight);

} // namespace rally::sim
