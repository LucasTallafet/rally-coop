#include "car_worlds.h"
#include "platform/fixed_step_clock.h"
#include "platform/input_shaping.h"
#include "platform/mouse_steer.h"
#include "sim/jolt_runtime.h"
#include "sim/sim_world.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

using rally::platform::combine_inputs;
using rally::platform::KeyboardState;
using rally::platform::MouseState;
using rally::platform::MouseSteer;
using rally::platform::PadState;
using rally::sim::kAxisMax;
using rally::sim::kButtonSteerDigital;

TEST_CASE("mouse steer: the displacement maps to the wheel exactly, clipped at full lock") {
    MouseSteer mouse;
    mouse.move(200.0f, 400);
    CHECK(mouse.steer() == 0.5f);
    CHECK(rally::platform::to_axis(mouse.steer()) == 16384); // 16383,5 al más cercano
    mouse.move(400.0f, 400);
    CHECK(mouse.position_px() == 400.0f); // acotado al tope: 600 px quedan en 400
    CHECK(mouse.steer() == 1.0f);
    CHECK(rally::platform::to_axis(mouse.steer()) == kAxisMax);
    // Al volver responde en cuanto se vuelve: los 200 px sobrantes no cuentan.
    mouse.move(-100.0f, 400);
    CHECK(mouse.steer() == 0.75f);
    mouse.move(-1000.0f, 400);
    CHECK(mouse.steer() == -1.0f);
    mouse.recenter();
    CHECK(mouse.steer() == 0.0f);
    CHECK(rally::platform::to_axis(mouse.steer()) == 0);
    // La sensibilidad cambia la escala: 400 px con 800 son medio volante.
    mouse.move(400.0f, 800);
    CHECK(mouse.steer() == 0.5f);
}

TEST_CASE("mouse steer: with the mouse chosen A and D do not steer and the axis is analog") {
    KeyboardState keys;
    keys.left = true;
    keys.throttle = true;
    const rally::sim::SimInput input = combine_inputs(keys, PadState{}, MouseState{true, 0.25f});
    CHECK(input.steer == rally::platform::to_axis(0.25f));
    CHECK((input.buttons & kButtonSteerDigital) == 0);
    CHECK(input.throttle == kAxisMax); // acelerador y freno siguen en el teclado
    // Con el ratón centrado, A tampoco gira.
    CHECK(combine_inputs(keys, PadState{}, MouseState{true, 0.0f}).steer == 0);
    // Sin el ratón, como antes.
    CHECK(combine_inputs(keys, PadState{}, MouseState{}).steer == -kAxisMax);
    // Un stick movido más que el ratón gana (empate: el mando).
    PadState pad;
    pad.active = true;
    pad.steer = -0.5f;
    CHECK(combine_inputs(keys, pad, MouseState{true, 0.25f}).steer ==
          rally::platform::to_axis(-0.5f));
    CHECK(combine_inputs(keys, pad, MouseState{true, 0.75f}).steer ==
          rally::platform::to_axis(0.75f));
}

TEST_CASE("mouse steer: the indicator bar puts the wheel between the two ends") {
    CHECK(std::string(rally::platform::wheel_bar(0.0f).data()) == "---------O---------");
    CHECK(std::string(rally::platform::wheel_bar(1.0f).data()) == "---------+--------O");
    CHECK(std::string(rally::platform::wheel_bar(-1.0f).data()) == "O--------+---------");
    CHECK(std::string(rally::platform::wheel_bar(0.5f).data()) == "---------+----O----");
    CHECK(std::string(rally::platform::wheel_bar(3.0f).data()) == "---------+--------O");
}

namespace {

// Desplazamiento del ratón en el fotograma de 30 fps `frame`: llega siempre en límites de
// fotograma comunes a 30 y 60 fps (múltiplos de 8 pasos).
float scripted_dx(int frame30) {
    if (frame30 >= 4 && frame30 < 10) {
        return 50.0f; // a la derecha hasta 300 px
    }
    if (frame30 >= 20 && frame30 < 30) {
        return -60.0f; // pasa del centro y se acota al tope izquierdo
    }
    if (frame30 == 45) {
        return 250.0f;
    }
    return 0.0f;
}

constexpr int kFrames30 = 70;

struct MouseRun {
    std::vector<std::int32_t> applied;
    std::uint64_t hash = 0;
};

// El bucle del juego a `steps_per_frame` (4 a 60 fps, 8 a 30 fps): el desplazamiento de cada
// fotograma de 30 fps llega en el primero de sus fotogramas de 60 fps; la entrada se lee una vez
// por fotograma y vale para todos sus pasos.
MouseRun run_mouse(const std::array<std::int64_t, 3>& frame_cycle, int steps_per_frame) {
    rally::sim::WorldDesc desc =
        rally::tests::flat_world(rally::sim::Surface::Asphalt, false, 60.0f / 3.6f);
    desc.aids.steer_assist = true;
    rally::sim::SimWorld world;
    world.build(desc);
    world.seal();
    rally::platform::FixedStepClock clock;
    MouseSteer mouse;
    MouseRun run;
    const int frames = kFrames30 * 8 / steps_per_frame;
    for (int frame = 0; frame < frames; ++frame) {
        const int count = clock.advance(frame_cycle[static_cast<std::size_t>(frame % 3)]).count;
        REQUIRE(count == steps_per_frame);
        const int first_step = frame * steps_per_frame;
        if (first_step % 8 == 0) {
            mouse.move(scripted_dx(first_step / 8), 400);
        }
        KeyboardState keys;
        keys.throttle = (first_step / 8) % 3 == 0;
        const rally::sim::SimInput input =
            combine_inputs(keys, PadState{}, MouseState{true, mouse.steer()});
        for (int i = 0; i < count; ++i) {
            world.step(input);
            run.applied.push_back(world.controls().steer.applied);
        }
    }
    run.hash = world.state_hash();
    return run;
}

} // namespace

TEST_CASE("mouse steer: the same displacement gives the same trajectory at 30 and 60 fps") {
    const rally::sim::JoltRuntime runtime;
    const MouseRun at60 = run_mouse({16'666'667, 16'666'667, 16'666'666}, 4);
    const MouseRun at30 = run_mouse({33'333'334, 33'333'333, 33'333'333}, 8);
    REQUIRE(at60.applied.size() == static_cast<std::size_t>(kFrames30 * 8));
    // El ratón mueve la dirección de verdad, a los dos lados.
    CHECK(*std::max_element(at60.applied.begin(), at60.applied.end()) > kAxisMax / 4);
    CHECK(*std::min_element(at60.applied.begin(), at60.applied.end()) < -kAxisMax / 4);
    CHECK(at30.applied == at60.applied);
    CHECK(at30.hash == at60.hash);
}
