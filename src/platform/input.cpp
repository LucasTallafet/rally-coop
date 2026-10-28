#include "platform/input.h"

#include "platform/drive_keys.h"
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
    keys.left = action_down(action(DriveAction::SteerLeft), kContextAll);
    keys.right = action_down(action(DriveAction::SteerRight), kContextAll);
    keys.throttle = action_down(action(DriveAction::Throttle), kContextAll);
    keys.brake = action_down(action(DriveAction::Brake), kContextAll);
    keys.handbrake = action_down(action(DriveAction::Handbrake), kContextAll);
    keys.shift_down = action_down_or_pressed(action(DriveAction::ShiftDown), kContextAll);
    keys.shift_up = action_down_or_pressed(action(DriveAction::ShiftUp), kContextAll);
    keys.reset = action_down_or_pressed(action(DriveAction::Reset), kContextAll);

    const PadState pad = gamepads_.read(steer_sensitivity_pct_);
    update_mouse();
    // Mirar solo con el foco; conducir no cambia.
    look_.stick_x = IsWindowFocused() ? pad.look.x : 0.0f;
    look_.stick_y = IsWindowFocused() ? pad.look.y : 0.0f;
    sim::SimInput input = combine_inputs(keys, pad, MouseState{mouse_enabled_, mouse_.steer()});
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
    const MouseUse use = mouse_use(mouse_enabled_, mouse_capture_allowed_, IsWindowFocused());
    cursor_.update(use != MouseUse::None);
    if (cursor_.changed()) {
        mouse_.recenter(); // al capturar y al soltar, el volante al centro
    }
    look_.mouse_dx_px = 0.0f;
    look_.mouse_dy_px = 0.0f;
    look_.recenter = false;
    if (use == MouseUse::None) {
        return;
    }
    // F7 de teclado a ratón: la cabeza que dejó el ratón vuelve al frente (el ratón ya no mira y
    // no habría cómo recentrarla).
    if (look_recenters_on_switch(last_use_, use)) {
        look_.recenter = true;
    }
    last_use_ = use;
    float dx = 0.0f;
    float dy = 0.0f;
    const bool moved = cursor_.take_delta(dx, dy);
    if (use == MouseUse::Steer) {
        if (moved) {
            mouse_.move(dx, mouse_full_lock_px_);
        }
        if (action_pressed(action(DriveAction::RecenterWheel), kContextAll) ||
            IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) {
            mouse_.recenter();
        }
    } else {
        if (moved) {
            look_.mouse_dx_px = dx;
            look_.mouse_dy_px = dy;
        }
        if (IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE)) {
            look_.recenter = true;
        }
    }
}

} // namespace rally::platform
