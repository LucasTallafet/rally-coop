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
    KeyboardKey forward[2] = {KEY_W, KEY_UP};
    KeyboardKey back[2] = {KEY_S, KEY_DOWN};
    KeyboardKey jump = KEY_SPACE;
};
constexpr KeyBindings kDriverKeys{};

bool down(const KeyboardKey (&keys)[2]) { return IsKeyDown(keys[0]) || IsKeyDown(keys[1]); }

// Botones que se retienen en los fotogramas sin pasos: los que actúan al pulsar. Los bits
// digitales describen el fotograma y no se retienen.
constexpr std::uint16_t kPressButtons = sim::kButtonJump;

} // namespace

sim::SimInput DriverInput::read(int step_count) {
    KeyboardState keys;
    keys.left = down(kDriverKeys.left);
    keys.right = down(kDriverKeys.right);
    keys.forward = down(kDriverKeys.forward);
    keys.back = down(kDriverKeys.back);
    // IsKeyPressed recoge también una pulsación que empieza y acaba dentro del fotograma.
    keys.jump = IsKeyDown(kDriverKeys.jump) || IsKeyPressed(kDriverKeys.jump);

    sim::SimInput input = combine_inputs(keys, gamepads_.read());
    const std::uint16_t pressed = latch_.apply(input.buttons & kPressButtons, step_count);
    input.buttons = static_cast<std::uint16_t>((input.buttons & ~kPressButtons) | pressed);
    return input;
}

} // namespace rally::platform
