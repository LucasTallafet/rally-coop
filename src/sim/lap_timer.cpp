#include "sim/lap_timer.h"

#include "sim/rmath.h"

namespace rally::sim {

void LapTimer::update(double x0, double z0, double x1, double z1, bool reset) {
    LapState& s = state_;
    ++s.steps;
    if (reset) {
        s.lap_start = -1;
        s.debt = 0;
        return;
    }
    // Distancia con signo a la línea a lo largo de su dirección de avance.
    const double d0 = (x0 - line_.x) * line_.forward_x + (z0 - line_.z) * line_.forward_z;
    const double d1 = (x1 - line_.x) * line_.forward_x + (z1 - line_.z) * line_.forward_z;
    const bool forward = d0 < 0.0 && d1 >= 0.0;
    const bool backward = d0 >= 0.0 && d1 < 0.0;
    if (!forward && !backward) {
        return;
    }
    // Punto de cruce: tiene que caer dentro de la línea.
    const double t = d0 / (d0 - d1);
    const double cx = x0 + t * (x1 - x0) - line_.x;
    const double cz = z0 + t * (z1 - z0) - line_.z;
    const double lateral = cx * -line_.forward_z + cz * line_.forward_x;
    if (rmath::abs(lateral) > line_.half_length) {
        return;
    }
    if (backward) {
        ++s.debt;
        return;
    }
    if (s.debt > 0) {
        --s.debt;
        return;
    }
    if (s.lap_start >= 0) {
        const std::int64_t lap = s.steps - s.lap_start;
        s.last_lap = lap;
        s.best_lap = s.best_lap < 0 ? lap : rmath::min(s.best_lap, lap);
        ++s.laps;
    }
    s.lap_start = s.steps;
}

} // namespace rally::sim
