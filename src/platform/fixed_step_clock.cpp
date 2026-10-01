#include "platform/fixed_step_clock.h"

#include <algorithm>

namespace rally::platform {

namespace {

// Un paso, en las unidades del acumulador (ns × kStepsPerSecond).
constexpr std::int64_t kStepUnits = 1'000'000'000;

// Duración máxima que se acepta de un fotograma antes de multiplicar, para que el
// producto no desborde (lo haría por encima de unos 440 días). Por encima del tope de
// pasos solo importa para conservar la fracción de paso.
constexpr std::int64_t kMaxFrameNs = 60'000'000'000;

} // namespace

FrameSteps FixedStepClock::advance(std::int64_t frame_ns) {
    frame_ns = std::clamp<std::int64_t>(frame_ns, 0, kMaxFrameNs);
    accumulator_ += frame_ns * kStepsPerSecond;

    std::int64_t count = accumulator_ / kStepUnits;
    accumulator_ -= count * kStepUnits;
    count = std::min<std::int64_t>(count, kMaxStepsPerFrame);

    FrameSteps steps;
    steps.count = static_cast<int>(count);
    steps.first = steps_done_;
    steps.alpha = static_cast<double>(accumulator_) / static_cast<double>(kStepUnits);
    steps_done_ += count;
    return steps;
}

} // namespace rally::platform
