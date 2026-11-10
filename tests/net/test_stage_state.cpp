// s, t y «en carretera» del host en el búfer del copiloto (lote 6, red-tramo): la regla de
// extrapolación y el recorrido de lazadas conducido, sin red y con red media.

#include "net/protocol.h"
#include "net/state_buffer.h"
#include "sim/jolt_runtime.h"
#include "stage_net.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using namespace rally::net;

// Estado con s = s_per_step · paso y t = t_per_step · paso, sin pasar por el cable.
CarState linear(std::uint32_t step, double s, double t, std::uint8_t count = 0) {
    CarState c;
    c.step = step;
    c.s = s;
    c.t = t;
    c.reset_count = count;
    c.on_road = true;
    return c;
}

double at_target(double target) { return (target + 24.0) / 240.0; }

CarState through_wire(const CarState& in) {
    std::array<std::uint8_t, kCarStateBytes> bytes{};
    encode(in, bytes);
    CarState out;
    REQUIRE(decode(bytes, out));
    return out;
}

} // namespace

TEST_CASE("net_stage: s and t extrapolate 100 ms with the previous pair and then stay") {
    StateBuffer buffer;
    buffer.reset(0);
    buffer.push(linear(0, 0.0, 0.0), 0.0);
    buffer.push(linear(8, 0.8, 0.08), 8.0 / 240.0);
    buffer.push(linear(16, 1.6, 0.16), 16.0 / 240.0);
    Pose pose;
    SampledState e;
    REQUIRE(buffer.sample(at_target(28.0), pose, e)); // 12 pasos más allá del último
    CHECK(e.s == doctest::Approx(2.8));
    CHECK(e.t == doctest::Approx(0.28));
    CHECK(e.on_road);
    REQUIRE(buffer.sample(at_target(40.0), pose, e)); // 24 pasos: el límite
    CHECK(e.s == doctest::Approx(4.0));
    REQUIRE(buffer.sample(at_target(60.0), pose, e)); // más allá: quietos
    CHECK(e.s == doctest::Approx(4.0));
    CHECK(e.t == doctest::Approx(0.40));
    REQUIRE(buffer.sample(at_target(2400.0), pose, e));
    CHECK(e.s == doctest::Approx(4.0));
    // Antes del primero: el primero.
    REQUIRE(buffer.sample(at_target(-5.0), pose, e));
    CHECK(e.s == doctest::Approx(0.0));
    // Entre dos: interpolado por paso.
    REQUIRE(buffer.sample(at_target(12.0), pose, e));
    CHECK(e.s == doctest::Approx(1.2));
    CHECK(e.t == doctest::Approx(0.12));
}

TEST_CASE("net_stage: without a usable previous state s and t stay") {
    Pose pose;
    SampledState e;
    SUBCASE("a single state") {
        StateBuffer buffer;
        buffer.reset(0);
        buffer.push(linear(8, 5.0, 1.0), 8.0 / 240.0);
        REQUIRE(buffer.sample(at_target(20.0), pose, e));
        CHECK(e.s == doctest::Approx(5.0));
        CHECK(e.t == doctest::Approx(1.0));
    }
    SUBCASE("the previous state has another reset counter") {
        StateBuffer buffer;
        buffer.reset(0);
        buffer.push(linear(0, 500.0, 0.0, 0), 0.0);
        buffer.push(linear(8, 1.0, 0.0, 1), 8.0 / 240.0);
        REQUIRE(buffer.sample(at_target(20.0), pose, e));
        CHECK(e.s == doctest::Approx(1.0)); // ni una pendiente de 500 m ni del salto
    }
}

TEST_CASE("net_stage: the slope pair spans at least 8 steps") {
    // Estados en 0, 15 y 16: la pareja 15 -> 16 daría una pendiente de 0,1·... distinta; la regla
    // usa la más nueva a 8 pasos o más del último (el paso 0).
    StateBuffer buffer;
    buffer.reset(0);
    buffer.push(linear(0, 0.0, 0.0), 0.0);
    buffer.push(linear(15, 3.0, 0.0), 15.0 / 240.0);
    buffer.push(linear(16, 3.1, 0.0), 16.0 / 240.0);
    Pose pose;
    SampledState e;
    REQUIRE(buffer.sample(at_target(28.0), pose, e));
    CHECK(e.s == doctest::Approx(3.1 + 3.1 / 16.0 * 12.0));
}

TEST_CASE("net_stage: with a reset between two states the earlier follows its own pair") {
    StateBuffer buffer;
    buffer.reset(0);
    buffer.push(linear(0, 0.0, 0.0, 0), 0.0);
    buffer.push(linear(8, 0.8, 0.0, 0), 8.0 / 240.0);
    buffer.push(linear(16, 500.0, 40.0, 1), 16.0 / 240.0);
    Pose pose;
    SampledState e;
    REQUIRE(buffer.sample(at_target(12.0), pose, e));
    CHECK(e.s == doctest::Approx(1.2)); // 0,8 + 0,1 · 4 pasos; nada hacia los 500 m
    CHECK(e.t == doctest::Approx(0.0));
    REQUIRE(buffer.sample(at_target(16.0), pose, e));
    CHECK(e.s == doctest::Approx(500.0));
    CHECK(e.t == doctest::Approx(40.0));
}

namespace {

struct StageRunErrors {
    double interp = 0, extrap = 0, backwards = 0;
    int interp_samples = 0, extrap_samples = 0;
};

// s del host en un paso fraccional: interpolación lineal entre pasos enteros.
double host_s_at(const rally::tests::DrivenRun& run, double step) {
    const double clamped = std::clamp(step, 1.0, static_cast<double>(run.states.size()) - 1.0);
    const std::size_t i = static_cast<std::size_t>(clamped);
    const double f = clamped - static_cast<double>(i);
    const double a = run.states[i - 1].s;
    const double b = run.states[i].s;
    return a + (b - a) * f;
}

// Alimenta el búfer con los estados del host cada 8 pasos (por el cable) a través de la red
// simulada y compara cada muestra con la s del host en el paso objetivo.
StageRunErrors play(const rally::tests::DrivenRun& run, const Impairment& impairment,
                    std::uint64_t seed) {
    StateBuffer buffer;
    buffer.reset(0);
    LinkSimulator link;
    link.set(impairment, seed);
    const int total = static_cast<int>(run.states.size());
    std::uint32_t next_step = 8;
    StageRunErrors errors;
    double prev_s = 0.0;
    bool have_prev = false;
    for (int frame = 0; frame <= (total / 240 + 1) * 60; ++frame) {
        const double now = frame / 60.0;
        while (next_step < static_cast<std::uint32_t>(total) && next_step / 240.0 + 0.005 <= now) {
            link.submit(through_wire(run.states[next_step - 1]), next_step / 240.0 + 0.005);
            next_step += 8;
        }
        CarState s;
        double release = 0.0;
        while (link.pop_ready(now, s, release)) {
            buffer.push(s, release);
        }
        Pose pose;
        SampledState e;
        if (buffer.size() < 4 || !buffer.sample(now, pose, e)) {
            continue;
        }
        const double target = buffer.target_step(now);
        if (target < static_cast<double>(run.states.front().step) + 16.0 ||
            target > static_cast<double>(total - 1)) {
            continue;
        }
        const double err = std::abs(e.s - host_s_at(run, target));
        if (buffer.stats(now).extrapolating) {
            errors.extrap = std::max(errors.extrap, err);
            ++errors.extrap_samples;
        } else {
            errors.interp = std::max(errors.interp, err);
            ++errors.interp_samples;
        }
        if (have_prev) {
            errors.backwards = std::max(errors.backwards, prev_s - e.s);
        }
        prev_s = e.s;
        have_prev = true;
    }
    return errors;
}

} // namespace

TEST_CASE("net_stage: s of the buffer follows the host s over the lazadas hairpins") {
    const rally::sim::JoltRuntime runtime;
    // Pasada conducida desde 5 m antes de la salida a 80 km/h como mucho (con la velocidad que
    // deje la curva); en Debug, la versión corta.
#ifdef NDEBUG
    const int steps = 60 * 240;
#else
    const int steps = 20 * 240;
#endif
    const rally::tests::DrivenRun run = rally::tests::drive_lazadas(steps, 80.0 / 3.6);
    // Sin recolocados ni atajos: «sin retroceder nunca» mide algo.
    REQUIRE(run.resets == 0);
    REQUIRE(run.shortcuts == 0);
    double host_backwards = 0.0;
    for (std::size_t i = 1; i < run.states.size(); ++i) {
        host_backwards = std::max(host_backwards, run.states[i - 1].s - run.states[i].s);
    }
    std::printf("net_stage: la s del host llega a %.1f m en %d pasos, retroceso maximo %.4f m\n",
                run.states.back().s, steps, host_backwards);
    REQUIRE(run.states.back().s > 150.0);

    const StageRunErrors clean = play(run, Impairment{0.0, 0.0, 0.0}, 1);
    std::printf("net_stage: sin red: interpolando %d muestras, error max %.3f cm; extrapolando %d, "
                "%.3f cm; retroceso %.4f m\n",
                clean.interp_samples, clean.interp * 100.0, clean.extrap_samples,
                clean.extrap * 100.0, clean.backwards);
    CHECK(clean.interp_samples > 500);
    CHECK(clean.interp <= 0.01);
    CHECK(clean.backwards <= host_backwards);

    const StageRunErrors medium = play(run, Impairment{0.100, 0.030, 0.05}, 424242);
    std::printf("net_stage: red media: interpolando %d muestras, error max %.3f cm; extrapolando "
                "%d, %.3f cm; retroceso %.4f m\n",
                medium.interp_samples, medium.interp * 100.0, medium.extrap_samples,
                medium.extrap * 100.0, medium.backwards);
    CHECK(medium.interp_samples > 500);
    CHECK(medium.extrap_samples > 0);
    CHECK(medium.interp <= 0.03);
    CHECK(medium.extrap <= 0.08);
    CHECK(medium.backwards <= host_backwards);
}
