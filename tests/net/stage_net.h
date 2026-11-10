#pragma once

// Mundos con tramo para los tests de la red (lote 6, red-tramo): lazadas con el 205 desde 5 m
// antes de la línea (como la grabación de test_stage_v8) y una pasada conducida con el conductor
// de test que guarda, paso a paso, el estado que el host mandaría (con la s del host).

#include "car_worlds.h"
#include "net/car_state.h"
#include "sim/check.h"
#include "sim/sim_world.h"
#include "stage_driver.h"

#include <cstdint>
#include <vector>

namespace rally::tests {

inline const sim::StageTrack& net_lazadas_track() {
    static const sim::StageTrack track = [] {
        sim::StageTrack t;
        const sim::StageParseResult r = sim::StageTrack::build(lazadas_stage_text(), t);
        RALLY_CHECK(r.ok, "tests: lazadas no carga");
        return t;
    }();
    return track;
}

inline sim::WorldDesc net_lazadas_desc(double s, float speed = 0.0f) {
    sim::WorldDesc desc;
    desc.stage_text = lazadas_stage_text();
    desc.car = true;
    desc.car_data = reference_car();
    desc.car_spawn = sim::stage_spawn(net_lazadas_track(), s, speed);
    return desc;
}

// Pasada conducida: un CarState por paso (el del paso i está en states[i - 1]), tal como lo
// daría car_state_from.
struct DrivenRun {
    std::vector<net::CarState> states;
    int resets = 0;
    int shortcuts = 0;
};

inline DrivenRun drive_lazadas(int steps, double speed_ms) {
    sim::SimWorld world;
    world.build(net_lazadas_desc(-5.0));
    world.seal();
    world.set_telemetry(true);
    StageDriver driver(net_lazadas_track(), speed_ms);
    DrivenRun run;
    run.states.reserve(static_cast<std::size_t>(steps));
    for (int i = 1; i <= steps; ++i) {
        const sim::SimInput input = driver.input(world.car_pose(), world.car_velocity());
        world.step(input);
        run.states.push_back(net::car_state_from(world, static_cast<std::uint32_t>(i), 0, 0));
    }
    run.resets = world.stage_run()->resets;
    run.shortcuts = world.stage_run()->shortcuts;
    return run;
}

} // namespace rally::tests
