#include "platform/copilot_dial_input.h"

#include <cmath>

namespace rally::platform {

DialsInput PadDialMapper::map(const PadState& pad, DistanceForm form) {
    DialsInput in;
    if (!pad.active) {
        previous_back_ = false;
        secondary_ = distance_ = left_first_ = right_first_ = false;
        return in;
    }
    // R1 y L1 a la vez: manda el primero que se pulsó; el otro no hace nada hasta soltar.
    if (!pad.shoulder_left) {
        left_first_ = false;
    }
    if (!pad.shoulder_right) {
        right_first_ = false;
    }
    if (pad.shoulder_left && pad.shoulder_right && !left_first_ && !right_first_) {
        left_first_ = true; // los dos en el mismo fotograma: la izquierda
    } else if (pad.shoulder_left && !pad.shoulder_right) {
        left_first_ = true;
    } else if (pad.shoulder_right && !pad.shoulder_left) {
        right_first_ = true;
    }
    in.main_left = pad.shoulder_left && (!pad.shoulder_right || left_first_);
    in.main_right = pad.shoulder_right && (!pad.shoulder_left || (right_first_ && !left_first_));
    secondary_ = pad.throttle >= (secondary_ ? kDialTriggerClose : kDialTriggerOpen);
    distance_ = pad.brake >= (distance_ ? kDialTriggerClose : kDialTriggerOpen);
    in.secondary = secondary_;
    in.distance = distance_;
    in.analog = true;
    in.x = pad.pick_x;
    in.y = pad.pick_y;
    if (form == DistanceForm::Counter) {
        in.step_held = (pad.dpad_right ? 1 : 0) - (pad.dpad_left ? 1 : 0);
    }
    in.erase_down = pad.face_west;
    in.toggle_form = pad.back && !previous_back_;
    previous_back_ = pad.back;
    return in;
}

DialsInput dials_from_keys(const KeyDialReading& keys, DistanceForm form) {
    DialsInput in;
    in.main_left = keys.left && !keys.right;
    in.main_right = keys.right && !keys.left;
    in.secondary = keys.secondary;
    in.distance = keys.distance;
    in.erase_down = keys.erase_down;
    in.toggle_form = keys.form_pressed;
    // Con D mantenida y el contador, las flechas son [−] y [+]; no abren el principal (la lógica
    // da prioridad a la distancia, pero así tampoco cambian el lado).
    if (keys.distance && form == DistanceForm::Counter) {
        in.step_held = (keys.right ? 1 : 0) - (keys.left ? 1 : 0);
    }
    for (int i = 0; i < 8; ++i) {
        if (!keys.number_pressed[static_cast<std::size_t>(i)]) {
            continue;
        }
        if (keys.distance || keys.secondary) {
            in.direct = i; // 1..8 = posición 0..7
        } else if (i < 6) {
            in.direct = 5 - i; // 1 → «1» (posición 5) … 6 → «6» (posición 0)
        } else if (i == 6) {
            in.direct = kMainHairpinIndex; // el 7 es la horquilla (el 8 no hace nada)
        }
    }
    return in;
}

void KeyDialFilter::step(Arrow& a, bool down, bool other) {
    if (!down) {
        a = Arrow{};
    } else if (!a.taken) {
        if (other && !a.active) {
            a.taken = true;
        } else if (!other) {
            a.active = true;
        }
    }
}

KeyDialReading KeyDialFilter::apply(const KeyDialReading& keys, bool other_open) {
    const bool other = keys.secondary || keys.distance || other_open;
    step(left_, keys.left, other);
    step(right_, keys.right, other);
    KeyDialReading out = keys;
    if (!keys.secondary && !keys.distance) {
        out.left = keys.left && !left_.taken;
        out.right = keys.right && !right_.taken;
    }
    return out;
}

void KeyDialFilter::block(const KeyDialReading& keys) {
    for (Arrow* a : {&left_, &right_}) {
        *a = Arrow{};
    }
    left_.taken = keys.left;
    right_.taken = keys.right;
}

int CursorHoldGate::update(bool up, bool down, bool dial_open) {
    const int raw = up == down ? 0 : (up ? 1 : -1);
    if (dial_open) {
        taken_ = raw;
        return 0;
    }
    if (raw != taken_) {
        taken_ = 0;
    }
    return taken_ != 0 ? 0 : raw;
}

int next_help_page(int page, int pages, bool pressed, bool dial_open) {
    if (!pressed || dial_open) {
        return page;
    }
    return page + 1 < pages ? page + 1 : -1;
}

DialsInput MouseDialMapper::map(const MouseDialReading& mouse, const DialsGeometry& g,
                                DistanceForm form) {
    DialsInput in;
    if (!mouse.enabled) {
        captured_ = DialKind::None;
        step_ = 0;
        erase_ = false;
        return in;
    }
    // El botón que sostiene lo capturado: al soltarlo, se acaba.
    const bool held = captured_right_button_ ? mouse.right_down : mouse.left_down;
    if (captured_ != DialKind::None && !held) {
        captured_ = DialKind::None;
    }
    if (captured_ == DialKind::None) {
        if (mouse.left_pressed && inside(g.main, mouse.x, mouse.y)) {
            captured_ = DialKind::Main;
            captured_right_button_ = false;
            side_ = mouse.x < g.main.cx ? game::NoteSide::Left : game::NoteSide::Right;
        } else if (mouse.left_pressed && inside(g.secondary, mouse.x, mouse.y)) {
            captured_ = DialKind::Secondary;
            captured_right_button_ = false;
        } else if (mouse.right_pressed && form == DistanceForm::Wheel &&
                   inside(g.distance, mouse.x, mouse.y)) {
            captured_ = DialKind::Distance;
            captured_right_button_ = true;
        }
    }
    const DialCircle* circle = nullptr;
    switch (captured_) {
    case DialKind::Main:
        in.main_left = side_ == game::NoteSide::Left;
        in.main_right = side_ == game::NoteSide::Right;
        circle = &g.main;
        break;
    case DialKind::Secondary:
        in.secondary = true;
        circle = &g.secondary;
        break;
    case DialKind::Distance:
        in.distance = true;
        circle = &g.distance;
        break;
    case DialKind::None:
        break;
    }
    if (circle != nullptr) {
        in.analog = true;
        dial_vector(*circle, mouse.x, mouse.y, in.x, in.y);
    }
    // Botones: lo pulsado es de quien se pulsó, no de dónde esté el puntero después.
    if (!mouse.left_down) {
        step_ = 0;
        erase_ = false;
    } else if (mouse.left_pressed && captured_ == DialKind::None) {
        if (form == DistanceForm::Counter) {
            if (inside(g.counter.minus, mouse.x, mouse.y)) {
                step_ = -1;
            } else if (inside(g.counter.plus, mouse.x, mouse.y)) {
                step_ = 1;
            } else if (inside(g.counter.sing, mouse.x, mouse.y)) {
                in.sing_pressed = true;
            }
        }
        if (inside(g.erase, mouse.x, mouse.y)) {
            erase_ = true;
        }
        if (inside(g.form_button, mouse.x, mouse.y)) {
            in.toggle_form = true;
        }
    }
    in.step_held = form == DistanceForm::Counter ? step_ : 0;
    in.erase_down = erase_;
    return in;
}

} // namespace rally::platform

namespace rally::platform {

DialsInput merge_dials(const DialsInput& a, const DialsInput& b) {
    DialsInput o;
    o.main_left = a.main_left || b.main_left;
    o.main_right = a.main_right || b.main_right;
    o.secondary = a.secondary || b.secondary;
    o.distance = a.distance || b.distance;
    const float ma = a.x * a.x + a.y * a.y;
    const float mb = b.x * b.x + b.y * b.y;
    const bool use_a = a.analog && (!b.analog || ma >= mb);
    const DialsInput& pick = use_a ? a : b;
    o.analog = a.analog || b.analog;
    o.x = pick.x;
    o.y = pick.y;
    o.direct = a.direct >= 0 ? a.direct : b.direct;
    o.step_held = a.step_held != 0 ? a.step_held : b.step_held;
    o.sing_pressed = a.sing_pressed || b.sing_pressed;
    o.erase_down = a.erase_down || b.erase_down;
    o.toggle_form = a.toggle_form || b.toggle_form;
    return o;
}

} // namespace rally::platform
