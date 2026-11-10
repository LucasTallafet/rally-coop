#include "platform/tablet_cursor.h"

#include <algorithm>

namespace rally::platform {

double TabletCursor::slide_speed(double slid_s) {
    const double k = std::clamp(slid_s / kCursorRampS, 0.0, 1.0);
    return kCursorSlideMin + (kCursorSlideMax - kCursorSlideMin) * k;
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
                          double length, int held, double dt) {
    // Un cursor a mano que el coche ha pasado deja de serlo.
    if (manual_ && s_ < s_car) {
        manual_ = false;
    }
    if (held != 0 && held != held_) {
        // Pulsación nueva: salta al instante.
        hold_s_ = 0.0;
        if (!manual_) {
            // Desde el valor por defecto de este fotograma.
            const int i = game::next_direction_change(changes, s_car);
            valid_ = i >= 0;
            if (valid_) {
                s_ = changes[static_cast<std::size_t>(i)].s_start;
            }
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
        const int i = game::next_direction_change(changes, s_car);
        valid_ = i >= 0;
        if (valid_) {
            s_ = changes[static_cast<std::size_t>(i)].s_start;
        }
    }
}

void TabletCursor::place(double s, double s_car, double length) {
    s_ = std::clamp(s, s_car, length);
    valid_ = true;
    manual_ = true;
}

} // namespace rally::platform
