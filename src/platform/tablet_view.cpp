#include "platform/tablet_view.h"

#include "platform/wheel_spin.h"

#include <algorithm>
#include <cmath>

namespace rally::platform {

namespace {

// Coeficiente del filtro de primer orden exacto en el tiempo del fotograma.
double blend(double dt, double tau) { return dt <= 0.0 ? 0.0 : 1.0 - std::exp(-dt / tau); }

} // namespace

int zoom_level_for(double speed_mps, int current) {
    int level = std::clamp(current, 0, kZoomLevels - 1);
    while (level < kZoomLevels - 1 && speed_mps > kZoomSpeed[level] * (1.0 + kZoomHysteresis)) {
        ++level;
    }
    while (level > 0 && speed_mps < kZoomSpeed[level - 1] * (1.0 - kZoomHysteresis)) {
        --level;
    }
    return level;
}

void car_position(const sim::StageTrack& track, double s, double t, double& x, double& z) {
    const sim::StagePose p = track.pose(s);
    x = p.x + t * std::cos(p.heading);
    z = p.z + t * std::sin(p.heading);
}

double target_heading(const sim::StageTrack& track, double s, double t,
                      MapOrientation orientation) {
    if (orientation == MapOrientation::Axis) {
        return track.pose(s).heading;
    }
    double cx = 0.0;
    double cz = 0.0;
    car_position(track, s, t, cx, cz);
    const sim::StagePose ahead = track.pose(s + kAheadDistance);
    const double dx = ahead.x - cx;
    const double dz = ahead.z - cz;
    if (dx * dx + dz * dz < 1e-6) {
        return track.pose(s).heading; // sin dirección (al final del eje): la tangente
    }
    // Rumbo 0 hacia −z, positivo a la derecha: delante = (sin h, −cos h).
    return std::atan2(dx, -dz);
}

void MapView::to_screen(double x, double z, double& px, double& py) const {
    const double dx = x - cx;
    const double dz = z - cz;
    const double right = dx * std::cos(heading) + dz * std::sin(heading);
    const double forward = dx * std::sin(heading) - dz * std::cos(heading);
    px = arrow_x + right * ppm;
    py = arrow_y - forward * ppm;
}

void MapView::to_world(double px, double py, double& x, double& z) const {
    const double right = (px - arrow_x) / ppm;
    const double forward = (arrow_y - py) / ppm;
    x = cx + right * std::cos(heading) + forward * std::sin(heading);
    z = cz + right * std::sin(heading) - forward * std::cos(heading);
}

double TabletView::distance() const { return std::exp(log_distance_); }

void TabletView::update(const sim::StageTrack& track, double s, double t, double now_s,
                        MapOrientation orientation) {
    const double dt = initialized_ ? std::max(0.0, now_s - now_s_) : 0.0;
    const double heading = target_heading(track, s, t, orientation);
    relocated_ = false;
    if (!initialized_) {
        initialized_ = true;
        heading_ = heading;
        speed_ = 0.0;
        level_ = 0;
        log_distance_ = std::log(kZoomDistance[0]);
    } else {
        // El rumbo siempre lo suaviza el filtro, también tras una recolocación.
        heading_ += wrap_angle(heading - heading_) * blend(dt, kHeadingTau);
        if (std::abs(s - s_) > kRelocationJump + kMaxSpeed * dt) {
            relocated_ = true; // esa velocidad no cuenta
        } else if (dt > 0.0) {
            // Avance por la carretera: el que da hacia atrás (recolocar unos metros) no cuenta.
            speed_ += (std::max(0.0, (s - s_) / dt) - speed_) * blend(dt, kSpeedTau);
        }
        level_ = zoom_level_for(speed_, level_);
        log_distance_ += (std::log(kZoomDistance[level_]) - log_distance_) * blend(dt, kZoomTau);
    }
    now_s_ = now_s;
    s_ = s;
    t_ = t;
    car_position(track, s, t, car_x_, car_z_);
}

MapView TabletView::map_view(const UiRect& map_rect) const {
    MapView v;
    v.rect = map_rect;
    v.arrow_x = map_rect.x + 0.5 * map_rect.w;
    v.arrow_y = map_rect.y + kArrowFraction * map_rect.h;
    v.distance = distance();
    v.ppm = (v.arrow_y - map_rect.y) / v.distance;
    v.phase_ppm = (v.arrow_y - map_rect.y) / kZoomDistance[level_];
    v.cx = car_x_;
    v.cz = car_z_;
    v.heading = heading_;
    return v;
}

} // namespace rally::platform
