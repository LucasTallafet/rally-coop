#include "new_counter.h"
#include "platform/perf_stats.h"

#include <doctest/doctest.h>

#include <cstdint>

using rally::platform::FrameSample;
using rally::platform::PerfStats;
using rally::platform::PerfTotals;
using rally::platform::RollingMax;
using rally::platform::RunningTotal;

namespace {

constexpr std::int64_t kMs = 1'000'000;
constexpr std::int64_t kFrame60 = 16'666'667;

FrameSample sample(std::int64_t frame_ns, std::int64_t sim_ns, int steps, std::int64_t step_max,
                   std::int64_t render_ns, std::int64_t present_ns) {
    FrameSample s;
    s.frame_ns = frame_ns;
    s.sim_ns = sim_ns;
    s.steps = steps;
    s.step_max_ns = step_max;
    s.render_ns = render_ns;
    s.present_ns = present_ns;
    return s;
}

} // namespace

TEST_CASE("perf stats: running total keeps sum, count and max of groups") {
    RunningTotal total;
    CHECK(total.mean() == 0.0);
    total.add(10, 2, 7);
    total.add(5);
    total.add(0, 0, 100); // grupo vacío: no cuenta
    CHECK(total.sum == 15);
    CHECK(total.count == 3);
    CHECK(total.max == 7);
    CHECK(total.mean() == doctest::Approx(5.0));
}

TEST_CASE("perf stats: rolling max forgets values older than 2 s") {
    RollingMax rolling;
    CHECK(rolling.max(0) == 0);
    rolling.add(0, 50);
    rolling.add(500 * kMs, 20);
    CHECK(rolling.max(500 * kMs) == 50);
    // El cubo de t = 0 sigue dentro hasta que pasan 20 cubos de 100 ms.
    CHECK(rolling.max(1'999 * kMs) == 50);
    CHECK(rolling.max(2'000 * kMs) == 20);
    CHECK(rolling.max(2'500 * kMs) == 0);
}

TEST_CASE("perf stats: rolling max reuses buckets at any frame rate") {
    // 1000 fps durante 5 s con un pico a los 3 s y valores de 1 el resto.
    RollingMax rolling;
    for (std::int64_t t = 0; t < 5'000 * kMs; t += kMs) {
        rolling.add(t, t == 3'000 * kMs ? 9 : 1);
    }
    CHECK(rolling.max(4'999 * kMs) == 9);
    // 10 fps: un valor cada 100 ms.
    RollingMax slow;
    for (std::int64_t t = 0; t < 5'000 * kMs; t += 100 * kMs) {
        slow.add(t, t / kMs);
    }
    CHECK(slow.max(4'900 * kMs) == 4'900);
    // Un bucle que vuelve sobre un cubo viejo lo reinicia: no arrastra el 4900.
    slow.add(6'950 * kMs, 3);
    CHECK(slow.max(6'950 * kMs) == 3);
}

TEST_CASE("perf stats: publishes averages and 2 s maxima every 0.5 s at 60 fps") {
    PerfStats stats;
    std::int64_t now = 0;
    bool published = false;
    int frames = 0;
    // 0,5 s a 60 fps: 4 pasos de 0,1 ms con uno de 0,4 ms; render 2 ms, presentación 14 ms.
    while (!published) {
        now += kFrame60;
        FrameSample s = sample(kFrame60, 7 * kMs / 10, 4, 4 * kMs / 10, 2 * kMs, 14 * kMs);
        s.gpu_ns = frames % 2 == 0 ? 3 * kMs : -1; // la GPU solo llega uno de cada dos
        s.draw_calls = 30;
        published = stats.push(now, s);
        ++frames;
    }
    CHECK(frames == 31); // el primero abre el periodo; se cierra al pasar 0,5 s
    CHECK(stats.fps() == doctest::Approx(60.0).epsilon(0.001));
    CHECK(stats.frame().average() == doctest::Approx(kFrame60));
    CHECK(stats.sim().average() == doctest::Approx(0.7 * kMs));
    CHECK(stats.step().average() == doctest::Approx(0.175 * kMs));
    CHECK(stats.step().max() == 4 * kMs / 10);
    CHECK(stats.steps().average() == doctest::Approx(4.0));
    CHECK(stats.render().average() == doctest::Approx(2.0 * kMs));
    CHECK(stats.present().average() == doctest::Approx(14.0 * kMs));
    CHECK(stats.other().average() == doctest::Approx(kFrame60 - 16.7 * kMs));
    CHECK(stats.gpu().average() == doctest::Approx(3.0 * kMs));
    CHECK(stats.draw_calls().average() == doctest::Approx(30.0));
    CHECK(stats.draw_calls().max() == 30);
}

TEST_CASE("perf stats: a slow step stays in the maximum for 2 s and then goes") {
    PerfStats stats;
    std::int64_t now = 0;
    for (int frame = 0; frame < 60 * 5; ++frame) {
        now += kFrame60;
        const std::int64_t step_max = frame == 30 ? 5 * kMs : kMs / 10;
        stats.push(now, sample(kFrame60, 4 * step_max, 4, step_max, kMs, kMs));
        const double seconds = static_cast<double>(now) / 1e9;
        // Publicaciones entre 0,5 s y 2,4 s después del pico (en t = 0,52 s) lo ven.
        if (seconds > 1.1 && seconds < 2.4) {
            CHECK(stats.step().max() == 5 * kMs);
        }
        if (seconds > 3.0) {
            CHECK(stats.step().max() == kMs / 10);
        }
    }
}

TEST_CASE("perf stats: frames without a GPU result leave the GPU channel without data") {
    PerfStats stats;
    std::int64_t now = 0;
    for (int frame = 0; frame < 60; ++frame) {
        now += kFrame60;
        stats.push(now, sample(kFrame60, 0, 0, 0, kMs, kMs));
    }
    CHECK(stats.frame().has_data());
    CHECK_FALSE(stats.gpu().has_data());
    CHECK(stats.step().max() == 0);
}

TEST_CASE("perf stats: pushing frames does not allocate") {
    PerfStats stats;
    PerfTotals totals;
    std::int64_t now = 0;
    const std::uint64_t before = rally::tests::global_new_count();
    for (int frame = 0; frame < 1'000; ++frame) {
        now += kFrame60;
        FrameSample s = sample(kFrame60, kMs, 4, kMs / 2, kMs, kMs);
        s.gpu_ns = kMs;
        stats.push(now, s);
        totals.add(s);
    }
    CHECK(rally::tests::global_new_count() == before);
}

TEST_CASE("perf stats: totals give mean and maximum over a long measurement") {
    PerfTotals totals;
    totals.add(sample(10 * kMs, 2 * kMs, 4, kMs, 3 * kMs, 4 * kMs));
    FrameSample slow = sample(30 * kMs, 6 * kMs, 4, 3 * kMs, 5 * kMs, 18 * kMs);
    slow.gpu_ns = 7 * kMs;
    slow.draw_calls = 20'003;
    slow.instances = 0;
    totals.add(slow);
    CHECK(totals.frames == 2);
    CHECK(totals.frame.mean() == doctest::Approx(20.0 * kMs));
    CHECK(totals.frame.max == 30 * kMs);
    CHECK(totals.step.mean() == doctest::Approx(1.0 * kMs)); // 8 ms en 8 pasos
    CHECK(totals.step.max == 3 * kMs);
    CHECK(totals.other.mean() == doctest::Approx(1.0 * kMs)); // 1 y 1 ms
    CHECK(totals.gpu.count == 1);
    CHECK(totals.gpu.max == 7 * kMs);
    CHECK(totals.draw_calls.max == 20'003);
}
