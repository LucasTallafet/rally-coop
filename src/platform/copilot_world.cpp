#include "platform/copilot_world.h"

#include "io/recording.h"

#include <utility>

namespace rally::platform {

CopilotWorld build_copilot_world(const net::WorldInfo& info) {
    CopilotWorld out;
    std::string error;
    if (!io::parse_world(info.world_bytes, out.desc, error)) {
        out.error = "mundo del conductor no válido: " + error;
        return out;
    }
    const sim::CarParseResult car = sim::parse_car(info.car_text, sim::CarKeys::Full);
    if (!car.ok) {
        out.error = "coche del conductor no válido: " + car.error;
        return out;
    }
    out.desc.car_data = car.car; // la descripción serializada no lleva los datos del coche
    out.car = car.car;
    out.world = std::make_unique<sim::SimWorld>();
    out.world->build(out.desc);
    out.world->seal();
    out.zone_matches = out.world->zone_hash() == info.zone_hash;
    out.stage_matches = out.world->stage_ground_hash() == info.stage_ground_hash;
    out.ok = true;
    return out;
}

} // namespace rally::platform
