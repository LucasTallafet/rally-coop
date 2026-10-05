#include "sim/input_smoothing.h"

#include "sim/rmath.h"

namespace rally::sim {

std::int32_t follow_target(std::int32_t applied, std::int32_t target, AxisRates rates) {
    if (applied == target || rates.rise == 0) {
        return target;
    }
    const bool up = target > applied;
    const bool to_center = applied != 0 && (applied > 0) != up;
    if (to_center) {
        // No cruza el 0 en el mismo paso: si el objetivo está al otro lado, se para en 0.
        return applied > 0 ? rmath::max(applied - rates.fall, rmath::max(target, 0))
                           : rmath::min(applied + rates.fall, rmath::min(target, 0));
    }
    return up ? rmath::min(applied + rates.rise, target) : rmath::max(applied - rates.rise, target);
}

AxisRates steer_rates(float speed, bool digital) {
    const float t = rmath::clamp(speed, 0.0f, kSteerRateFullSpeed) / kSteerRateFullSpeed;
    const auto steps = [t](int still, int fast) {
        return static_cast<int>(
            rmath::floor(static_cast<float>(still) + t * static_cast<float>(fast - still) + 0.5f));
    };
    if (digital) {
        return {rate_for_steps(steps(kSteerDigitalRiseSteps, kSteerDigitalRiseStepsFast)),
                rate_for_steps(steps(kSteerDigitalFallSteps, kSteerDigitalFallStepsFast))};
    }
    const std::int32_t rate = rate_for_steps(steps(kSteerAnalogSteps, kSteerAnalogStepsFast));
    return {rate, rate};
}

std::int32_t clip_steer(std::int32_t target, const SteerContext& ctx) {
    if (target > 0) {
        return rmath::min(target, ctx.right_max);
    }
    if (target < 0) {
        return rmath::max(target, -ctx.left_max);
    }
    return 0;
}

void SmoothedAxis::step(std::int32_t target, bool target_digital, AxisRates digital_rates,
                        AxisRates analog_rates) {
    if (target != 0) {
        digital = target_digital;
    }
    applied = follow_target(applied, target, digital ? digital_rates : analog_rates);
}

std::int32_t ControlState::step(const SimInput& input, const SteerContext& steer_context) {
    // El modo sale del objetivo del jugador, no del recortado: un recorte a 0 no cambia de
    // teclado a stick.
    if (input.steer != 0) {
        steer.digital = (input.buttons & kButtonSteerDigital) != 0;
    }
    const std::int32_t aided = clip_steer(input.steer, steer_context);
    steer.applied =
        follow_target(steer.applied, aided, steer_rates(steer_context.speed, steer.digital));
    throttle.step(input.throttle, (input.buttons & kButtonThrottleDigital) != 0,
                  kThrottleDigitalRates, kThrottleAnalogRates);
    brake.step(input.brake, (input.buttons & kButtonBrakeDigital) != 0, kBrakeDigitalRates,
               kBrakeAnalogRates);
    handbrake = follow_target(handbrake, (input.buttons & kButtonHandbrake) != 0 ? kAxisMax : 0,
                              kHandbrakeRates);
    pressed = static_cast<std::uint16_t>(input.buttons & ~previous_buttons);
    previous_buttons = input.buttons;
    return aided;
}

void ControlState::zero_axes() {
    steer.applied = 0;
    throttle.applied = 0;
    brake.applied = 0;
    handbrake = 0;
}

} // namespace rally::sim
