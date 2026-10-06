#include "platform/head_motion.h"

#include <algorithm>
#include <cmath>

namespace rally::platform {

namespace {

constexpr double kOmega = 2.0 * 3.14159265358979323846 * kHeadFrequency;

struct Axis {
    double gain;  // por g
    double limit; // límite suave
};

constexpr std::array<Axis, HeadMotion::kAxes> kAxis = {{
    {kHeadGainForward * kHeadIntensity, kHeadLimitForward},
    {kHeadGainRight * kHeadIntensity, kHeadLimitRight},
    {kHeadGainUp * kHeadIntensity, kHeadLimitUp},
    {kHeadGainRoll * kHeadIntensity, kHeadLimitRoll},
    {kHeadGainPitch * kHeadIntensity, kHeadLimitPitch},
}};

double soft_limit(double x, double limit) { return limit * std::tanh(x / limit); }

} // namespace

HeadOffset lerp(const HeadOffset& a, const HeadOffset& b, double t) {
    const auto mix = [t](double p, double q) { return p + (q - p) * t; };
    return {mix(a.forward, b.forward), mix(a.right, b.right), mix(a.up, b.up), mix(a.roll, b.roll),
            mix(a.pitch, b.pitch)};
}

void HeadMotion::step(const HeadInput& input) {
    const double clamp = kHeadInputClamp * kHeadGravity;
    const double forward = std::clamp(input.forward, -clamp, clamp) / kHeadGravity;
    const double right = std::clamp(input.right, -clamp, clamp) / kHeadGravity;
    const double up = std::clamp(input.up, -clamp, clamp) / kHeadGravity;
    // Objetivo de cada eje en g de entrada: la cabeza va contra la fuerza; acelerando mira un
    // poco hacia arriba y en una curva a la derecha se inclina a la izquierda.
    const std::array<double, kAxes> target_g = {-forward, -right, -up, -right, forward};
    for (int i = 0; i < kAxes; ++i) {
        const auto k = static_cast<std::size_t>(i);
        const double target = kAxis[k].gain * target_g[k];
        const double accel =
            kOmega * kOmega * (target - x_[k]) - 2.0 * kHeadDamping * kOmega * v_[k];
        v_[k] += accel * kHeadStep;
        x_[k] += v_[k] * kHeadStep;
    }
    previous_ = current_;
    current_ = {soft_limit(x_[0], kAxis[0].limit), soft_limit(x_[1], kAxis[1].limit),
                soft_limit(x_[2], kAxis[2].limit), soft_limit(x_[3], kAxis[3].limit),
                soft_limit(x_[4], kAxis[4].limit)};
}

void HeadMotion::reset() {
    x_ = {};
    v_ = {};
    previous_ = {};
    current_ = {};
}

} // namespace rally::platform
