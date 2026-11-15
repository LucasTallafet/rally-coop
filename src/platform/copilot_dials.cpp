#include "platform/copilot_dials.h"

#include <algorithm>

namespace rally::platform {

namespace {

constexpr game::NoteMark kSecondaryMarks[7] = {
    game::NoteMark::Crest,    game::NoteMark::Jump,  game::NoteMark::Caution,
    game::NoteMark::Tightens, game::NoteMark::Opens, game::NoteMark::DontCut,
    game::NoteMark::Long,
};

const WheelSpec& spec_for(DialKind kind) {
    return kind == DialKind::Main ? kMainWheel : kRing8Wheel;
}

} // namespace

void DistanceCounter::update(int held, double dt_s) {
    changed_ = false;
    if (held == 0) {
        held_ = 0;
        hold_s_ = 0.0;
        repeat_s_ = 0.0;
        return;
    }
    const auto step = [&]() {
        const int next = std::clamp(metres_ + held * kCounterStepM, 0, kCounterMaxM);
        changed_ = changed_ || next != metres_;
        metres_ = next;
    };
    if (held != held_) {
        // Pulsación nueva (o cambio de sentido): cuenta al instante.
        held_ = held;
        hold_s_ = 0.0;
        repeat_s_ = 0.0;
        step();
        return;
    }
    hold_s_ += dt_s;
    if (hold_s_ < kCounterFirstRepeatS) {
        return;
    }
    // Tiempo de repetición de este fotograma: solo lo que pasa de la espera.
    const double repeating = std::min(dt_s, hold_s_ - kCounterFirstRepeatS);
    repeat_s_ += repeating;
    for (;;) {
        const double rate = hold_s_ - kCounterFirstRepeatS >= kCounterFastAfterS ? kCounterFastRate
                                                                                 : kCounterSlowRate;
        const double period = 1.0 / rate;
        if (repeat_s_ < period) {
            break;
        }
        repeat_s_ -= period;
        step();
    }
}

int DistanceCounter::sing() {
    const int metres = metres_;
    reset();
    return metres;
}

EraseEvent EraseButton::update(bool down, double dt_s) {
    if (down) {
        if (!down_) {
            down_ = true;
            fired_ = false;
            held_s_ = 0.0;
        } else {
            held_s_ += dt_s;
        }
        if (!fired_ && held_s_ >= kEraseHoldS) {
            fired_ = true;
            return EraseEvent::ClearPending;
        }
        return EraseEvent::None;
    }
    const bool was_down = down_;
    const bool fired = fired_;
    down_ = false;
    fired_ = false;
    held_s_ = 0.0;
    return was_down && !fired ? EraseEvent::EraseLast : EraseEvent::None;
}

game::NoteGrade main_dial_grade(int index) {
    if (index == kMainHairpinIndex) {
        return game::NoteGrade::Hairpin;
    }
    // 0 = «6» … 5 = «1».
    return static_cast<game::NoteGrade>(static_cast<int>(game::NoteGrade::G6) - index);
}

game::NoteMark secondary_mark(int index) {
    return kSecondaryMarks[static_cast<std::size_t>(std::clamp(index, 0, 6))];
}

DialsView CopilotDials::view() const {
    DialsView v;
    v.open = open_;
    v.side = side_;
    v.selected = selected_;
    v.form = form_;
    v.counter_m = counter_.metres();
    v.erase_progress = erase_.progress();
    return v;
}

void CopilotDials::confirm(DialsOutput& out, double anchor_s, bool anchor_valid,
                           bool attach_to_last) {
    const DialKind kind = open_;
    const int selected = selected_;
    if (kind == DialKind::Distance && form_ == DistanceForm::Counter) {
        out.add(game::distance_order(game::counter_distance(counter_.sing())));
    } else if (selected < 0) {
        // Cancela.
    } else if (kind == DialKind::Main) {
        if (anchor_valid) {
            out.add(game::corner_order(anchor_s, main_dial_grade(selected), side_));
        } else {
            out.no_anchor = true;
        }
    } else if (kind == DialKind::Secondary) {
        if (selected == kSecondaryLinkIndex) {
            out.add(game::distance_order(game::wheel_distance(0)));
        } else {
            const game::NoteMark mark = secondary_mark(selected);
            if (!game::is_hazard(mark) || attach_to_last) {
                out.add(game::mark_order(mark));
            } else if (anchor_valid) {
                out.add(game::hazard_order(anchor_s, mark));
            } else {
                out.no_anchor = true;
            }
        }
    } else if (kind == DialKind::Distance) {
        out.add(game::distance_order(game::wheel_distance(selected)));
    }
}

void CopilotDials::cancel() {
    open_ = DialKind::None;
    side_ = game::NoteSide::None;
    selected_ = -1;
    counter_.reset();
}

DialsOutput CopilotDials::update(const DialsInput& in, double dt_s, double anchor_s,
                                 bool anchor_valid, bool attach_to_last) {
    DialsOutput out;
    if (in.toggle_form) {
        form_ = form_ == DistanceForm::Wheel ? DistanceForm::Counter : DistanceForm::Wheel;
        if (open_ != DialKind::Distance) {
            counter_.reset();
        }
    }
    // Qué control está mantenido: el abierto sigue abierto hasta que se suelta; si no hay ninguno,
    // se abre por prioridad distancia > secundario > principal.
    const bool main_held = in.main_left || in.main_right;
    const auto held = [&](DialKind kind) {
        switch (kind) {
        case DialKind::Main:
            return main_held;
        case DialKind::Secondary:
            return in.secondary;
        case DialKind::Distance:
            return in.distance;
        case DialKind::None:
            break;
        }
        return false;
    };
    if (open_ != DialKind::None && !held(open_)) {
        confirm(out, anchor_s, anchor_valid, attach_to_last);
        open_ = DialKind::None;
        side_ = game::NoteSide::None;
        selected_ = -1;
    }
    if (open_ == DialKind::None) {
        if (in.distance) {
            open_ = DialKind::Distance;
        } else if (in.secondary) {
            open_ = DialKind::Secondary;
        } else if (main_held) {
            open_ = DialKind::Main;
        }
        selected_ = -1;
        counter_stepped_ = false;
        if (open_ == DialKind::Main) {
            side_ = in.main_right && !in.main_left ? game::NoteSide::Right : game::NoteSide::Left;
        }
    }
    // El lado del principal puede cambiar mientras está abierto (teclado: ← y →).
    if (open_ == DialKind::Main) {
        if (in.main_right && !in.main_left) {
            side_ = game::NoteSide::Right;
        } else if (in.main_left && !in.main_right) {
            side_ = game::NoteSide::Left;
        }
    }
    // La elección.
    if (open_ != DialKind::None &&
        !(open_ == DialKind::Distance && form_ == DistanceForm::Counter)) {
        const WheelSpec& spec = spec_for(open_);
        if (in.direct >= 0 && in.direct < spec.count) {
            selected_ = in.direct;
        } else if (in.analog) {
            selected_ = wheel_pick(spec, in.x, in.y, selected_);
        }
    }
    // El contador: con los botones (ratón) siempre; con el mando o el teclado, con el dial abierto.
    if (form_ == DistanceForm::Counter &&
        (open_ == DialKind::None || open_ == DialKind::Distance)) {
        counter_.update(in.step_held, dt_s);
        if (in.sing_pressed) {
            out.add(game::distance_order(game::counter_distance(counter_.sing())));
        }
    }
    // Borrar.
    switch (erase_.update(in.erase_down, dt_s)) {
    case EraseEvent::EraseLast:
        out.add(game::erase_last_order());
        break;
    case EraseEvent::ClearPending:
        out.add(game::clear_pending_order());
        break;
    case EraseEvent::None:
        break;
    }
    return out;
}

} // namespace rally::platform
