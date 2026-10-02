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

void SmoothedAxis::step(std::int16_t target, bool target_digital, AxisRates digital_rates,
                        AxisRates analog_rates) {
    if (target != 0) {
        digital = target_digital;
    }
    applied = follow_target(applied, target, digital ? digital_rates : analog_rates);
}

void ControlState::step(const SimInput& input) {
    steer.step(input.steer, (input.buttons & kButtonSteerDigital) != 0, kSteerDigitalRates,
               kSteerAnalogRates);
    throttle.step(input.throttle, (input.buttons & kButtonThrottleDigital) != 0,
                  kThrottleDigitalRates, kThrottleAnalogRates);
    brake.step(input.brake, (input.buttons & kButtonBrakeDigital) != 0, kBrakeDigitalRates,
               kBrakeAnalogRates);
    handbrake = follow_target(handbrake, (input.buttons & kButtonHandbrake) != 0 ? kAxisMax : 0,
                              kHandbrakeRates);
    pressed = static_cast<std::uint16_t>(input.buttons & ~previous_buttons);
    previous_buttons = input.buttons;
}

void ControlState::zero_axes() {
    steer.applied = 0;
    throttle.applied = 0;
    brake.applied = 0;
    handbrake = 0;
}

} // namespace rally::sim
