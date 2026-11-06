#include "platform/stage_checked.h"

#include <cstdio>
#include <utility>

namespace rally::platform {

bool build_checked_ground(const sim::StageTrack& track, std::uint64_t expected_hash,
                          sim::StageGround& out, std::string& error) {
    sim::StageGround ground = sim::StageGround::build(
        track, sim::kStageCell, sim::StageGroundKind::Design, sim::StageMeshDensity{});
    const std::uint64_t hash = ground.hash();
    if (hash != expected_hash) {
        char text[200];
        std::snprintf(text, sizeof text,
                      "el suelo del tramo dibujado (hash 0x%016llx) no es el de la física "
                      "(hash 0x%016llx)",
                      static_cast<unsigned long long>(hash),
                      static_cast<unsigned long long>(expected_hash));
        error = text;
        return false;
    }
    out = std::move(ground);
    error.clear();
    return true;
}

} // namespace rally::platform
