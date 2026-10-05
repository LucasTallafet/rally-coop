#include "car_worlds.h"
#include "platform/fixed_step_clock.h"
#include "platform/input_shaping.h"
#include "sim/input_smoothing.h"
#include "sim/jolt_runtime.h"
#include "sim/sim_input.h"
#include "sim/sim_world.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

using rally::platform::FixedStepClock;
using rally::platform::KeyboardState;
using rally::platform::PadState;
using rally::sim::ControlState;
using rally::sim::kAxisMax;
using rally::sim::kButtonBrakeDigital;
using rally::sim::kButtonHandbrake;
using rally::sim::kButtonShiftUp;
using rally::sim::kButtonSteerDigital;
using rally::sim::kButtonThrottleDigital;
using rally::sim::SimInput;
using rally::sim::SmoothedAxis;
using rally::sim::SteerContext;

namespace {

SimInput steer_input(std::int16_t steer, bool digital) {
    SimInput input;
    input.steer = steer;
    input.buttons = digital ? kButtonSteerDigital : 0;
    return input;
}

SimInput throttle_input(std::int16_t throttle, bool digital) {
    SimInput input;
    input.throttle = throttle;
    input.buttons = digital ? kButtonThrottleDigital : 0;
    return input;
}

// Pasos hasta que la dirección aplicada llega a `target` con la misma entrada en cada paso.
int steps_until_steer(ControlState& controls, const SimInput& input, std::int32_t target,
                      const SteerContext& context = {}) {
    for (int steps = 1; steps <= 1000; ++steps) {
        controls.step(input, context);
        if (controls.steer.applied == target) {
            return steps;
        }
    }
    return -1;
}

int steps_until_throttle(ControlState& controls, const SimInput& input, std::int32_t target) {
    for (int steps = 1; steps <= 1000; ++steps) {
        controls.step(input);
        if (controls.throttle.applied == target) {
            return steps;
        }
    }
    return -1;
}

SimInput brake_input(std::int16_t brake, bool digital) {
    SimInput input;
    input.brake = brake;
    input.buttons = digital ? kButtonBrakeDigital : 0;
    return input;
}

int steps_until_brake(ControlState& controls, const SimInput& input, std::int32_t target) {
    for (int steps = 1; steps <= 1000; ++steps) {
        controls.step(input);
        if (controls.brake.applied == target) {
            return steps;
        }
    }
    return -1;
}

int steps_until_handbrake(ControlState& controls, const SimInput& input, std::int32_t target) {
    for (int steps = 1; steps <= 1000; ++steps) {
        controls.step(input);
        if (controls.handbrake == target) {
            return steps;
        }
    }
    return -1;
}

} // namespace

TEST_CASE("input smoothing: a held key reaches full steer in 0.25 s and returns in 0.12 s") {
    ControlState controls;
    CHECK(steps_until_steer(controls, steer_input(kAxisMax, true), kAxisMax) ==
          rally::sim::kSteerDigitalRiseSteps);
    CHECK(rally::sim::kSteerDigitalRiseSteps == 60); // 0,25 s a 240 Hz
    // Al soltar, el objetivo es 0 y sin bit: vuelve con el modo del último objetivo (teclado).
    CHECK(steps_until_steer(controls, SimInput{}, 0) == rally::sim::kSteerDigitalFallSteps);
    CHECK(rally::sim::kSteerDigitalFallSteps == 29); // 0,12 s

    // A la izquierda, igual.
    CHECK(steps_until_steer(controls, steer_input(-kAxisMax, true), -kAxisMax) == 60);
    CHECK(steps_until_steer(controls, SimInput{}, 0) == 29);
}

TEST_CASE("input smoothing: the stick reaches full steer in 0.10 s at rest, the same back") {
    ControlState controls;
    CHECK(steps_until_steer(controls, steer_input(kAxisMax, false), kAxisMax) ==
          rally::sim::kSteerAnalogSteps);
    CHECK(rally::sim::kSteerAnalogSteps == 24); // 0,10 s a 240 Hz
    CHECK(steps_until_steer(controls, SimInput{}, 0) == rally::sim::kSteerAnalogSteps);
    // Un objetivo intermedio se alcanza exacto, sin pasarse.
    CHECK(steps_until_steer(controls, steer_input(1000, false), 1000) == 1);
}

// Pasos exactos (240 × tiempo) hasta el tope y de vuelta, con la velocidad del coche inyectada.
TEST_CASE("input smoothing: steer steps to full and back grow with the car speed") {
    struct Row {
        float kmh;
        bool digital;
        int rise;
        int fall;
    };
    // Teclado: 0,25 s y 0,12 s parado; 0,6 s y 0,3 s a 100 km/h o más; a 50 km/h, 102 y 50,5
    // (redondeado hacia arriba: 51). Stick: 0,10 s parado y 0,35 s a 100 km/h, igual al volver.
    const std::array<Row, 7> rows = {Row{0.0f, true, 60, 29},    Row{50.0f, true, 102, 51},
                                     Row{100.0f, true, 144, 72}, Row{150.0f, true, 144, 72},
                                     Row{0.0f, false, 24, 24},   Row{100.0f, false, 84, 84},
                                     Row{150.0f, false, 84, 84}};
    for (const Row& row : rows) {
        INFO(row.kmh, " km/h, ", row.digital ? "teclado" : "stick");
        SteerContext context;
        context.speed = row.kmh / 3.6f;
        ControlState controls;
        CHECK(steps_until_steer(controls, steer_input(kAxisMax, row.digital), kAxisMax, context) ==
              row.rise);
        CHECK(steps_until_steer(controls, SimInput{}, 0, context) == row.fall);
        CHECK(steps_until_steer(controls, steer_input(-kAxisMax, row.digital), -kAxisMax,
                                context) == row.rise);
        CHECK(steps_until_steer(controls, SimInput{}, 0, context) == row.fall);
    }
}

TEST_CASE("input smoothing: the assist clips the target before the hand rate limiter") {
    ControlState controls;
    SteerContext open; // sin recorte, a 100 km/h
    open.speed = 100.0f / 3.6f;
    steps_until_steer(controls, steer_input(kAxisMax, false), kAxisMax, open);

    // La ventana aparece de golpe (aterrizaje, cruce de los 30 km/h): el aplicado baja hacia
    // ella a la velocidad de las manos, nunca de un salto.
    SteerContext window = open;
    window.right_max = 6000;
    const rally::sim::AxisRates rates = rally::sim::steer_rates(window.speed, false);
    std::int32_t previous = controls.steer.applied;
    int steps = 0;
    while (controls.steer.applied != 6000 && steps < 1000) {
        const std::int32_t aided = controls.step(steer_input(kAxisMax, false), window);
        CHECK(aided == 6000);
        CHECK(previous - controls.steer.applied <= rates.fall);
        CHECK(controls.steer.applied >= 6000);
        previous = controls.steer.applied;
        ++steps;
    }
    CHECK(steps == (kAxisMax - 6000 + rates.fall - 1) / rates.fall);

    // Hacia el otro lado (contravolante sin ventana a la izquierda) no recorta.
    CHECK(controls.step(steer_input(-kAxisMax, false), window) == -kAxisMax);
    // Con el volante centrado, el objetivo es 0: la ayuda nunca gira las ruedas por su cuenta.
    window.right_max = 0;
    window.left_max = 0;
    CHECK(controls.step(SimInput{}, window) == 0);
    // Un recorte a 0 no cambia el modo del eje (sigue el del objetivo del jugador).
    controls.step(steer_input(kAxisMax, true), window);
    CHECK(controls.steer.digital);
}

TEST_CASE("input smoothing: reversing the key returns to center first, then rises") {
    ControlState controls;
    steps_until_steer(controls, steer_input(kAxisMax, true), kAxisMax);
    // Hasta el 0 a la velocidad de vuelta, y de ahí al otro tope a la de subida.
    CHECK(steps_until_steer(controls, steer_input(-kAxisMax, true), 0) == 29);
    CHECK(steps_until_steer(controls, steer_input(-kAxisMax, true), -kAxisMax) == 60);
}

TEST_CASE("input smoothing: releasing a key with a gamepad present still returns at key speed") {
    ControlState controls;
    steps_until_steer(controls, steer_input(kAxisMax, true), kAxisMax);
    // Con un mando conectado y quieto, combine_inputs da objetivo 0 sin el bit digital.
    const SimInput released = rally::platform::combine_inputs(KeyboardState{}, PadState{true});
    REQUIRE(released.steer == 0);
    REQUIRE((released.buttons & kButtonSteerDigital) == 0);
    CHECK(steps_until_steer(controls, released, 0) == 29);
}

TEST_CASE("input smoothing: digital throttle ramps, analog throttle is applied as is") {
    ControlState controls;
    CHECK(steps_until_throttle(controls, throttle_input(kAxisMax, true), kAxisMax) == 48);
    CHECK(steps_until_throttle(controls, SimInput{}, 0) == 24);
    CHECK(steps_until_throttle(controls, throttle_input(kAxisMax, false), kAxisMax) == 1);
    CHECK(steps_until_throttle(controls, throttle_input(12345, false), 12345) == 1);
    CHECK(steps_until_throttle(controls, throttle_input(0, false), 0) == 1);
}

TEST_CASE("input smoothing: digital brake ramps in 48 steps and releases in 24, analog as is") {
    ControlState controls;
    CHECK(steps_until_brake(controls, brake_input(kAxisMax, true), kAxisMax) == 48);
    CHECK(steps_until_brake(controls, SimInput{}, 0) == 24);
    CHECK(steps_until_brake(controls, brake_input(kAxisMax, false), kAxisMax) == 1);
    CHECK(steps_until_brake(controls, brake_input(20000, false), 20000) == 1);
    CHECK(steps_until_brake(controls, brake_input(0, false), 0) == 1);
    // El freno no mueve el acelerador ni al revés.
    CHECK(controls.throttle.applied == 0);
}

TEST_CASE("input smoothing: the handbrake is fully applied in 12 steps and released in 12") {
    ControlState controls;
    SimInput pull;
    pull.buttons = kButtonHandbrake;
    CHECK(steps_until_handbrake(controls, pull, kAxisMax) == rally::sim::kHandbrakeSteps);
    CHECK(rally::sim::kHandbrakeSteps == 12); // 0,05 s
    CHECK(steps_until_handbrake(controls, SimInput{}, 0) == 12);
}

TEST_CASE("input smoothing: holding shift up for 100 steps is one press") {
    ControlState controls;
    SimInput shift;
    shift.buttons = kButtonShiftUp;
    int presses = 0;
    for (int i = 0; i < 100; ++i) {
        controls.step(shift);
        presses += (controls.pressed & kButtonShiftUp) != 0 ? 1 : 0;
    }
    CHECK(presses == 1);

    // Pulsado en un fotograma de 8 pasos (30 fps; la entrada vale para los 8): 1, no 8.
    controls.step(SimInput{});
    presses = 0;
    for (int i = 0; i < 8; ++i) {
        controls.step(shift);
        presses += (controls.pressed & kButtonShiftUp) != 0 ? 1 : 0;
    }
    CHECK(presses == 1);
    CHECK(controls.previous_buttons == kButtonShiftUp);
}

TEST_CASE("input smoothing: zero_axes clears the applied axes and keeps previous buttons") {
    ControlState controls;
    SimInput input;
    input.steer = kAxisMax;
    input.throttle = kAxisMax;
    input.brake = kAxisMax;
    input.buttons = kButtonHandbrake | kButtonShiftUp;
    for (int i = 0; i < 100; ++i) {
        controls.step(input);
    }
    REQUIRE(controls.handbrake == kAxisMax);
    controls.zero_axes();
    CHECK(controls.steer.applied == 0);
    CHECK(controls.throttle.applied == 0);
    CHECK(controls.brake.applied == 0);
    CHECK(controls.handbrake == 0);
    CHECK(controls.previous_buttons == input.buttons);
}

TEST_CASE("input smoothing: follow_target never overshoots and stops at center when reversing") {
    const rally::sim::AxisRates rates{1000, 3000};
    CHECK(rally::sim::follow_target(0, 500, rates) == 500);
    CHECK(rally::sim::follow_target(0, 5000, rates) == 1000);
    CHECK(rally::sim::follow_target(5000, 4000, rates) == 4000);
    CHECK(rally::sim::follow_target(5000, 0, rates) == 2000);
    CHECK(rally::sim::follow_target(2000, -5000, rates) == 0);
    CHECK(rally::sim::follow_target(-2000, 5000, rates) == 0);
    CHECK(rally::sim::follow_target(-5000, -1000, rates) == -2000);
}

namespace {

// Teclado del conductor en el fotograma de 60 fps `frame`. Los cambios caen en fotogramas
// pares (límites de los fotogramas de 30 fps), para que a 30 y a 60 fps se lea lo mismo.
KeyboardState scripted_keys(int frame) {
    KeyboardState keys;
    keys.right =
        (frame >= 6 && frame < 54) || (frame >= 80 && frame < 86) || (frame >= 100 && frame < 104);
    // Desde la 2.5b la vuelta al centro es más lenta con velocidad: la izquierda dura lo
    // bastante para cruzar el 0.
    keys.left = (frame >= 54 && frame < 78) || (frame >= 100 && frame < 104);
    keys.throttle = (frame >= 10 && frame < 70) || (frame >= 90 && frame < 92);
    keys.brake = (frame >= 60 && frame < 76) || (frame >= 120 && frame < 122);
    keys.handbrake = (frame >= 30 && frame < 34) || (frame >= 110 && frame < 112);
    keys.shift_up = (frame >= 20 && frame < 22) || (frame >= 40 && frame < 60);
    keys.shift_down = frame >= 64 && frame < 66;
    return keys;
}

constexpr int kScriptFrames60 = 140;

struct Trajectory {
    std::vector<std::int32_t> steer;       // dirección aplicada tras cada paso
    std::vector<std::int32_t> throttle;    // acelerador aplicado tras cada paso
    std::vector<std::int32_t> brake;       // freno aplicado tras cada paso
    std::vector<std::int32_t> handbrake;   // freno de mano aplicado tras cada paso
    std::vector<std::uint16_t> pressed;    // pulsaciones de cada paso
    std::vector<std::int32_t> world_steer; // la misma, leída de SimWorld
    std::string state;                     // SaveState final
};

// El bucle del juego a `steps_per_frame` (4 a 60 fps, 8 a 30 fps): reloj de paso fijo,
// entrada leída una vez por fotograma y aplicada a todos sus pasos.
Trajectory run_script(const std::array<std::int64_t, 3>& frame_cycle, int steps_per_frame) {
    const rally::sim::WorldDesc desc = rally::tests::flat_world(rally::sim::Surface::Asphalt);
    rally::sim::SimWorld world;
    world.build(desc);
    world.seal();

    FixedStepClock clock;
    ControlState controls;
    Trajectory trajectory;
    const int frames = kScriptFrames60 * 4 / steps_per_frame;
    for (int frame = 0; frame < frames; ++frame) {
        const int count = clock.advance(frame_cycle[static_cast<std::size_t>(frame % 3)]).count;
        REQUIRE(count == steps_per_frame);
        const int frame60 = frame * steps_per_frame / 4;
        const SimInput input = rally::platform::combine_inputs(scripted_keys(frame60), PadState{});
        for (int i = 0; i < count; ++i) {
            // La dirección depende de la velocidad del coche: el ControlState suelto usa el
            // mismo contexto que el mundo (calculado al principio de su paso).
            world.step(input);
            controls.step(input, world.steer_context());
            trajectory.steer.push_back(controls.steer.applied);
            trajectory.throttle.push_back(controls.throttle.applied);
            trajectory.brake.push_back(controls.brake.applied);
            trajectory.handbrake.push_back(controls.handbrake);
            trajectory.pressed.push_back(controls.pressed);
            trajectory.world_steer.push_back(world.controls().steer.applied);
        }
    }
    trajectory.state = world.save_state();
    return trajectory;
}

} // namespace

TEST_CASE("input smoothing: the same key presses give the same trajectories and presses at 30 and "
          "60 fps") {
    const rally::sim::JoltRuntime runtime;
    // Duraciones de fotograma como en test_fixed_step_clock: el ciclo de tres suma un número
    // exacto de pasos.
    const Trajectory at60 = run_script({16'666'667, 16'666'667, 16'666'666}, 4);
    const Trajectory at30 = run_script({33'333'334, 33'333'333, 33'333'333}, 8);

    REQUIRE(at60.steer.size() == static_cast<std::size_t>(kScriptFrames60 * 4));
    REQUIRE(at30.steer.size() == at60.steer.size());
    // La secuencia tiene que mover la dirección de verdad: llega a los dos topes.
    CHECK(*std::max_element(at60.steer.begin(), at60.steer.end()) == kAxisMax);
    CHECK(*std::min_element(at60.steer.begin(), at60.steer.end()) < 0);
    CHECK(*std::max_element(at60.throttle.begin(), at60.throttle.end()) == kAxisMax);
    CHECK(*std::max_element(at60.brake.begin(), at60.brake.end()) == kAxisMax);
    CHECK(*std::max_element(at60.handbrake.begin(), at60.handbrake.end()) == kAxisMax);
    // Tres pulsaciones de marcha (dos de subir y una de bajar).
    const auto count_presses = [](const Trajectory& t) {
        int n = 0;
        for (const std::uint16_t p : t.pressed) {
            n += ((p & rally::sim::kButtonShiftUp) != 0 ? 1 : 0) +
                 ((p & rally::sim::kButtonShiftDown) != 0 ? 1 : 0);
        }
        return n;
    };
    CHECK(count_presses(at60) == 3);

    // Bit a bit, paso a paso (son enteros: la igualdad es exacta).
    CHECK(at30.steer == at60.steer);
    CHECK(at30.throttle == at60.throttle);
    CHECK(at30.brake == at60.brake);
    CHECK(at30.handbrake == at60.handbrake);
    CHECK(at30.pressed == at60.pressed);
    CHECK(at60.world_steer == at60.steer);
    CHECK(at30.world_steer == at30.steer);
    CHECK(at30.state == at60.state);
}
