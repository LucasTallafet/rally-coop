#include "platform/input_shaping.h"
#include "sim/sim_input.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <bit>
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
using rally::sim::kButtonBrakeDigital;
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

TEST_CASE("input shaping: each trigger is digital until it gives an intermediate value") {
    PadTracker tracker;
    PadReading switch_pro = pad(5);
    switch_pro.right_trigger = 1.0f;
    PadState state = tracker.update(&switch_pro, 1);
    CHECK(state.throttle_digital);
    CHECK(state.brake_digital);
    switch_pro.right_trigger = 0.0f;
    switch_pro.left_trigger = 1.0f;
    state = tracker.update(&switch_pro, 1);
    CHECK(state.brake_digital);
    CHECK(state.brake == 1.0f);
    CHECK(state.throttle == 0.0f);
    CHECK(tracker.left_trigger_digital(5));
    CHECK(tracker.right_trigger_digital(5));

    // Gatillos de 0 o 1: el freno lleva su bit digital.
    CHECK((combine_inputs(KeyboardState{}, state).buttons & kButtonBrakeDigital) != 0);

    // Un valor intermedio en el izquierdo: el freno pasa a analógico y se queda así aunque
    // vuelva a 0 o a 1; el acelerador sigue digital (se aprende por gatillo).
    switch_pro.left_trigger = 0.4f;
    state = tracker.update(&switch_pro, 1);
    CHECK_FALSE(state.brake_digital);
    CHECK(state.throttle_digital);
    switch_pro.left_trigger = 1.0f;
    state = tracker.update(&switch_pro, 1);
    CHECK_FALSE(state.brake_digital);
    CHECK((combine_inputs(KeyboardState{}, state).buttons & kButtonBrakeDigital) == 0);

    // Al desconectar se olvida: al volver, digital hasta otro valor intermedio.
    tracker.update(nullptr, 0);
    CHECK(tracker.update(&switch_pro, 1).brake_digital);

    // raylib da -1 antes del primer movimiento: no cuenta como intermedio.
    PadReading fresh = pad(6);
    fresh.left_trigger = -1.0f;
    fresh.right_trigger = -1.0f;
    fresh.any_button = true;
    state = tracker.update(&fresh, 1);
    CHECK(state.throttle_digital);
    CHECK(state.brake_digital);
    CHECK(state.throttle == 0.0f);
    CHECK(state.brake == 0.0f);
}

TEST_CASE("input shaping: throttle and brake are independent axes") {
    PadTracker tracker;
    PadReading both = pad(3);
    both.right_trigger = 0.8f;
    both.left_trigger = 0.6f;
    const PadState state = tracker.update(&both, 1);
    REQUIRE(state.active);
    const rally::sim::SimInput input = combine_inputs(KeyboardState{}, state);
    // Los dos gatillos a la vez dan los dos ejes (frenada con el pie izquierdo).
    CHECK(input.throttle == to_axis(shape_trigger(0.8f)));
    CHECK(input.brake == to_axis(shape_trigger(0.6f)));
    CHECK(input.throttle > 0);
    CHECK(input.brake > 0);

    // Con el teclado, W y S a la vez también.
    KeyboardState keys;
    keys.throttle = true;
    keys.brake = true;
    const rally::sim::SimInput key_input = combine_inputs(keys, PadState{});
    CHECK(key_input.throttle == kAxisMax);
    CHECK(key_input.brake == kAxisMax);
    CHECK((key_input.buttons & kButtonThrottleDigital) != 0);
    CHECK((key_input.buttons & kButtonBrakeDigital) != 0);
}

TEST_CASE("input shaping: drift neither brakes nor throttles") {
    PadTracker tracker;
    PadReading worn = pad(4);
    worn.left_trigger = 0.04f; // gatillo que no vuelve del todo, dentro de la zona muerta
    worn.right_trigger = 0.03f;
    worn.left_y = 1.0f; // stick que deriva en vertical
    worn.any_button = true;
    const PadState state = tracker.update(&worn, 1);
    REQUIRE(state.active);
    const rally::sim::SimInput input = combine_inputs(KeyboardState{}, state);
    CHECK(input.brake == 0);
    CHECK(input.throttle == 0);
    CHECK(input.steer == 0);
}

TEST_CASE("input shaping: keyboard and gamepad mix by magnitude per axis") {
    KeyboardState keys;
    PadState pad_state;
    pad_state.active = true;

    // Solo teclado: a fondo y con los bits digitales.
    keys.right = true;
    keys.throttle = true;
    rally::sim::SimInput input = combine_inputs(keys, PadState{});
    CHECK(input.steer == kAxisMax);
    CHECK(input.throttle == kAxisMax);
    CHECK(input.brake == 0);
    CHECK((input.buttons & kButtonSteerDigital) != 0);
    CHECK((input.buttons & kButtonThrottleDigital) != 0);
    CHECK((input.buttons & kButtonBrakeDigital) == 0);

    // El stick a medias pierde contra la tecla; el gatillo analógico a fondo empata y gana.
    // El freno del mando no compite con el acelerador del teclado: es otro eje.
    pad_state.steer = -0.5f;
    pad_state.throttle = 1.0f;
    pad_state.brake = 0.5f;
    input = combine_inputs(keys, pad_state);
    CHECK(input.steer == kAxisMax);
    CHECK((input.buttons & kButtonSteerDigital) != 0);
    CHECK(input.throttle == kAxisMax);
    CHECK((input.buttons & kButtonThrottleDigital) == 0);
    CHECK(input.brake == to_axis(0.5f));
    CHECK((input.buttons & kButtonBrakeDigital) == 0);

    // S a fondo gana al freno del mando a medias y pone el bit del freno.
    keys.brake = true;
    input = combine_inputs(keys, pad_state);
    CHECK(input.brake == kAxisMax);
    CHECK((input.buttons & kButtonBrakeDigital) != 0);

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
}

TEST_CASE("input shaping: buttons come from either the keyboard or the gamepad") {
    struct Case {
        bool KeyboardState::* key;
        bool PadState::* pad;
        std::uint16_t bit;
    };
    const Case cases[] = {
        {&KeyboardState::handbrake, &PadState::handbrake, rally::sim::kButtonHandbrake},
        {&KeyboardState::shift_up, &PadState::shift_up, rally::sim::kButtonShiftUp},
        {&KeyboardState::shift_down, &PadState::shift_down, rally::sim::kButtonShiftDown},
        {&KeyboardState::reset, &PadState::reset, rally::sim::kButtonReset},
    };
    for (const Case& c : cases) {
        CAPTURE(c.bit);
        KeyboardState keys;
        keys.*c.key = true;
        CHECK(combine_inputs(keys, PadState{}).buttons == c.bit);
        PadState pad_state;
        pad_state.active = true;
        pad_state.*c.pad = true;
        CHECK(combine_inputs(KeyboardState{}, pad_state).buttons == c.bit);
    }
    // Los botones del mando llegan desde la lectura cruda.
    PadTracker tracker;
    PadReading reading = pad(9);
    reading.handbrake = true;
    reading.reset = true;
    reading.any_button = true;
    const PadState state = tracker.update(&reading, 1);
    CHECK(state.handbrake);
    CHECK(state.reset);
    CHECK_FALSE(state.shift_up);
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

namespace {

// Copia fija de shape_steer tal como era antes de la sensibilidad (tarea 2.8): exponente 1,5.
float steer_before_sensitivity(float x) {
    const float magnitude = std::abs(x);
    if (magnitude <= rally::platform::kSteerInnerDeadzone) {
        return 0.0f;
    }
    const float t = std::min(
        1.0f, (magnitude - rally::platform::kSteerInnerDeadzone) /
                  (rally::platform::kSteerOuterDeadzone - rally::platform::kSteerInnerDeadzone));
    const float shaped = std::pow(t, 1.5f);
    return x < 0.0f ? -shaped : shaped;
}

// Stick en la mitad del recorrido entre las zonas muertas: t = 0,5.
constexpr float kHalfStick = 0.525f;

} // namespace

TEST_CASE("steer sensitivity: at 100 percent the output is the same bit for bit as before") {
    for (int i = 0; i <= 1000; ++i) {
        const float x = -1.0f + 2.0f * static_cast<float>(i) / 1000.0f;
        INFO(x);
        CHECK(std::bit_cast<std::uint32_t>(shape_steer(x, 100)) ==
              std::bit_cast<std::uint32_t>(steer_before_sensitivity(x)));
        CHECK(std::bit_cast<std::uint32_t>(shape_steer(x)) ==
              std::bit_cast<std::uint32_t>(steer_before_sensitivity(x)));
    }
}

TEST_CASE("steer sensitivity: 50 and 150 percent give exponents 3 and 1, full lock is full") {
    using rally::platform::steer_curve_exponent;
    CHECK(steer_curve_exponent(50) == 3.0f);
    CHECK(steer_curve_exponent(100) == 1.5f);
    CHECK(steer_curve_exponent(150) == 1.0f);
    // Recorta fuera de 50..150.
    CHECK(steer_curve_exponent(10) == 3.0f);
    CHECK(steer_curve_exponent(500) == 1.0f);
    for (const int pct : {50, 100, 150}) {
        INFO(pct);
        CHECK(shape_steer(1.0f, pct) == 1.0f);
        CHECK(shape_steer(-1.0f, pct) == -1.0f);
        CHECK(shape_steer(0.95f, pct) == 1.0f);
        float previous = 0.0f;
        for (int i = 0; i <= 200; ++i) {
            const float value = shape_steer(static_cast<float>(i) / 200.0f, pct);
            CHECK(value >= previous);
            previous = value;
        }
    }
}

TEST_CASE("steer sensitivity: at 70 percent half the stick gives 0.5^(1.5/0.7)") {
    const double expected = std::pow(0.5, 1.5 / 0.7); // 0,2264
    CHECK(shape_steer(kHalfStick, 70) == doctest::Approx(expected).epsilon(1e-5));
    CHECK(shape_steer(-kHalfStick, 70) == doctest::Approx(-expected).epsilon(1e-5));
}

TEST_CASE("steer sensitivity: every step is felt, at half stick 0.125 against 0.5") {
    CHECK(shape_steer(kHalfStick, 50) == doctest::Approx(0.125).epsilon(1e-5));
    CHECK(shape_steer(kHalfStick, 150) == doctest::Approx(0.5).epsilon(1e-5));
    float previous = 0.0f;
    for (int pct = 50; pct <= 150; pct += 10) {
        const float value = shape_steer(kHalfStick, pct);
        INFO(pct);
        CHECK(value > previous); // cada paso de 10 % sube la salida a medio stick
        previous = value;
    }
}

TEST_CASE("steer sensitivity: the gamepad state carries it, the deadzone and trigger do not") {
    PadTracker tracker;
    PadReading reading;
    reading.id = 1;
    reading.left_x = kHalfStick;
    reading.right_trigger = 1.0f;
    const PadState soft = tracker.update(&reading, 1, 50);
    const PadState normal = tracker.update(&reading, 1, 100);
    const PadState sharp = tracker.update(&reading, 1, 150);
    CHECK(soft.steer == doctest::Approx(0.125).epsilon(1e-5));
    CHECK(normal.steer == shape_steer(kHalfStick));
    CHECK(sharp.steer == doctest::Approx(0.5).epsilon(1e-5));
    CHECK(soft.throttle == normal.throttle);
    // Dentro de la zona muerta, 0 con cualquier sensibilidad, y no activa el mando.
    PadTracker idle;
    PadReading drift;
    drift.id = 2;
    drift.left_x = 0.09f;
    CHECK_FALSE(idle.update(&drift, 1, 50).active);
    drift.any_button = true;
    CHECK(idle.update(&drift, 1, 150).steer == 0.0f);
}
