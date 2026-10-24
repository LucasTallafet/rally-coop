#include "platform/copilot_view.h"

#include "platform/seat.h"

#include <algorithm>

namespace rally::platform {

namespace {

constexpr double kRadPerRpm = 6.283185307179586 / 60.0;
constexpr double kPaPerBar = 1.0e5;

} // namespace

Vec3d copilot_eye(const sim::CarData& car) {
    const Vec3d eye = car_eye(car);
    return Vec3d{-eye.x, eye.y, eye.z};
}

double look_step(double t, bool held, double dt_s) {
    const double step = dt_s / kLookSeconds;
    return std::clamp(held ? t + step : t - step, 0.0, 1.0);
}

double look_yaw(double t) {
    const double c = std::clamp(t, 0.0, 1.0);
    return kLookDashYaw * c * c * (3.0 - 2.0 * c);
}

CopilotCockpit copilot_cockpit(const net::SampledState& state, const sim::CarData& car) {
    CopilotCockpit out;
    out.wheel_angle = steering_wheel_angle(state.steer, car);
    CockpitDisplay& d = out.display;
    d.gear = std::clamp<int>(state.gear, -1, 5);
    d.rpm_fraction = static_cast<float>(static_cast<double>(state.rpm) * kRadPerRpm /
                                        static_cast<double>(car.limiter_speed));
    d.red_fraction = car.upshift_full / car.limiter_speed;
    d.turbo = car.boost_max > 0.0f;
    d.boost_fraction = d.turbo ? static_cast<float>(static_cast<double>(state.boost_bar) *
                                                    kPaPerBar / static_cast<double>(car.boost_max))
                               : 0.0f;
    return out;
}

} // namespace rally::platform
