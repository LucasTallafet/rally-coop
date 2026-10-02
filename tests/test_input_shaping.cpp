#include "platform/input_shaping.h"
#include "sim/sim_input.h"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>

using rally::platform::combine_inputs;
using rally::platform::KeyboardState;
using rally::platform::PadReading;
using rally::platform::PadState;
using rally::platform::PadTracker;
using rally::platform::shape_steer;
using rally::platform::shape_trigger;
using rally::platform::to_axis;
using rally::sim::kAxisMax;
using rally::sim::kButtonJump;
using rally::sim::kButtonSteerDigital;
using rally::sim::kButtonThrottleDigital;

TEST_CASE("input shaping: steer deadzone and response curve") {
    CHECK(shape_steer(0.0f) == 0.0f);
    CHECK(shape_steer(0.10f) == 0.0f);
    CHECK(shape_steer(-0.10f) == 0.0f);
    CHECK(shape_steer(0.11f) > 0.0f);
    CHECK(shape_steer(0.95f) == 1.0f);
    CHECK(shape_steer(1.0f) == 1.0f);
    CHECK(shape_steer(-0.97f) == -1.0f);
    // A mitad de recorrido entre las zonas, 0,5^1,5.
    CHECK(shape_steer(0.525f) == doctest::Approx(std::pow(0.5, 1.5)).epsilon(1e-5));
    CHECK(shape_steer(-0.525f) == doctest::Approx(-std::pow(0.5, 1.5)).epsilon(1e-5));
    // Monótona.
    float previous = 0.0f;
    for (int i = 0; i <= 100; ++i) {
        const float value = shape_steer(static_cast<float>(i) / 100.0f);
        CHECK(value >= previous);
        previous = value;
    }
}

TEST_CASE("input shaping: triggers are clamped and get a small deadzone") {
    CHECK(shape_trigger(-1.0f) == 0.0f); // raylib, antes del primer movimiento
    CHECK(shape_trigger(0.0f) == 0.0f);
    CHECK(shape_trigger(0.05f) == 0.0f);
    CHECK(shape_trigger(0.525f) == doctest::Approx(0.5f));
    CHECK(shape_trigger(1.0f) == 1.0f);
    CHECK(shape_trigger(1.5f) == 1.0f);
}

TEST_CASE("input shaping: axis conversion rounds and clamps") {
    CHECK(to_axis(0.0f) == 0);
    CHECK(to_axis(1.0f) == kAxisMax);
    CHECK(to_axis(-1.0f) == -kAxisMax);
    CHECK(to_axis(2.0f) == kAxisMax);
    CHECK(to_axis(0.5f) == 16384); // 16383,5 redondeado
}

namespace {

PadReading pad(std::uint32_t id) {
    PadReading reading;
    reading.id = id;
    return reading;
}

} // namespace

TEST_CASE("input shaping: a stick drifting vertically neither steers nor takes over") {
    PadTracker tracker;
    PadReading worn = pad(1);
    worn.left_x = 0.09f;
    worn.left_y = 1.0f;
    PadState state = tracker.update(&worn, 1);
    CHECK_FALSE(state.active);
    CHECK(tracker.active_id() == 0);

    // Ya activo (por un botón), la deriva vertical sigue sin girar.
    worn.any_button = true;
    state = tracker.update(&worn, 1);
    REQUIRE(state.active);
    CHECK(state.steer == 0.0f);
    worn.any_button = false;
    worn.left_y = -1.0f;
    state = tracker.update(&worn, 1);
    CHECK(state.active);
    CHECK(state.steer == 0.0f);
    CHECK(combine_inputs(KeyboardState{}, state).steer == 0);
}

TEST_CASE("input shaping: the active gamepad is the last one used") {
    PadTracker tracker;
    PadReading pads[2] = {pad(11), pad(22)};

    // Nadie lo ha usado: ninguno activo.
    CHECK_FALSE(tracker.update(pads, 2).active);

    // B acelera: B activo.
    pads[1].right_trigger = 0.6f;
    PadState state = tracker.update(pads, 2);
    CHECK(tracker.active_id() == 22);
    CHECK(state.throttle > 0.5f);

    // A pulsa un botón mientras B sigue acelerando: B se queda (sigue en uso).
    pads[0].any_button = true;
    tracker.update(pads, 2);
    CHECK(tracker.active_id() == 22);

    // B suelta: A, que sigue usándose, pasa a activo.
    pads[1].right_trigger = 0.0f;
    tracker.update(pads, 2);
    CHECK(tracker.active_id() == 11);

    // Nadie se usa: A sigue activo.
    pads[0].any_button = false;
    tracker.update(pads, 2);
    CHECK(tracker.active_id() == 11);

    // B gira: B activo, con su dirección.
    pads[1].left_x = -1.0f;
    state = tracker.update(pads, 2);
    CHECK(tracker.active_id() == 22);
    CHECK(state.steer == -1.0f);
    CHECK(combine_inputs(KeyboardState{}, state).steer == -kAxisMax);

    // B se desconecta girando: en ese mismo fotograma no hay mando activo y los ejes son 0,
    // aunque A siga conectado.
    state = tracker.update(pads, 1);
    CHECK_FALSE(state.active);
    CHECK(tracker.active_id() == 0);
    const rally::sim::SimInput input = combine_inputs(KeyboardState{}, state);
    CHECK(input.steer == 0);
    CHECK(input.throttle == 0);

    // A gira: A activo.
    pads[0].left_x = 0.5f;
    state = tracker.update(pads, 1);
    CHECK(tracker.active_id() == 11);
    CHECK(state.steer > 0.0f);
}

TEST_CASE("input shaping: triggers are digital until they give an intermediate value") {
    PadTracker tracker;
    PadReading switch_pro = pad(5);
    switch_pro.right_trigger = 1.0f;
    PadState state = tracker.update(&switch_pro, 1);
    CHECK(state.throttle_digital);
    switch_pro.right_trigger = 0.0f;
    switch_pro.left_trigger = 1.0f;
    state = tracker.update(&switch_pro, 1);
    CHECK(state.throttle_digital);
    CHECK(state.throttle == -1.0f);
    CHECK(tracker.trigger_digital(5));

    // Un valor intermedio: analógico, y se queda así aunque vuelva a 0 o a 1.
    switch_pro.left_trigger = 0.4f;
    CHECK_FALSE(tracker.update(&switch_pro, 1).throttle_digital);
    switch_pro.left_trigger = 1.0f;
    CHECK_FALSE(tracker.update(&switch_pro, 1).throttle_digital);

    // Al desconectar se olvida: al volver, digital hasta otro valor intermedio.
    tracker.update(nullptr, 0);
    CHECK(tracker.update(&switch_pro, 1).throttle_digital);

    // raylib da -1 antes del primer movimiento: no cuenta como intermedio.
    PadReading fresh = pad(6);
    fresh.left_trigger = -1.0f;
    fresh.right_trigger = -1.0f;
    fresh.any_button = true;
    state = tracker.update(&fresh, 1);
    CHECK(state.throttle_digital);
    CHECK(state.throttle == 0.0f);
}

TEST_CASE("input shaping: keyboard and gamepad mix by magnitude per axis") {
    KeyboardState keys;
    PadState pad_state;
    pad_state.active = true;

    // Solo teclado: a fondo y con los bits digitales.
    keys.right = true;
    keys.forward = true;
    rally::sim::SimInput input = combine_inputs(keys, PadState{});
    CHECK(input.steer == kAxisMax);
    CHECK(input.throttle == kAxisMax);
    CHECK((input.buttons & kButtonSteerDigital) != 0);
    CHECK((input.buttons & kButtonThrottleDigital) != 0);

    // El stick a medias pierde contra la tecla; el gatillo analógico a fondo empata y gana.
    pad_state.steer = -0.5f;
    pad_state.throttle = -1.0f;
    input = combine_inputs(keys, pad_state);
    CHECK(input.steer == kAxisMax);
    CHECK((input.buttons & kButtonSteerDigital) != 0);
    CHECK(input.throttle == -kAxisMax);
    CHECK((input.buttons & kButtonThrottleDigital) == 0);

    // El stick a fondo gana (empate) y quita el bit de la dirección.
    pad_state.steer = -1.0f;
    input = combine_inputs(keys, pad_state);
    CHECK(input.steer == -kAxisMax);
    CHECK((input.buttons & kButtonSteerDigital) == 0);

    // Gatillo digital ganando: bit digital del acelerador.
    keys = KeyboardState{};
    pad_state.throttle_digital = true;
    pad_state.throttle = 1.0f;
    input = combine_inputs(keys, pad_state);
    CHECK(input.throttle == kAxisMax);
    CHECK((input.buttons & kButtonThrottleDigital) != 0);

    // Salto de cualquiera de los dos.
    CHECK((combine_inputs(KeyboardState{false, false, false, false, true}, PadState{}).buttons &
           kButtonJump) != 0);
    pad_state.jump = true;
    CHECK((combine_inputs(KeyboardState{}, pad_state).buttons & kButtonJump) != 0);
}

TEST_CASE("input shaping: rumble only above the impact threshold") {
    using rally::platform::rumble_for_impact;
    CHECK(rumble_for_impact(0.0f).duration_ms == 0);
    CHECK(rumble_for_impact(0.04f).duration_ms == 0); // apoyado: la gravedad de un paso
    CHECK(rumble_for_impact(rally::platform::kImpactThreshold).duration_ms == 0);
    const rally::platform::Rumble light = rumble_for_impact(1.5f);
    CHECK(light.duration_ms == rally::platform::kRumbleMs);
    CHECK(light.low >= rally::platform::kRumbleMinStrength);
    const rally::platform::Rumble hard = rumble_for_impact(50.0f);
    CHECK(hard.low == 1.0f);
    CHECK(hard.low > light.low);
}
