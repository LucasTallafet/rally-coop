#include "sim/steer_assist.h"

#include "sim/rmath.h"

namespace rally::sim {

namespace {

// Iteraciones de Newton de la inversa de Ackermann (número fijo: mismo resultado siempre). La
// relación es casi lineal (pendiente cerca de 1): con 4, el error queda muy por debajo de un
// paso del eje.
constexpr int kAckermannNewtonIterations = 4;

// Ángulo de Ackermann puro de la rueda exterior con la interior a `inner` (> 0) y su derivada.
float pure_ackermann(const CarData& car, float inner) {
    return rmath::atan(car.wheelbase / (car.wheelbase / rmath::tan(inner) + car.track));
}

float pure_ackermann_slope(const CarData& car, float inner) {
    // u = L / (L·cot i + T); du/di = L²·csc²i / (L·cot i + T)²; d atan(u) = du / (1 + u²).
    const float s = rmath::sin(inner);
    const float d = car.wheelbase * rmath::cos(inner) / s + car.track;
    const float u = car.wheelbase / d;
    const float du = car.wheelbase * car.wheelbase / (s * s * d * d);
    return du / (1.0f + u * u);
}

std::int32_t to_axis_units(float axis) {
    return static_cast<std::int32_t>(
        rmath::floor(rmath::clamp(axis, 0.0f, 1.0f) * static_cast<float>(kAxisMax) + 0.5f));
}

// Tope de un lado en unidades de eje: el borde de la ventana (ángulo de la rueda hacia ese
// lado, rad) pasado por la inversa de Ackermann.
std::int32_t side_limit(const CarData& car, const SteerSide& side, float edge) {
    if (edge <= 0.0f) {
        return 0;
    }
    return to_axis_units(axis_for_wheel_angle(car, edge, side.outer));
}

} // namespace

float wheel_angle_for_axis(const CarData& car, float axis, bool outer) {
    const float inner = rmath::clamp(axis, 0.0f, 1.0f) * car.max_steer;
    if (!outer || inner <= 0.0f) {
        return inner;
    }
    return (1.0f - car.ackermann) * inner + car.ackermann * pure_ackermann(car, inner);
}

float axis_for_wheel_angle(const CarData& car, float angle, bool outer) {
    if (angle <= 0.0f) {
        return 0.0f;
    }
    if (!outer) {
        return rmath::min(angle / car.max_steer, 1.0f);
    }
    if (angle >= wheel_angle_for_axis(car, 1.0f, true)) {
        return 1.0f;
    }
    // Newton sobre g(i) = exterior(i) − angle, desde i = angle (la exterior gira algo menos).
    float inner = angle;
    for (int k = 0; k < kAckermannNewtonIterations; ++k) {
        const float g =
            (1.0f - car.ackermann) * inner + car.ackermann * pure_ackermann(car, inner) - angle;
        const float slope =
            (1.0f - car.ackermann) + car.ackermann * pure_ackermann_slope(car, inner);
        inner = rmath::clamp(inner - g / slope, 1.0e-6f, car.max_steer);
    }
    return rmath::min(inner / car.max_steer, 1.0f);
}

SteerContext steer_context(const SteerAssistInput& in, const CarData& car, bool assist) {
    SteerContext ctx;
    ctx.speed = in.speed;
    ctx.rear_sliding =
        in.rear_loaded && rmath::abs(in.rear_vel_angle) > rmath::atan(in.rear_tan_alpha_peak);
    const bool any_front = in.side[kSteerRight].loaded || in.side[kSteerLeft].loaded;
    if (!assist || in.speed < kAssistStartSpeed || in.reverse || in.forward_speed < 0.0f ||
        !any_front) {
        return ctx;
    }
    ctx.active = true;
    const float t =
        rmath::min((in.speed - kAssistStartSpeed) / (kAssistFullSpeed - kAssistStartSpeed), 1.0f);
    const auto blend = [t](std::int32_t limit) {
        const float full = static_cast<float>(kAxisMax);
        return static_cast<std::int32_t>(
            rmath::floor(full + t * (static_cast<float>(limit) - full) + 0.5f));
    };
    // Contravolante: el lado hacia el que se va la trasera, si desliza, no recorta.
    const bool counter_right = ctx.rear_sliding && in.rear_vel_angle > 0.0f;
    const bool counter_left = ctx.rear_sliding && in.rear_vel_angle < 0.0f;

    const SteerSide& right = in.side[kSteerRight];
    if (right.loaded && !counter_right) {
        const float edge = right.vel_angle + rmath::atan(right.tan_alpha_peak);
        ctx.right_max = blend(side_limit(car, right, edge));
    }
    const SteerSide& left = in.side[kSteerLeft];
    if (left.loaded && !counter_left) {
        const float edge = -(left.vel_angle - rmath::atan(left.tan_alpha_peak));
        ctx.left_max = blend(side_limit(car, left, edge));
    }
    return ctx;
}

} // namespace rally::sim
