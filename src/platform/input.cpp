#include "platform/input.h"

#include <raylib.h>

#include <cstdint>

namespace rally::platform {

namespace {

bool down(KeyboardKey a, KeyboardKey b) { return IsKeyDown(a) || IsKeyDown(b); }

// Dos teclas opuestas: a fondo hacia un lado, o 0 si están las dos o ninguna.
std::int16_t keyboard_axis(bool negative, bool positive) {
    return static_cast<std::int16_t>((positive ? sim::kAxisMax : 0) -
                                     (negative ? sim::kAxisMax : 0));
}

} // namespace

sim::SimInput KeyboardInput::read(int step_count) {
    sim::SimInput input;
    input.throttle = keyboard_axis(down(KEY_S, KEY_DOWN), down(KEY_W, KEY_UP));
    input.steer = keyboard_axis(down(KEY_A, KEY_LEFT), down(KEY_D, KEY_RIGHT));
    std::uint16_t buttons = 0;
    // IsKeyPressed recoge también una pulsación que empieza y acaba dentro del fotograma.
    if (IsKeyDown(KEY_SPACE) || IsKeyPressed(KEY_SPACE)) {
        buttons |= sim::kButtonJump;
    }
    input.buttons = latch_.apply(buttons, step_count);
    return input;
}

} // namespace rally::platform
