#include "platform/fixed_step_clock.h"
#include "sim/pcg32.h"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <limits>

using rally::platform::FixedStepClock;
using rally::platform::FrameSteps;
using rally::platform::kMaxStepsPerFrame;

namespace {

constexpr std::int64_t kNsPerSecond = 1'000'000'000;

// Pasos que corresponden a un tiempo total sin tope: floor(total × 240 / 1 s).
std::int64_t expected_steps(std::int64_t total_ns) { return total_ns * 240 / kNsPerSecond; }

void check_alpha(const FrameSteps& steps) {
    REQUIRE(steps.alpha >= 0.0);
    REQUIRE(steps.alpha < 1.0);
}

// Repite un ciclo de duraciones que suma un número exacto de pasos y comprueba que todos
// los fotogramas dan los mismos pasos.
template <std::size_t N>
void check_constant_rate(const std::array<std::int64_t, N>& cycle, int steps_per_frame) {
    FixedStepClock clock;
    for (int frame = 0; frame < 10'000; ++frame) {
        const FrameSteps steps = clock.advance(cycle[static_cast<std::size_t>(frame) % N]);
        REQUIRE(steps.count == steps_per_frame);
        check_alpha(steps);
    }
    CHECK(clock.steps_done() == 10'000 * steps_per_frame);
}

} // namespace

TEST_CASE("fixed step clock: 60 fps gives 4 steps per frame") {
    // 1/60 s no es un número entero de ns; tres fotogramas suman 1/20 s exacto. El largo va
    // primero para que ningún prefijo del ciclo quede por debajo de un número entero de pasos.
    check_constant_rate(std::array<std::int64_t, 3>{16'666'667, 16'666'667, 16'666'666}, 4);
}

TEST_CASE("fixed step clock: 30 fps gives 8 steps per frame") {
    check_constant_rate(std::array<std::int64_t, 3>{33'333'334, 33'333'333, 33'333'333}, 8);
}

TEST_CASE("fixed step clock: a frame of zero gives no steps") {
    FixedStepClock clock;
    const FrameSteps steps = clock.advance(0);
    CHECK(steps.count == 0);
    CHECK(steps.first == 0);
    CHECK(steps.alpha == 0.0);
}

TEST_CASE("fixed step clock: a step starts exactly at 1/240 s") {
    FixedStepClock clock;
    CHECK(clock.advance(4'166'666).count == 0); // 4 166 666 × 240 < 10^9
    CHECK(clock.advance(1).count == 1);         // 4 166 667 × 240 ≥ 10^9
}

TEST_CASE("fixed step clock: irregular frames give floor of total time over step") {
    FixedStepClock clock;
    rally::sim::Pcg32 rng(2026u, 10u);
    std::int64_t total_ns = 0;
    std::int64_t total_steps = 0;
    for (int frame = 0; frame < 10'000; ++frame) {
        // Entre 0 y 100 ms: nunca supera el tope de 24 pasos.
        const std::int64_t frame_ns = rng.bounded(100'000'001u);
        const FrameSteps steps = clock.advance(frame_ns);
        REQUIRE(steps.count <= kMaxStepsPerFrame);
        REQUIRE(steps.first == total_steps); // numeración sin huecos
        check_alpha(steps);

        total_ns += frame_ns;
        total_steps += steps.count;
        REQUIRE(total_steps == expected_steps(total_ns));
        // alpha es exactamente la fracción de paso pendiente.
        REQUIRE(steps.alpha == static_cast<double>(total_ns * 240 % kNsPerSecond) /
                                   static_cast<double>(kNsPerSecond));
    }
    CHECK(clock.steps_done() == total_steps);
}

TEST_CASE("fixed step clock: long frames are capped without catching up later") {
    FixedStepClock clock;
    // 1,001 s son 240,24 pasos: se dan 24 y se descartan los 216 enteros sobrantes.
    const FrameSteps capped = clock.advance(1'001'000'000);
    CHECK(capped.count == kMaxStepsPerFrame);
    CHECK(capped.alpha == doctest::Approx(0.24).epsilon(1e-12));

    // El fotograma siguiente vuelve a lo normal: 0,24 + 4 pasos.
    const FrameSteps next = clock.advance(16'666'667);
    CHECK(next.count == 4);
    CHECK(next.first == kMaxStepsPerFrame);
    check_alpha(next);
}

TEST_CASE("fixed step clock: frames just over the cap give exactly the cap") {
    FixedStepClock clock;
    CHECK(clock.advance(100'000'000).count == 24); // 100 ms: justo el tope
    CHECK(clock.advance(100'000'000).count == 24);
    CHECK(clock.advance(104'166'667).count == 24); // 25 pasos pedidos
    CHECK(clock.advance(16'666'667).count == 4);
}

TEST_CASE("fixed step clock: extreme and negative durations are safe") {
    FixedStepClock clock;
    const FrameSteps huge = clock.advance(std::numeric_limits<std::int64_t>::max());
    CHECK(huge.count == kMaxStepsPerFrame);
    check_alpha(huge);

    const FrameSteps negative = clock.advance(-5'000'000);
    CHECK(negative.count == 0);
    CHECK(negative.alpha == huge.alpha);
    CHECK(clock.steps_done() == kMaxStepsPerFrame);
}
