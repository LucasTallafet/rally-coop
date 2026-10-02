#include "platform/gamepad.h"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>

namespace rally::platform {

namespace {

// Mapeo del conductor (sin menú de reasignación hasta los menús, fase 5).
struct PadBindings {
    SDL_GamepadAxis steer = SDL_GAMEPAD_AXIS_LEFTX;
    SDL_GamepadAxis vertical = SDL_GAMEPAD_AXIS_LEFTY; // solo para no tomarlo por dirección
    SDL_GamepadAxis accelerate = SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
    SDL_GamepadAxis brake = SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
    SDL_GamepadButton handbrake = SDL_GAMEPAD_BUTTON_SOUTH;
    SDL_GamepadButton reset = SDL_GAMEPAD_BUTTON_NORTH;
    SDL_GamepadButton shift_down = SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
    SDL_GamepadButton shift_up = SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
};
constexpr PadBindings kDriverPad{};

// SDL da los sticks en [-32768, 32767] y los gatillos en [0, 32767].
float axis_value(SDL_Gamepad* pad, SDL_GamepadAxis axis) {
    return std::max(-1.0f, static_cast<float>(SDL_GetGamepadAxis(pad, axis)) / 32767.0f);
}

SDL_Gamepad* open_pad(std::uint32_t id) {
    return id != 0 ? SDL_GetGamepadFromID(static_cast<SDL_JoystickID>(id)) : nullptr;
}

} // namespace

PadState Gamepads::read() {
    PadReading readings[PadTracker::kMaxPads];
    int count = 0;
    int listed = 0;
    SDL_JoystickID* ids = SDL_GetGamepads(&listed);
    for (int i = 0; ids != nullptr && i < listed && count < PadTracker::kMaxPads; ++i) {
        // Solo los que raylib ha abierto: los demás no dan estado.
        SDL_Gamepad* pad = SDL_GetGamepadFromID(ids[i]);
        if (pad == nullptr) {
            continue;
        }
        PadReading& reading = readings[count++];
        reading.id = ids[i];
        reading.left_x = axis_value(pad, kDriverPad.steer);
        reading.left_y = axis_value(pad, kDriverPad.vertical);
        reading.left_trigger = axis_value(pad, kDriverPad.brake);
        reading.right_trigger = axis_value(pad, kDriverPad.accelerate);
        reading.handbrake = SDL_GetGamepadButton(pad, kDriverPad.handbrake);
        reading.reset = SDL_GetGamepadButton(pad, kDriverPad.reset);
        reading.shift_down = SDL_GetGamepadButton(pad, kDriverPad.shift_down);
        reading.shift_up = SDL_GetGamepadButton(pad, kDriverPad.shift_up);
        for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT && !reading.any_button; ++b) {
            reading.any_button = SDL_GetGamepadButton(pad, static_cast<SDL_GamepadButton>(b));
        }
    }
    SDL_free(ids);
    connected_ = count;
    return tracker_.update(readings, count);
}

void Gamepads::rumble(const Rumble& rumble) {
    SDL_Gamepad* pad = open_pad(tracker_.active_id());
    if (pad == nullptr || rumble.duration_ms <= 0) {
        return;
    }
    const auto motor = [](float value) {
        return static_cast<Uint16>(std::clamp(value, 0.0f, 1.0f) * 65535.0f);
    };
    SDL_RumbleGamepad(pad, motor(rumble.low), motor(rumble.high),
                      static_cast<Uint32>(rumble.duration_ms));
}

const char* Gamepads::active_name() const {
    SDL_Gamepad* pad = open_pad(tracker_.active_id());
    if (pad == nullptr) {
        return nullptr;
    }
    const char* name = SDL_GetGamepadName(pad);
    return name != nullptr ? name : "sin nombre";
}

const char* Gamepads::active_type() const {
    SDL_Gamepad* pad = open_pad(tracker_.active_id());
    if (pad == nullptr) {
        return nullptr;
    }
    const char* type = SDL_GetGamepadStringForType(SDL_GetGamepadType(pad));
    return type != nullptr ? type : "desconocido";
}

} // namespace rally::platform
