#include "platform/gamepad.h"

#include "platform/keys.h"

#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_stdinc.h>

#include <algorithm>

namespace rally::platform {

namespace {

// Mapeo del conductor: el mando de cada acción en la tabla de platform/keys (sin menú de
// reasignación hasta los menús, fase 5).
SDL_GamepadAxis to_axis(PadControl control) {
    switch (control) {
    case PadControl::LeftStickX:
        return SDL_GAMEPAD_AXIS_LEFTX;
    case PadControl::RightTrigger:
        return SDL_GAMEPAD_AXIS_RIGHT_TRIGGER;
    case PadControl::LeftTrigger:
        return SDL_GAMEPAD_AXIS_LEFT_TRIGGER;
    default:
        return SDL_GAMEPAD_AXIS_INVALID;
    }
}

SDL_GamepadButton to_button(PadControl control) {
    switch (control) {
    case PadControl::FaceSouth:
        return SDL_GAMEPAD_BUTTON_SOUTH;
    case PadControl::FaceNorth:
        return SDL_GAMEPAD_BUTTON_NORTH;
    case PadControl::LeftShoulder:
        return SDL_GAMEPAD_BUTTON_LEFT_SHOULDER;
    case PadControl::RightShoulder:
        return SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER;
    default:
        return SDL_GAMEPAD_BUTTON_INVALID;
    }
}

struct PadBindings {
    SDL_GamepadAxis steer = to_axis(pad_for(Action::SteerLeft));
    SDL_GamepadAxis vertical = SDL_GAMEPAD_AXIS_LEFTY; // solo para no tomarlo por dirección
    SDL_GamepadAxis accelerate = to_axis(pad_for(Action::Throttle));
    SDL_GamepadAxis brake = to_axis(pad_for(Action::Brake));
    SDL_GamepadButton handbrake = to_button(pad_for(Action::Handbrake));
    SDL_GamepadButton reset = to_button(pad_for(Action::Reset));
    SDL_GamepadButton shift_down = to_button(pad_for(Action::ShiftDown));
    SDL_GamepadButton shift_up = to_button(pad_for(Action::ShiftUp));
};
const PadBindings kDriverPad{};

// SDL da false o 0 con un botón o un eje no válido; lo leído queda en reposo.
bool button(SDL_Gamepad* pad, SDL_GamepadButton b) {
    return b != SDL_GAMEPAD_BUTTON_INVALID && SDL_GetGamepadButton(pad, b);
}

// SDL da los sticks en [-32768, 32767] y los gatillos en [0, 32767].
float axis_value(SDL_Gamepad* pad, SDL_GamepadAxis axis) {
    if (axis == SDL_GAMEPAD_AXIS_INVALID) {
        return 0.0f;
    }
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
        reading.handbrake = button(pad, kDriverPad.handbrake);
        reading.reset = button(pad, kDriverPad.reset);
        reading.shift_down = button(pad, kDriverPad.shift_down);
        reading.shift_up = button(pad, kDriverPad.shift_up);
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
