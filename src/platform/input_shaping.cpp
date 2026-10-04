#include "platform/input_shaping.h"

#include <algorithm>
#include <cmath>

namespace rally::platform {

float shape_steer(float x) {
    const float magnitude = std::abs(x);
    if (magnitude <= kSteerInnerDeadzone) {
        return 0.0f;
    }
    const float t = std::min(1.0f, (magnitude - kSteerInnerDeadzone) /
                                       (kSteerOuterDeadzone - kSteerInnerDeadzone));
    const float shaped = std::pow(t, kSteerCurveExponent);
    return x < 0.0f ? -shaped : shaped;
}

float shape_trigger(float value) {
    const float clamped = std::clamp(value, 0.0f, 1.0f);
    if (clamped <= kTriggerDeadzone) {
        return 0.0f;
    }
    return (clamped - kTriggerDeadzone) / (1.0f - kTriggerDeadzone);
}

std::int16_t to_axis(float value) {
    const float clamped = std::clamp(value, -1.0f, 1.0f);
    return static_cast<std::int16_t>(std::lround(clamped * sim::kAxisMax));
}

namespace {

bool intermediate(float trigger) {
    return trigger > kTriggerAnalogLow && trigger < kTriggerAnalogHigh;
}

// Usado en este fotograma: eje de dirección o gatillo fuera de su zona muerta, o un botón.
// El eje Y del stick no cuenta: una deriva vertical no roba el mando activo.
bool in_use(const PadReading& pad) {
    return shape_steer(pad.left_x) != 0.0f || shape_trigger(pad.left_trigger) != 0.0f ||
           shape_trigger(pad.right_trigger) != 0.0f || pad.any_button;
}

} // namespace

PadState PadTracker::update(const PadReading* pads, int count) {
    count = std::min(count, kMaxPads);

    // Olvida los mandos desconectados y añade los nuevos, conservando lo aprendido.
    Known next[kMaxPads];
    for (int i = 0; i < count; ++i) {
        next[i].id = pads[i].id;
        if (const Known* known = find(pads[i].id)) {
            next[i] = *known;
        }
        if (intermediate(std::clamp(pads[i].left_trigger, 0.0f, 1.0f))) {
            next[i].analog_left = true;
        }
        if (intermediate(std::clamp(pads[i].right_trigger, 0.0f, 1.0f))) {
            next[i].analog_right = true;
        }
    }
    std::copy(next, next + count, known_);
    known_count_ = count;

    // Mando activo: si el actual sigue en uso, se queda; si no, el primero en uso; si nadie
    // se usa, el actual mientras siga conectado.
    int active_index = -1;
    int first_in_use = -1;
    bool active_in_use = false;
    for (int i = 0; i < count; ++i) {
        const bool used = in_use(pads[i]);
        if (pads[i].id == active_) {
            active_index = i;
            active_in_use = used;
        }
        if (used && first_in_use < 0) {
            first_in_use = i;
        }
    }
    if (!active_in_use && first_in_use >= 0) {
        active_index = first_in_use;
    }
    active_ = active_index >= 0 ? pads[active_index].id : 0;

    PadState state;
    if (active_index < 0) {
        return state;
    }
    const PadReading& pad = pads[active_index];
    state.active = true;
    state.steer = shape_steer(pad.left_x);
    state.throttle = shape_trigger(pad.right_trigger);
    state.brake = shape_trigger(pad.left_trigger);
    state.throttle_digital = !known_[active_index].analog_right;
    state.brake_digital = !known_[active_index].analog_left;
    state.handbrake = pad.handbrake;
    state.shift_up = pad.shift_up;
    state.shift_down = pad.shift_down;
    state.reset = pad.reset;
    return state;
}

const PadTracker::Known* PadTracker::find(std::uint32_t id) const {
    for (int i = 0; i < known_count_; ++i) {
        if (known_[i].id == id) {
            return &known_[i];
        }
    }
    return nullptr;
}

bool PadTracker::left_trigger_digital(std::uint32_t id) const {
    const Known* known = find(id);
    return known != nullptr && !known->analog_left;
}

bool PadTracker::right_trigger_digital(std::uint32_t id) const {
    const Known* known = find(id);
    return known != nullptr && !known->analog_right;
}

namespace {

// Dos teclas opuestas: a fondo hacia un lado, o 0 si están las dos o ninguna.
float keyboard_axis(bool negative, bool positive) {
    return (positive ? 1.0f : 0.0f) - (negative ? 1.0f : 0.0f);
}

} // namespace

sim::SimInput combine_inputs(const KeyboardState& keys, const PadState& pad) {
    const float key_steer = keyboard_axis(keys.left, keys.right);
    const float key_throttle = keys.throttle ? 1.0f : 0.0f;
    const float key_brake = keys.brake ? 1.0f : 0.0f;
    const bool keyboard_steers = std::abs(key_steer) > std::abs(pad.steer);
    const bool keyboard_throttles = key_throttle > pad.throttle;
    const bool keyboard_brakes = key_brake > pad.brake;

    sim::SimInput input;
    input.steer = to_axis(keyboard_steers ? key_steer : pad.steer);
    input.throttle = to_axis(keyboard_throttles ? key_throttle : pad.throttle);
    input.brake = to_axis(keyboard_brakes ? key_brake : pad.brake);
    std::uint16_t buttons = 0;
    const auto set = [&buttons](bool on, std::uint16_t bit) {
        if (on) {
            buttons |= bit;
        }
    };
    set(keys.handbrake || pad.handbrake, sim::kButtonHandbrake);
    set(keys.shift_up || pad.shift_up, sim::kButtonShiftUp);
    set(keys.shift_down || pad.shift_down, sim::kButtonShiftDown);
    set(keys.reset || pad.reset, sim::kButtonReset);
    set(keyboard_steers, sim::kButtonSteerDigital);
    set(keyboard_throttles || (pad.active && pad.throttle_digital), sim::kButtonThrottleDigital);
    set(keyboard_brakes || (pad.active && pad.brake_digital), sim::kButtonBrakeDigital);
    input.buttons = buttons;
    return input;
}

Rumble rumble_for_impact(float peak_impact) {
    Rumble rumble;
    if (!(peak_impact > kImpactThreshold)) {
        return rumble;
    }
    const float t =
        std::min(1.0f, (peak_impact - kImpactThreshold) / (kImpactFull - kImpactThreshold));
    const float strength = kRumbleMinStrength + (1.0f - kRumbleMinStrength) * t;
    rumble.low = strength;
    rumble.high = strength * 0.5f;
    rumble.duration_ms = kRumbleMs;
    return rumble;
}

} // namespace rally::platform
