#include "platform/note_panel.h"

namespace rally::platform {

namespace {

PanelSlot slot_of(const game::Note& note, bool struck) {
    PanelSlot slot;
    slot.kind = note.grade == game::NoteGrade::None ? PanelSlotKind::Hazard : PanelSlotKind::Corner;
    slot.note = note;
    slot.struck = struck;
    return slot;
}

} // namespace

void NotePanel::reset() { *this = NotePanel(); }

void NotePanel::update(const game::NoteQueue& queue, double car_s, std::int64_t now_ns) {
    const game::NotesView live = queue.view(car_s);

    // Las notas borradas desde el último fotograma, una vez cada una. Con cada reset() de la cola
    // el contador vuelve a 0: se empieza de nuevo sin sucesos.
    const std::uint32_t total = queue.erased_total();
    if (total < erased_seen_) {
        erased_seen_ = total;
    }
    if (total - erased_seen_ > static_cast<std::uint32_t>(game::kMaxNotes)) {
        erased_seen_ = total - static_cast<std::uint32_t>(game::kMaxNotes);
    }
    if (total != erased_seen_) {
        for (std::uint32_t i = erased_seen_; i < total; ++i) {
            const game::Note& erased = queue.erased(i);
            // Solo se tacha la que estaba a la vista, en su hueco.
            if (shown_left_ != 0 && erased.id == shown_left_) {
                strike_left_ = {true, erased, now_ns + kStrikeNs};
                shown_left_ = 0;
            } else if (shown_right_ != 0 && erased.id == shown_right_) {
                strike_right_ = {true, erased, now_ns + kStrikeNs};
                shown_right_ = 0;
            }
        }
        erased_seen_ = total;
        ++sounds_.erase_beeps; // un tono por fotograma, borre las que borre
    }
    if (strike_left_.active && now_ns >= strike_left_.until_ns) {
        strike_left_.active = false;
    }
    if (strike_right_.active && now_ns >= strike_right_.until_ns) {
        strike_right_.active = false;
    }

    // Qué se muestra. Mientras una nota está tachada, ocupa su hueco; si es la izquierda, la nota
    // viva siguiente espera en el derecho hasta que acabe la tachadura.
    PanelView out;
    if (strike_left_.active) {
        out.left = slot_of(strike_left_.note, true);
        if (!strike_right_.active && live.has_next) {
            out.right = slot_of(live.next, false);
        }
    } else {
        if (live.has_next) {
            out.left = slot_of(live.next, false);
            out.center = live.next.distance;
            out.has_center = live.next.distance.kind != game::DistanceKind::None;
        }
        if (!strike_right_.active && live.has_after) {
            out.right = slot_of(live.after, false);
        }
    }
    if (strike_right_.active) {
        out.right = slot_of(strike_right_.note, true);
    }

    // Pitido: al entrar una nota viva en el hueco izquierdo (cambia su id). Una nota que el coche
    // salta entre dos fotogramas nunca estuvo en el hueco y no suena.
    shown_left_ = 0;
    shown_right_ = 0;
    if (out.left.kind != PanelSlotKind::Empty && !out.left.struck) {
        shown_left_ = out.left.note.id;
        if (out.left.note.id != beeped_left_) {
            beeped_left_ = out.left.note.id;
            if (out.left.kind == PanelSlotKind::Hazard ||
                out.left.note.side == game::NoteSide::None) {
                ++sounds_.hazard_beeps;
            } else if (out.left.note.side == game::NoteSide::Left) {
                ++sounds_.beeps_left;
            } else {
                ++sounds_.beeps_right;
            }
        }
    } else if (out.left.kind == PanelSlotKind::Empty) {
        beeped_left_ = 0;
    }
    if (out.right.kind != PanelSlotKind::Empty && !out.right.struck) {
        shown_right_ = out.right.note.id;
    }
    view_ = out;
}

} // namespace rally::platform
