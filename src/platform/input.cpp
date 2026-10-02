#include "platform/input.h"

#include "platform/input_shaping.h"

#include <raylib.h>

#include <cstdint>

namespace rally::platform {

namespace {

// Mapeo del teclado (sin menú de reasignación hasta los menús, fase 5): dos teclas por acción.
struct KeyBindings {
    KeyboardKey left[2] = {KEY_A, KEY_LEFT};
    KeyboardKey right[2] = {KEY_D, KEY_RIGHT};
    KeyboardKey throttle[2] = {KEY_W, KEY_UP};
    KeyboardKey brake[2] = {KEY_S, KEY_DOWN};
    KeyboardKey handbrake = KEY_SPACE;
    KeyboardKey shift_down = KEY_Q;
    KeyboardKey shift_up = KEY_E;
    KeyboardKey reset = KEY_R;
    KeyboardKey jump = KEY_X; // temporal: la caja hasta la 2.4
};
constexpr KeyBindings kDriverKeys{};

bool down(const KeyboardKey (&keys)[2]) { return IsKeyDown(keys[0]) || IsKeyDown(keys[1]); }

// Botones que se retienen en los fotogramas sin pasos: los que actúan al pulsar. Los bits
// digitales y el freno de mano describen el fotograma y no se retienen.
constexpr std::uint16_t kPressButtons =
    sim::kButtonShiftUp | sim::kButtonShiftDown | sim::kButtonReset | sim::kButtonJump;

// IsKeyPressed recoge también una pulsación que empieza y acaba dentro del fotograma.
bool down_or_pressed(KeyboardKey key) { return IsKeyDown(key) || IsKeyPressed(key); }

} // namespace

sim::SimInput DriverInput::read(int step_count) {
    KeyboardState keys;
    keys.left = down(kDriverKeys.left);
    keys.right = down(kDriverKeys.right);
    keys.throttle = down(kDriverKeys.throttle);
    keys.brake = down(kDriverKeys.brake);
    keys.handbrake = IsKeyDown(kDriverKeys.handbrake);
    keys.shift_down = down_or_pressed(kDriverKeys.shift_down);
    keys.shift_up = down_or_pressed(kDriverKeys.shift_up);
    keys.reset = down_or_pressed(kDriverKeys.reset);
    keys.jump = down_or_pressed(kDriverKeys.jump);

    sim::SimInput input = combine_inputs(keys, gamepads_.read());
    const std::uint16_t pressed = latch_.apply(input.buttons & kPressButtons, step_count);
    input.buttons = static_cast<std::uint16_t>((input.buttons & ~kPressButtons) | pressed);
    return input;
}

} // namespace rally::platform
