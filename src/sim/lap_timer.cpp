#include "sim/lap_timer.h"

#include "sim/rmath.h"

namespace rally::sim {

int line_crossing(const FinishLine& line, double x0, double z0, double x1, double z1) {
    // Distancia con signo a la línea a lo largo de su dirección de avance.
    const double d0 = (x0 - line.x) * line.forward_x + (z0 - line.z) * line.forward_z;
    const double d1 = (x1 - line.x) * line.forward_x + (z1 - line.z) * line.forward_z;
    const bool forward = d0 < 0.0 && d1 >= 0.0;
    const bool backward = d0 >= 0.0 && d1 < 0.0;
    if (!forward && !backward) {
        return 0;
    }
    // Punto de cruce: tiene que caer dentro de la línea.
    const double t = d0 / (d0 - d1);
    const double cx = x0 + t * (x1 - x0) - line.x;
    const double cz = z0 + t * (z1 - z0) - line.z;
    const double lateral = cx * -line.forward_z + cz * line.forward_x;
    if (rmath::abs(lateral) > line.half_length) {
        return 0;
    }
    return forward ? 1 : -1;
}

void LapTimer::update(double x0, double z0, double x1, double z1, bool reset) {
    LapState& s = state_;
    ++s.steps;
    if (reset) {
        s.lap_start = -1;
        s.debt = 0;
        return;
    }
    const int crossing = line_crossing(line_, x0, z0, x1, z1);
    if (crossing == 0) {
        return;
    }
    if (crossing < 0) {
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

void RoadTimer::update(double x0, double z0, double x1, double z1, bool reset) {
    RoadRunState& s = state_;
    ++s.steps;
    if (reset) {
        if (s.run_start >= 0) {
            ++s.resets;
        }
        s.start_debt = 0;
        s.finish_debt = 0;
        return;
    }
    // Las dos líneas están a casi 1 km: un paso no cruza las dos.
    const int start = line_crossing(start_, x0, z0, x1, z1);
    if (start < 0) {
        ++s.start_debt;
    } else if (start > 0) {
        if (s.start_debt > 0) {
            --s.start_debt;
        } else {
            s.run_start = s.steps;
            s.resets = 0;
        }
    }
    const int finish = line_crossing(finish_, x0, z0, x1, z1);
    if (finish < 0) {
        ++s.finish_debt;
    } else if (finish > 0) {
        if (s.finish_debt > 0) {
            --s.finish_debt;
        } else if (s.run_start >= 0) {
            const std::int64_t run = s.steps - s.run_start;
            s.last_run = run;
            s.last_resets = s.resets;
            if (s.resets == 0) {
                s.best_clean = s.best_clean < 0 ? run : rmath::min(s.best_clean, run);
            }
            ++s.runs;
            s.run_start = -1;
            s.resets = 0;
        }
    }
}

} // namespace rally::sim
