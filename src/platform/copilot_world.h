#pragma once

#include "net/protocol.h"
#include "sim/car_data.h"
#include "sim/sim_world.h"

#include <memory>
#include <string>

namespace rally::platform {

// El mundo del copiloto a partir de lo que manda el host (lote 3; protocolo 2 desde el lote 5):
// el mismo código que el juego, sin llamar nunca a step. Sin raylib, para probarlo sin ventana
// (tarea tramo-modo).
struct CopilotWorld {
    bool ok = false;
    std::string error; // «mundo del conductor no válido: …» o «coche del conductor no válido: …»
    sim::WorldDesc desc;
    sim::CarData car;
    std::unique_ptr<sim::SimWorld> world;
    // La zona y el suelo del tramo construidos aquí son los del host (SimWorld::zone_hash y
    // stage_ground_hash frente a los de WorldInfo). Sin zona o sin tramo valen 0 en los dos.
    bool zone_matches = true;
    bool stage_matches = true;
    bool has_stage() const { return world && world->stage() != nullptr; }
};

CopilotWorld build_copilot_world(const net::WorldInfo& info);

} // namespace rally::platform
