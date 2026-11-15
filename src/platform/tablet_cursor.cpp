#include "platform/tablet_cursor.h"

#include <algorithm>

namespace rally::platform {

double TabletCursor::slide_speed(double slid_s) {
    const double k = std::clamp(slid_s / kCursorRampS, 0.0, 1.0);
    return kCursorSlideMin + (kCursorSlideMax - kCursorSlideMin) * k;
}

namespace {

bool has_note(const net::NotesState& notes, const game::DirectionChange& change) {
    for (int i = 0; i < notes.count; ++i) {
        const double a = notes.anchors[static_cast<std::size_t>(i)];
        if (a >= change.s_start - kNoteSlackBefore && a <= change.s_end) {
            return true;
        }
    }
    return false;
}

} // namespace

bool TabletCursor::default_s(const std::vector<game::DirectionChange>& changes, double s_car,
                             double& out) const {
    const int first = game::next_direction_change(changes, s_car);
    if (first < 0) {
        return false;
    }
    if (notes_ == nullptr && !skip_) {
        out = changes[static_cast<std::size_t>(first)].s_start;
        return true;
    }
    for (std::size_t i = static_cast<std::size_t>(first); i < changes.size(); ++i) {
        const game::DirectionChange& c = changes[i];
        if (skip_ && c.s_start <= skip_s_ + net::kNoteAnchorStepM) {
            continue;
        }
        if (notes_ == nullptr || !has_note(*notes_, c)) {
            out = c.s_start;
            return true;
        }
    }
    // Todos con nota: se queda en el último.
    out = changes.back().s_start;
    return true;
}

void TabletCursor::note_sent(double s, std::uint32_t notes_revision) {
    manual_ = false;
    held_ = 0;
    hold_s_ = 0.0;
    skip_ = true;
    skip_s_ = s;
    skip_revision_ = notes_revision;
}

void TabletCursor::jump(const std::vector<game::DirectionChange>& changes, double s_car, int dir) {
    if (dir > 0) {
        const int i = game::next_direction_change(changes, valid_ ? s_ : s_car);
        if (i >= 0) {
            s_ = changes[static_cast<std::size_t>(i)].s_start;
            valid_ = true;
            manual_ = true;
        }
    } else if (dir < 0 && valid_) {
        // El anterior, solo si sigue por delante del coche: nunca por detrás.
        const int i = game::previous_direction_change(changes, s_);
        if (i >= 0 && changes[static_cast<std::size_t>(i)].s_start > s_car) {
            s_ = changes[static_cast<std::size_t>(i)].s_start;
            manual_ = true;
        }
    }
}

void TabletCursor::update(const std::vector<game::DirectionChange>& changes, double s_car,
                          double length, int held, double dt, const net::NotesState* notes) {
    notes_ = notes;
    if (skip_ && notes != nullptr && notes->revision != skip_revision_) {
        skip_ = false; // el estado del host ya lo refleja
    }
    // Un cursor a mano que el coche ha pasado deja de serlo.
    if (manual_ && s_ < s_car) {
        manual_ = false;
    }
    if (held != 0 && held != held_) {
        // Pulsación nueva: salta al instante.
        hold_s_ = 0.0;
        if (!manual_) {
            // Desde el valor por defecto de este fotograma.
            valid_ = default_s(changes, s_car, s_);
        }
        jump(changes, s_car, held);
    } else if (held != 0) {
        hold_s_ += dt;
        if (hold_s_ > kCursorHoldS) {
            // Deslizando: desde el valor actual, acotado a [s_car, longitud].
            const double slid = hold_s_ - kCursorHoldS;
            const double from = std::max(0.0, slid - dt);
            // Integral exacta de la rampa en el fotograma: media de la velocidad.
            const double v = 0.5 * (slide_speed(from) + slide_speed(slid));
            if (!valid_) {
                valid_ = true;
                s_ = s_car;
            }
            s_ = std::clamp(s_ + held * v * std::min(dt, slid), s_car, length);
            manual_ = true;
        }
    } else {
        hold_s_ = 0.0;
    }
    held_ = held;
    if (!manual_) {
        valid_ = default_s(changes, s_car, s_);
    }
}

void TabletCursor::place(double s, double s_car, double length) {
    s_ = std::clamp(s, s_car, length);
    valid_ = true;
    manual_ = true;
}

} // namespace rally::platform
