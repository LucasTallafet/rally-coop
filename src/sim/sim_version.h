#pragma once

#include <cstdint>

namespace rally::sim {

// Versión de la simulación. Sube en 1 cada vez que un cambio intencionado altera el
// resultado de una simulación (y con ello la huella de tests/test_sim_world.cpp). Va en la
// cabecera de las grabaciones: rally_replay no compara el hash final de una grabación de
// otra versión, porque no tiene por qué coincidir.
inline constexpr std::uint32_t kSimVersion = 1;

} // namespace rally::sim
