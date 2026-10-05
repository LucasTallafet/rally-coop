#include "platform/input.h"

#include "platform/input_shaping.h"
#include "platform/keys_raylib.h"

#include <raylib.h>

#include <cstdint>

namespace rally::platform {

namespace {

// Botones que se retienen en los fotogramas sin pasos: los que actúan al pulsar. Los bits
// digitales y el freno de mano describen el fotograma y no se retienen.
constexpr std::uint16_t kPressButtons =
    sim::kButtonShiftUp | sim::kButtonShiftDown | sim::kButtonReset;

} // namespace

sim::SimInput DriverInput::read(int step_count) {
    // Teclas de la tabla (platform/keys). Los botones que actúan al pulsar recogen también una
    // pulsación que empieza y acaba dentro del fotograma.
    KeyboardState keys;
    keys.left = action_down(Action::SteerLeft);
    keys.right = action_down(Action::SteerRight);
    keys.throttle = action_down(Action::Throttle);
    keys.brake = action_down(Action::Brake);
    keys.handbrake = action_down(Action::Handbrake);
    keys.shift_down = action_down_or_pressed(Action::ShiftDown);
    keys.shift_up = action_down_or_pressed(Action::ShiftUp);
    keys.reset = action_down_or_pressed(Action::Reset);

    update_mouse();
    sim::SimInput input =
        combine_inputs(keys, gamepads_.read(), MouseState{mouse_enabled_, mouse_.steer()});
    const std::uint16_t pressed = latch_.apply(input.buttons & kPressButtons, step_count);
    input.buttons = static_cast<std::uint16_t>((input.buttons & ~kPressButtons) | pressed);
    return input;
}

void DriverInput::set_mouse(bool enabled, int full_lock_px, bool capture_allowed) {
    mouse_enabled_ = enabled;
    mouse_full_lock_px_ = full_lock_px;
    mouse_capture_allowed_ = capture_allowed;
}

void DriverInput::update_mouse() {
    const bool want = mouse_enabled_ && mouse_capture_allowed_ && IsWindowFocused();
    if (want && !mouse_captured_) {
        DisableCursor();
        mouse_captured_ = true;
        skip_mouse_delta_ = true;
        mouse_.recenter();
    } else if (!want && mouse_captured_) {
        EnableCursor();
        mouse_captured_ = false;
        mouse_.recenter();
    }
    if (!mouse_captured_) {
        return;
    }
    const float dx = GetMouseDelta().x;
    if (skip_mouse_delta_) {
        skip_mouse_delta_ = false;
    } else {
        mouse_.move(dx, mouse_full_lock_px_);
    }
    if (action_pressed(Action::RecenterWheel, kContextAll) ||
        IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) {
        mouse_.recenter();
    }
}

} // namespace rally::platform
