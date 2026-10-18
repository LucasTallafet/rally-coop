#include "io/replay.h"

#include "game/actions.h"
#include "sim/sim_version.h"

#include <cinttypes>
#include <cstdio>
#include <optional>

namespace rally::io {

std::int64_t timed_steps(const sim::SimWorld& world) {
    const sim::LapState* lap = world.lap();
    return lap != nullptr ? lap->best_lap : -1;
}

RoadResult road_result(const sim::SimWorld& world) {
    RoadResult road;
    const sim::RoadRunState* run = world.road_run();
    if (run != nullptr) {
        road.last_run = run->last_run;
        road.last_resets = static_cast<std::uint32_t>(run->last_resets);
        road.runs = static_cast<std::uint32_t>(run->runs);
    }
    return road;
}

bool build_world(const Recording& recording, sim::SimWorld& world, std::string& error) {
    sim::WorldDesc desc;
    if (!parse_world(recording.header.world, desc, error, recording.header.format_version)) {
        return false;
    }
    if (desc.car) {
        const sim::CarParseResult car =
            sim::parse_car(recording.header.car_text, sim::CarKeys::Sim);
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

    // Acciones: desde el formato 7, con el texto de la cabecera.
    const bool has_actions =
        recording.header.format_version >= 7 && !recording.header.actions_text.empty();
    std::uint64_t total_steps = 0;
    for (const InputRun& run : recording.runs) {
        total_steps += run.count;
    }
    const std::vector<game::ActionEvent> no_events;
    const std::vector<game::ActionEvent>& events =
        recording.header.format_version >= 7 ? recording.events : no_events;
    const std::string events_problem = validate_events(events, total_steps);
    if (!events_problem.empty()) {
        result.problem = events_problem;
        return result;
    }
    if (!events.empty() && !has_actions) {
        result.problem = "eventos de acción sin texto de acciones en la cabecera";
        return result;
    }
    game::ActionTable table;
    std::optional<game::ActionMachine> machine;
    if (has_actions) {
        game::ActionParseResult parsed = game::parse_actions(recording.header.actions_text);
        if (!parsed.ok) {
            result.problem = "acciones de la cabecera: línea " + std::to_string(parsed.line) +
                             ": " + parsed.message;
            return result;
        }
        table = std::move(parsed.table);
        game::MachineSetup setup;
        setup.car = game::car_state_from(world);
        machine.emplace(table, setup);
    }

    std::size_t next_event = 0;
    std::uint64_t step = 0;
    for (const InputRun& run : recording.runs) {
        for (std::uint32_t i = 0; i < run.count; ++i) {
            while (machine && next_event < events.size() && events[next_event].step == step) {
                machine->apply(events[next_event++]);
            }
            world.step(run.input);
            if (machine) {
                machine->advance(game::car_state_from(world));
            }
            if (observer != nullptr) {
                observer->after_step(world);
            }
            ++step;
        }
        result.steps += run.count;
    }
    // Eventos tras el último paso.
    while (machine && next_event < events.size() && events[next_event].step == step) {
        machine->apply(events[next_event++]);
    }
    if (machine) {
        result.actions_ran = true;
        result.actions_hash = machine->timeline_hash();
    }
    result.ran = true;
    result.hash = world.state_hash();
    result.timed_steps = timed_steps(world);
    result.road = road_result(world);
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
    } else if (result.road != recording.footer.road) {
        std::snprintf(buffer, sizeof(buffer),
                      "pasada por la carretera distinta: grabada %" PRId64 " pasos (%u "
                      "recolocaciones, %u pasadas), repetida %" PRId64 " pasos (%u, %u)",
                      recording.footer.road.last_run, recording.footer.road.last_resets,
                      recording.footer.road.runs, result.road.last_run, result.road.last_resets,
                      result.road.runs);
        result.problem = buffer;
    } else if (result.actions_ran && result.actions_hash != recording.footer.actions_hash) {
        std::snprintf(buffer, sizeof(buffer),
                      "hash de las acciones distinto: grabado 0x%016" PRIx64
                      ", repetido 0x%016" PRIx64,
                      recording.footer.actions_hash, result.actions_hash);
        result.problem = buffer;
    } else {
        result.hash_ok = true;
    }
    return result;
}

ReplayResult rewrite_footer(Recording& recording) {
    // Desde la versión 3 del formato (el coche), la descripción del mundo pasa al formato actual
    // con lo que dice la grabación (una de la versión 3 queda sin ayudas, como se grabó).
    if (recording.header.format_version >= 3 &&
        recording.header.format_version < kRecordingFormatVersion) {
        sim::WorldDesc desc;
        std::string error;
        if (parse_world(recording.header.world, desc, error, recording.header.format_version)) {
            recording.header.world = serialize_world(desc);
            recording.header.format_version = kRecordingFormatVersion;
        }
    }
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
    recording.footer.road = result.road;
    recording.footer.actions_hash = result.actions_hash;
    return replay(recording);
}

} // namespace rally::io
