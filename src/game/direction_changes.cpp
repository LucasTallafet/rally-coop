#include "game/direction_changes.h"

#include "sim/rmath.h"

#include <cstddef>
#include <utility>

namespace rally::game {

namespace {

// |curvatura| por debajo de la cual un trozo cuenta como recta: radio de 1e9 m. Solo absorbe el
// ruido de coma flotante (el parser da 0 exacto en las rectas y en el origen de las clotoides);
// un umbral mayor movería el s_start de las clotoides.
constexpr double kStraightCurvature = 1e-9;

struct Builder {
    std::vector<DirectionChange> out;
    bool open = false;
    int sign = 0;
    double s_start = 0.0;
    double s_end = 0.0;
    double angle = 0.0;

    void close() {
        if (open && rmath::abs(angle) >= kDirectionChangeMinAngle) {
            out.push_back({s_start, s_end, angle});
        }
        open = false;
    }

    // Trozo de un solo signo: curvatura lineal de k0 a k1 desde s0 hasta s1.
    void piece(double s0, double s1, double k0, double k1) {
        if (!(s1 > s0)) {
            return;
        }
        const bool straight =
            rmath::abs(k0) <= kStraightCurvature && rmath::abs(k1) <= kStraightCurvature;
        if (straight) {
            close();
            return;
        }
        const double mean = 0.5 * (k0 + k1);
        const int piece_sign = mean > 0.0 ? 1 : -1;
        if (open && piece_sign != sign) {
            close();
        }
        if (!open) {
            open = true;
            sign = piece_sign;
            s_start = s0;
            angle = 0.0;
        }
        s_end = s1;
        angle += mean * (s1 - s0);
    }
};

} // namespace

std::vector<DirectionChange> direction_changes(const sim::StageAxis& axis) {
    Builder b;
    for (const sim::StagePrimitive& p : axis.primitives()) {
        const double s1 = p.s0 + p.length;
        const double k0 = p.curvature0;
        const double k1 = p.curvature1;
        const bool crosses = (k0 > kStraightCurvature && k1 < -kStraightCurvature) ||
                             (k0 < -kStraightCurvature && k1 > kStraightCurvature);
        if (crosses) {
            const double sm = p.s0 + p.length * k0 / (k0 - k1);
            b.piece(p.s0, sm, k0, 0.0);
            b.piece(sm, s1, 0.0, k1);
        } else {
            b.piece(p.s0, s1, k0, k1);
        }
    }
    b.close();
    return std::move(b.out);
}

int next_direction_change(const std::vector<DirectionChange>& changes, double s) {
    std::size_t lo = 0;
    std::size_t hi = changes.size();
    while (lo < hi) { // primero con s_start > s
        const std::size_t mid = lo + (hi - lo) / 2;
        if (changes[mid].s_start > s) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }
    return lo < changes.size() ? static_cast<int>(lo) : -1;
}

int previous_direction_change(const std::vector<DirectionChange>& changes, double s) {
    std::size_t lo = 0;
    std::size_t hi = changes.size();
    while (lo < hi) { // primero con s_start >= s; el anterior es el buscado
        const std::size_t mid = lo + (hi - lo) / 2;
        if (changes[mid].s_start >= s) {
            hi = mid;
        } else {
            lo = mid + 1;
        }
    }
    return lo > 0 ? static_cast<int>(lo) - 1 : -1;
}

} // namespace rally::game
