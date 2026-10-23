#include "platform/wheel_spin.h"

#include <cmath>

namespace rally::platform {

double wrap_angle(double a) {
    constexpr double kTwoPi = 6.283185307179586;
    a = std::fmod(a + 3.141592653589793, kTwoPi);
    return (a <= 0.0 ? a + kTwoPi : a) - 3.141592653589793;
}

double wheel_spin_delta(double forward_distance, double radius) {
    if (radius <= 0.0 || std::fabs(forward_distance) > kWheelSpinMaxJump) {
        return 0.0;
    }
    return forward_distance / radius;
}

double rest_suspension_length(double attach_y, double radius) { return attach_y - radius; }

} // namespace rally::platform
