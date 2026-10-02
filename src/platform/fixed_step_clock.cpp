#include "platform/fixed_step_clock.h"

#include <algorithm>

namespace rally::platform {

namespace {

// Duración máxima que se acepta de un fotograma antes de multiplicar, para que el
// producto no desborde (lo haría por encima de unos 440 días). Por encima del tope de
// pasos solo importa para conservar la fracción de paso.
constexpr std::int64_t kMaxFrameNs = 60'000'000'000;

std::int64_t abs_units(std::int64_t value) { return value < 0 ? -value : value; }

} // namespace

std::int64_t refresh_period_units(std::int64_t numerator, std::int64_t denominator) {
    if (numerator <= 0 || denominator <= 0) {
        return 0;
    }
    // 1 s en unidades del reloj × denominador / numerador, redondeado.
    constexpr std::int64_t kSecondUnits = kStepUnits * kStepsPerSecond;
    return (kSecondUnits * denominator + numerator / 2) / numerator;
}

void FixedStepClock::set_snap_period(std::int64_t period_units) {
    period_units = std::max<std::int64_t>(period_units, 0);
    if (period_units != snap_period_) {
        snap_period_ = period_units;
        centred_ = false; // al volver a ajustar se centra de nuevo la fase
    }
}

FrameSteps FixedStepClock::advance(std::int64_t frame_ns) {
    frame_ns = std::clamp<std::int64_t>(frame_ns, 0, kMaxFrameNs);
    const std::int64_t measured = frame_ns * kStepsPerSecond;

    std::int64_t used = measured;
    bool snapped = false;
    if (snap_period_ > 0) {
        const std::int64_t multiple = (measured + snap_period_ / 2) / snap_period_;
        const std::int64_t target = multiple * snap_period_;
        if (multiple >= 1 && abs_units(measured - target) < kSnapWindowUnits) {
            used = target;
            snapped = true;
        }
    }

    // Deuda: lo medido que no se ha aplicado. Se devuelve poco a poco, o entera si se pasa
    // del tope; el tiempo de juego nunca retrocede.
    debt_ += measured - used;
    std::int64_t repay = abs_units(debt_) > kMaxDebtUnits
                             ? debt_
                             : std::clamp(debt_ / kRepayDivisor, -kMaxRepayUnits, kMaxRepayUnits);
    repay = std::max(repay, -used);
    used += repay;
    debt_ -= repay;

    accumulator_ += used;
    std::int64_t count = accumulator_ / kStepUnits;
    accumulator_ -= count * kStepUnits;
    if (count > kMaxStepsPerFrame) {
        count = kMaxStepsPerFrame;
        debt_ = 0; // ese tiempo se descarta igual que los pasos sobrantes
        centred_ = false;
    }
    if (snapped && !centred_) {
        accumulator_ = kStepUnits / 2; // fase centrada: desplazamiento fijo del origen
        centred_ = true;
    }

    FrameSteps steps;
    steps.count = static_cast<int>(count);
    steps.first = steps_done_;
    steps.alpha = static_cast<double>(accumulator_) / static_cast<double>(kStepUnits);
    steps.used = used;
    steps.snapped = snapped;
    steps_done_ += count;
    return steps;
}

} // namespace rally::platform
