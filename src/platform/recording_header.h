#pragma once

#include "io/recording.h"
#include "io/replay.h"
#include "sim/sim_world.h"

#include <string>

namespace rally::platform {

// La cabecera de la grabación de la partida (la que arma RecordingSession::start), sin raylib para
// probarla: el mundo, el coche, los ajustes, las acciones y los hashes de la zona y del suelo del
// tramo del mundo ya construido (0 sin ellos).
io::RecordingHeader make_recording_header(const sim::WorldDesc& desc, const sim::SimWorld& world,
                                          const std::string& car_text, const std::string& settings,
                                          const std::string& actions_text);

// Cierra la grabación de la partida con lo que RecordingSession::finish pone en el pie: el hash
// del estado, la mejor vuelta, la pasada por la carretera, el hash de las acciones y la pasada por
// el tramo (io::stage_result; la de por defecto sin tramo).
io::Recording finish_recording(io::Recorder& recorder, const sim::SimWorld& world,
                               std::uint64_t actions_hash);

} // namespace rally::platform
