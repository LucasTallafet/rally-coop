#include "platform/copilot_wheel.h"

#include <cmath>

namespace rally::platform {

namespace {

constexpr float kPi = 3.14159265358979323846f;

// Posición más cercana a `angle` (grados desde arriba, [0, 360)): el índice y la distancia angular
// a su centro.
int nearest(const WheelSpec& spec, float angle, float& distance) {
    int best = 0;
    float best_distance = 1.0e9f;
    for (int i = 0; i < spec.count; ++i) {
        float d = std::fabs(angle - wheel_center_deg(spec, i));
        if (spec.shape == WheelShape::Circle && d > 180.0f) {
            d = 360.0f - d;
        }
        if (d < best_distance) {
            best_distance = d;
            best = i;
        }
    }
    distance = best_distance;
    return best;
}

} // namespace

float wheel_angle_deg(float x, float y) {
    if (x == 0.0f && y == 0.0f) {
        return 0.0f;
    }
    float angle = std::atan2(x, y) * 180.0f / kPi; // desde arriba, horario
    if (angle < 0.0f) {
        angle += 360.0f;
    }
    return angle;
}

float wheel_center_deg(const WheelSpec& spec, int index) {
    return spec.step_deg() * static_cast<float>(index);
}

int wheel_pick(const WheelSpec& spec, float x, float y, int current) {
    const float magnitude = std::sqrt(x * x + y * y);
    const float dead = current >= 0 ? kWheelDeadzoneStay : kWheelDeadzone;
    if (magnitude < dead) {
        return -1;
    }
    // Media rueda: simétrica, solo cuenta |x|.
    const float angle = wheel_angle_deg(spec.shape == WheelShape::HalfCircle ? std::fabs(x) : x, y);
    float distance = 0.0f;
    const int candidate = nearest(spec, angle, distance);
    if (current < 0 || current >= spec.count || candidate == current) {
        return candidate;
    }
    // Histéresis: se cambia solo si el ángulo pasa kWheelHysteresisDeg del límite con la vecina,
    // es decir, si está más cerca de la nueva que de la actual por ese margen.
    float to_current = std::fabs(angle - wheel_center_deg(spec, current));
    if (spec.shape == WheelShape::Circle && to_current > 180.0f) {
        to_current = 360.0f - to_current;
    }
    return to_current - distance > 2.0f * kWheelHysteresisDeg ? candidate : current;
}

} // namespace rally::platform
