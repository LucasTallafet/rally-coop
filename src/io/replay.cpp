#include "io/replay.h"

#include "sim/sim_version.h"

#include <cinttypes>
#include <cstdio>

namespace rally::io {

std::int64_t timed_steps(const sim::SimWorld& world) {
    const sim::LapState* lap = world.lap();
    return lap != nullptr ? lap->best_lap : -1;
}

bool build_world(const Recording& recording, sim::SimWorld& world, std::string& error) {
    sim::WorldDesc desc;
    if (!parse_world(recording.header.world, desc, error, recording.header.format_version)) {
        return false;
    }
    if (desc.car) {
        const sim::CarParseResult car = sim::parse_car(recording.header.car_text);
        if (!car.ok) {
            error = "coche de la grabación: " + car.error;
            if (recording.header.sim_version != sim::kSimVersion) {
                // Una grabación de otra versión lleva el .car de entonces: desde la 3 (tarea
                // 2.5) el neumático tiene otras claves y no se puede repetir.
                error += " (grabación de la versión de la simulación " +
                         std::to_string(recording.header.sim_version) + ", esta es la " +
                         std::to_string(sim::kSimVersion) +
                         ": su coche usa claves que ya no existen; rally_replay info sí la lee)";
            }
            return false;
        }
        desc.car_data = car.car;
    }
    world.build(desc);
    world.seal();
    return true;
}

ReplayResult replay(const Recording& recording, StepObserver* observer) {
    ReplayResult result;
    sim::SimWorld world;
    if (!build_world(recording, world, result.problem)) {
        return result;
    }
    world.set_telemetry(observer != nullptr);
    for (const InputRun& run : recording.runs) {
        for (std::uint32_t i = 0; i < run.count; ++i) {
            world.step(run.input);
            if (observer != nullptr) {
                observer->after_step(world);
            }
        }
        result.steps += run.count;
    }
    result.ran = true;
    result.hash = world.state_hash();
    result.timed_steps = timed_steps(world);
    result.version_mismatch = recording.header.sim_version != sim::kSimVersion;
    result.zone_mismatch = recording.header.zone_hash != world.zone_hash();
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
    } else if (result.zone_mismatch) {
        std::snprintf(buffer, sizeof(buffer),
                      "la zona ha cambiado (grabada con 0x%016" PRIx64 ", esta es 0x%016" PRIx64
                      "): no se compara el hash",
                      recording.header.zone_hash, world.zone_hash());
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
    } else if (result.timed_steps != recording.footer.timed_steps) {
        std::snprintf(buffer, sizeof(buffer),
                      "tiempo cronometrado distinto: grabado %" PRId64 " pasos, repetido %" PRId64,
                      recording.footer.timed_steps, result.timed_steps);
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
    sim::SimWorld world;
    std::string error;
    if (build_world(recording, world, error)) {
        recording.header.zone_hash = world.zone_hash();
    }
    recording.footer.complete = true;
    recording.footer.total_steps = result.steps;
    recording.footer.final_hash = result.hash;
    recording.footer.timed_steps = result.timed_steps;
    return replay(recording);
}

} // namespace rally::io
