#include "platform/fixed_step_clock.h"
#include "platform/frame_history.h"

#include <doctest/doctest.h>

#include <cstdint>

using rally::platform::FrameCounts;
using rally::platform::FrameHistory;
using rally::platform::FrameRecord;
using rally::platform::kStepUnits;
using rally::platform::refresh_period_units;

namespace {

FrameRecord frame(std::int64_t measured_ns, int steps) {
    return FrameRecord{measured_ns, measured_ns * 240, steps};
}

} // namespace

TEST_CASE("frame history: keeps the last 240 frames, oldest first") {
    FrameHistory history;
    CHECK(history.size() == 0);
    for (int i = 0; i < 100; ++i) {
        history.push(frame(i, i));
    }
    CHECK(history.size() == 100);
    CHECK(history.at(0).steps == 0);
    CHECK(history.at(99).steps == 99);

    for (int i = 100; i < 1'000; ++i) {
        history.push(frame(i, i));
    }
    CHECK(history.size() == FrameHistory::kCapacity);
    CHECK(history.at(0).steps == 1'000 - FrameHistory::kCapacity);
    CHECK(history.at(FrameHistory::kCapacity - 1).steps == 999);
}

TEST_CASE("frame history: splits off-step frames into lost frames and noise at 60 Hz") {
    FrameHistory history;
    const std::int64_t period = refresh_period_units(60, 1);
    for (int i = 0; i < 230; ++i) {
        history.push(frame(16'666'667, 4));
    }
    history.push(frame(16'200'000, 3)); // ruido: ~1 periodo con 3 pasos
    history.push(frame(17'100'000, 5)); // ruido: ~1 periodo con 5 pasos
    history.push(frame(33'333'333, 8)); // perdido
    history.push(frame(33'333'333, 8));
    history.push(frame(24'999'999, 6)); // a 1,5 periodos menos 1 ns: ruido
    history.push(frame(25'000'000, 6)); // a 1,5 periodos: perdido

    const FrameCounts counts = history.count(period);
    CHECK(counts.frames == 236);
    CHECK(counts.expected_steps == 4);
    CHECK(counts.off_steps == 6);
    CHECK(counts.dropped == 3);
    CHECK(counts.noise == 3);
}

TEST_CASE("frame history: 30 fps expects 8 steps per frame") {
    FrameHistory history;
    history.push(frame(33'333'333, 8));
    history.push(frame(33'333'333, 7));
    history.push(frame(66'666'667, 16));
    const FrameCounts counts = history.count(8 * kStepUnits);
    CHECK(counts.expected_steps == 8);
    CHECK(counts.off_steps == 2);
    CHECK(counts.dropped == 1);
    CHECK(counts.noise == 1);
}

TEST_CASE("frame history: no expected steps without a whole number of steps per frame") {
    FrameHistory history;
    history.push(frame(6'944'444, 2));
    CHECK(history.count(refresh_period_units(144, 1)).expected_steps == 0); // 1,67 pasos
    CHECK(history.count(refresh_period_units(144, 1)).off_steps == 0);
    CHECK(history.count(0).expected_steps == 0); // sin sincronización vertical
}
