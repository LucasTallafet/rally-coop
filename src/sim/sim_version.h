#pragma once

#include <cstdint>

namespace rally::sim {

// Versión de la simulación. Sube en 1 cada vez que un cambio intencionado altera el
// resultado de una simulación (y con ello la huella de tests/test_sim_world.cpp). Va en la
// cabecera de las grabaciones: rally_replay no compara el hash final de una grabación de
// otra versión, porque no tiene por qué coincidir.
// Versiones: 1 (tarea 2.1, la caja controlada); 2 (tarea 2.4, el coche); 3 (tarea 2.5, el
// neumático); 4 (tarea 2.5b, ayuda de dirección y velocidades de las manos).
inline constexpr std::uint32_t kSimVersion = 4;

} // namespace rally::sim
