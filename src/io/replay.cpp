#include "io/replay.h"

#include "sim/sim_version.h"

#include <cinttypes>
#include <cstdio>

namespace rally::io {

bool build_world(const Recording& recording, sim::SimWorld& world, std::string& error) {
    sim::WorldDesc desc;
    if (!parse_world(recording.header.world, desc, error)) {
        return false;
    }
    world.build(desc);
    world.seal();
    return true;
}

ReplayResult replay(const Recording& recording) {
    ReplayResult result;
    sim::SimWorld world;
    if (!build_world(recording, world, result.problem)) {
        return result;
    }
    for (const InputRun& run : recording.runs) {
        for (std::uint32_t i = 0; i < run.count; ++i) {
            world.step(run.input);
        }
        result.steps += run.count;
    }
    result.ran = true;
    result.hash = world.state_hash();
    result.version_mismatch = recording.header.sim_version != sim::kSimVersion;
    result.incomplete = !recording.footer.complete;

    char buffer[200];
    if (result.incomplete) {
        std::snprintf(buffer, sizeof(buffer),
                      "grabación incompleta (se guardó al abortar): repetidos %" PRIu64
                      " pasos, sin hash final con el que comparar",
                      result.steps);
        result.problem = buffer;
    } else if (result.version_mismatch) {
        std::snprintf(buffer, sizeof(buffer),
                      "grabada con la versión de la simulación %u y esta es la %u: no se "
                      "compara el hash",
                      recording.header.sim_version, sim::kSimVersion);
        result.problem = buffer;
    } else if (result.steps != recording.footer.total_steps) {
        std::snprintf(buffer, sizeof(buffer),
                      "pasos distintos: grabados %" PRIu64 ", repetidos %" PRIu64,
                      recording.footer.total_steps, result.steps);
        result.problem = buffer;
    } else if (result.hash != recording.footer.final_hash) {
        std::snprintf(buffer, sizeof(buffer),
                      "hash final distinto: grabado 0x%016" PRIx64 ", repetido 0x%016" PRIx64,
                      recording.footer.final_hash, result.hash);
        result.problem = buffer;
    } else {
        result.hash_ok = true;
    }
    return result;
}

ReplayResult rewrite_footer(Recording& recording) {
    const ReplayResult result = replay(recording);
    if (!result.ran) {
        return result;
    }
    recording.header.sim_version = sim::kSimVersion;
    recording.footer.complete = true;
    recording.footer.total_steps = result.steps;
    recording.footer.final_hash = result.hash;
    return replay(recording);
}

} // namespace rally::io
