#pragma once

#include "sim/stage_ground.h"
#include "sim/stage_track.h"

#include <cstdint>
#include <string>

namespace rally::platform {

// El suelo del tramo para dibujar (tarea tramo-dibujo, lote 5): StageGround::build con los
// mismos argumentos que SimWorld::build con StageGroundVariant::Design, y su hash comparado con el
// que dio la física (SimWorld::stage_ground_hash()). Si no coincide, false con los dos valores en
// `error` y `out` no se toca. Reserva memoria y tarda: al cargar.
bool build_checked_ground(const sim::StageTrack& track, std::uint64_t expected_hash,
                          sim::StageGround& out, std::string& error);

} // namespace rally::platform
