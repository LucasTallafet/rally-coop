#include "net/protocol.h"
#include "net/state_buffer.h"
#include "new_counter.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using namespace rally::net;

constexpr double kPi = 3.14159265358979323846;
constexpr double kRadius = 50.0; // m
constexpr double kSpeed = 20.0;  // m/s
constexpr double kOmega = kSpeed / kRadius;

// Coche en un círculo de R 50 m a 20 m/s alrededor del origen.
struct Truth {
    double x, z, theta;
};

Truth circle_at(double step) {
    const double theta = kOmega * step / 240.0;
    return Truth{kRadius * std::cos(theta), kRadius * std::sin(theta), theta};
}

CarState circle_state(std::uint32_t step, std::uint8_t generation = 0) {
    const Truth t = circle_at(step);
    CarState s;
    s.generation = generation;
    s.step = step;
    s.x = t.x;
    s.z = t.z;
    s.vx = static_cast<float>(-kSpeed * std::sin(t.theta));
    s.vz = static_cast<float>(kSpeed * std::cos(t.theta));
    // Giro alrededor de y.
    s.qx = 0;
    s.qy = std::sin(-t.theta / 2.0);
    s.qz = 0;
    s.qw = std::cos(t.theta / 2.0);
    return s;
}

// Por el cable: lo que ve el copiloto.
CarState through_wire(const CarState& in) {
    std::array<std::uint8_t, kCarStateBytes> bytes{};
    encode(in, bytes);
    CarState out;
    REQUIRE(decode(bytes, out));
    return out;
}

double dist(const Pose& p, double x, double z) {
    return std::hypot(p.x - x, p.z - z);
}

CarState line_state(std::uint32_t step, double v, std::uint8_t generation = 0) {
    CarState s;
    s.generation = generation;
    s.step = step;
    s.x = v * step / 240.0;
    s.vx = static_cast<float>(v);
    return s;
}

} // namespace

TEST_CASE("net_buffer: interpolation without simulated network follows the circle within 1 cm") {
    StateBuffer buffer;
    buffer.reset(0);
    std::uint32_t next_step = 0;
    double worst = 0.0;
    int samples = 0;
    for (int frame = 0; frame < 60 * 20; ++frame) {
        const double now = frame / 60.0;
        while (next_step / 240.0 + 0.005 <= now) {
            buffer.push(through_wire(circle_state(next_step)), next_step / 240.0 + 0.005);
            next_step += 8;
        }
        Pose pose;
        if (!buffer.sample(now, pose) || now < 3.0) {
            continue;
        }
        const Truth t = circle_at(buffer.target_step(now));
        worst = std::max(worst, dist(pose, t.x, t.z));
        CHECK_FALSE(buffer.stats(now).extrapolating);
        ++samples;
    }
    std::printf("net_buffer: sin red simulada, error maximo %.2f mm en %d muestras\n", worst * 1000.0,
                samples);
    CHECK(samples > 900);
    CHECK(worst <= 0.01);
}

TEST_CASE("net_buffer: simulated medium network (100 +- 30 ms, 5 % loss) for 60 s") {
    StateBuffer buffer;
    buffer.reset(0);
    LinkSimulator link;
    link.set(Impairment{0.100, 0.030, 0.05}, 424242);

    const std::uint32_t total_steps = 60 * 240;
    std::uint32_t next_step = 0;
    std::vector<std::uint32_t> delivered;
    double worst_interp = 0.0;
    double worst_extrap = 0.0;
    int interp_samples = 0;
    int extrap_samples = 0;
    int pre_first_samples = 0;
    double prev_angle = 0.0;
    bool have_prev = false;
    double worst_backwards = 0.0;
    for (int frame = 0; frame <= 60 * 60; ++frame) {
        const double now = frame / 60.0;
        while (next_step <= total_steps && next_step / 240.0 + 0.005 <= now) {
            link.submit(through_wire(circle_state(next_step)), next_step / 240.0 + 0.005);
            next_step += 8;
        }
        CarState s;
        double release = 0.0;
        while (link.pop_ready(now, s, release)) {
            buffer.push(s, release);
            delivered.push_back(s.step);
        }
        Pose pose;
        if (delivered.empty() || !buffer.sample(now, pose)) {
            continue;
        }
        const double target = buffer.target_step(now);
        const Truth t = circle_at(target);
        const double err = dist(pose, t.x, t.z);
        // Antes del primer estado no hay nada que interpolar: se queda en él.
        const bool before_first = target < static_cast<double>(*std::min_element(delivered.begin(), delivered.end()));
        if (before_first) {
            ++pre_first_samples;
        } else if (buffer.stats(now).extrapolating) {
            worst_extrap = std::max(worst_extrap, err);
            ++extrap_samples;
        } else {
            worst_interp = std::max(worst_interp, err);
            ++interp_samples;
        }
        // Ángulo recorrido, sin saltos de 2π (desde el primer estado).
        double angle = std::atan2(pose.z, pose.x);
        if (have_prev) {
            while (angle < prev_angle - kPi) {
                angle += 2.0 * kPi;
            }
            while (angle > prev_angle + kPi) {
                angle -= 2.0 * kPi;
            }
            worst_backwards = std::max(worst_backwards, prev_angle - angle);
        }
        prev_angle = angle;
        have_prev = true;
    }
    // Hueco más largo: estados consecutivos (de 8 en 8) que no llegaron.
    std::sort(delivered.begin(), delivered.end());
    int longest_gap = 0;
    for (std::size_t i = 1; i < delivered.size(); ++i) {
        longest_gap = std::max(longest_gap, static_cast<int>((delivered[i] - delivered[i - 1]) / 8) - 1);
    }
    const BufferStats stats = buffer.stats(60.0);
    std::printf("net_buffer: red media: interpolando %d muestras, error max %.2f cm; extrapolando %d "
                "muestras (y %d antes del primer estado), error max %.2f cm; hueco mas largo %d estados; perdidos por la red %llu, "
                "tarde %llu, missing %llu; retroceso maximo del angulo %.2e rad\n",
                interp_samples, worst_interp * 100.0, extrap_samples, pre_first_samples,
                worst_extrap * 100.0, longest_gap, static_cast<unsigned long long>(link.dropped()),
                static_cast<unsigned long long>(stats.late),
                static_cast<unsigned long long>(stats.missing), worst_backwards);
    CHECK(interp_samples > 2000);
    CHECK(worst_interp <= 0.03);
    CHECK(worst_extrap <= 0.08);
    CHECK(worst_backwards <= 0.0);
    CHECK(stats.late > 0); // con jitter hay estados desordenados: el contador mide
}

TEST_CASE("net_buffer: a reset state jumps exactly, without mixing") {
    StateBuffer buffer;
    buffer.reset(3);
    CarState a = line_state(0, 10.0, 3);
    CarState b = line_state(8, 10.0, 3);
    b.x = 500.0; // recolocado lejos
    b.reset = true;
    b.vx = 0.0f;
    CarState c = line_state(16, 0.0, 3);
    c.x = 500.0;
    c.vx = 0.0f;
    // Llegadas a la hora exacta de cada paso: offset = 0, objetivo = now·240 − 24.
    buffer.push(a, 0.0);
    buffer.push(b, 8.0 / 240.0);
    buffer.push(c, 16.0 / 240.0);
    Pose pose;
    // Antes del paso de b: a seguido con su velocidad, sin mezcla.
    for (double target : {1.0, 4.0, 7.5}) {
        const double now = (target + 24.0) / 240.0;
        REQUIRE(buffer.sample(now, pose));
        CHECK(pose.x == doctest::Approx(10.0 * target / 240.0).epsilon(1e-9));
    }
    // En el paso de b: b exacto.
    REQUIRE(buffer.sample((8.0 + 24.0) / 240.0, pose));
    CHECK(pose.x == 500.0);
    // Entre b y c: se mezcla con c (sin pasar por a).
    REQUIRE(buffer.sample((12.0 + 24.0) / 240.0, pose));
    CHECK(pose.x == doctest::Approx(500.0));

    // Con estados perdidos entre a y b (hueco de 0,2 s): a se extrapola como mucho 0,1 s.
    StateBuffer gap;
    gap.reset(3);
    CarState far = line_state(48, 0.0, 3);
    far.x = 500.0;
    far.reset = true;
    gap.push(a, 0.0);
    gap.push(far, 48.0 / 240.0);
    REQUIRE(gap.sample((30.0 + 24.0) / 240.0, pose)); // 30 pasos tras a, más de 24
    CHECK(pose.x == doctest::Approx(10.0 * 0.1));
    REQUIRE(gap.sample((48.0 + 24.0) / 240.0, pose));
    CHECK(pose.x == 500.0);
}

TEST_CASE("net_buffer: a new world drops states of the old generation, also across 255 to 0") {
    StateBuffer buffer;
    // Sin reset no acepta nada: 0 es una generación real.
    buffer.push(line_state(0, 1.0, 0), 0.0);
    Pose pose;
    CHECK_FALSE(buffer.sample(1.0, pose));

    buffer.reset(255);
    for (std::uint32_t step = 0; step <= 16; step += 8) {
        buffer.push(line_state(step, 1.0, 255), step / 240.0);
    }
    CHECK(buffer.stats(0.1).received == 3);
    // Mundo nuevo: la generación pasa de 255 a 0.
    buffer.reset(0);
    CHECK_FALSE(buffer.sample(1.0, pose));
    buffer.push(line_state(24, 1.0, 255), 0.1); // viejo: fuera
    CHECK_FALSE(buffer.sample(1.0, pose));
    buffer.push(line_state(0, 1.0, 0), 0.0);
    CHECK(buffer.sample(1.0, pose));
    CHECK(buffer.stats(1.0).received == 1);
    CHECK(buffer.newest_step() == 0);
    // Un duplicado no cuenta.
    buffer.push(line_state(0, 1.0, 0), 0.0);
    CHECK(buffer.stats(1.0).received == 1);
}

TEST_CASE("net_buffer: without new states the car extrapolates 100 ms and then stays") {
    StateBuffer buffer;
    buffer.reset(0);
    for (std::uint32_t step = 0; step <= 240; step += 8) {
        buffer.push(line_state(step, 20.0), step / 240.0);
    }
    const double last_x = 20.0 * 1.0;
    Pose pose;
    // Objetivo = último paso + n: now = (240 + n + 24) / 240.
    auto at = [&](double n) {
        REQUIRE(buffer.sample((240.0 + n + 24.0) / 240.0, pose));
        return pose.x;
    };
    CHECK(at(0.0) == doctest::Approx(last_x));
    CHECK(at(12.0) == doctest::Approx(last_x + 20.0 * 0.05));
    CHECK(at(24.0) == doctest::Approx(last_x + 20.0 * 0.1));
    CHECK(at(60.0) == doctest::Approx(last_x + 20.0 * 0.1));
    CHECK(at(2400.0) == doctest::Approx(last_x + 20.0 * 0.1));
    CHECK(buffer.stats((240.0 + 2400.0 + 24.0) / 240.0).extrapolating);
}

TEST_CASE("net_buffer: before any state there is nothing to sample") {
    StateBuffer buffer;
    Pose pose;
    CHECK_FALSE(buffer.sample(0.0, pose));
    buffer.reset(0);
    CHECK_FALSE(buffer.sample(1.0, pose));
    CHECK(buffer.stats(1.0).received == 0);
}

TEST_CASE("net_buffer: missing counts the cadence slots the host skipped") {
    StateBuffer buffer;
    buffer.reset(0);
    buffer.push(line_state(8, 1.0), 0.0);
    buffer.push(line_state(23, 1.0), 0.1); // un fotograma de 15 pasos: 8 -> 23 no pierde nada
    CHECK(buffer.stats(0.2).missing == 0);
    buffer.push(line_state(40, 1.0), 0.2); // índice 5: faltan el 3 y el 4 ... el 2 llegó como 23
    CHECK(buffer.stats(0.3).missing == 2);
    buffer.push(line_state(32, 1.0), 0.3); // llega tarde el del índice 4
    CHECK(buffer.stats(0.3).missing == 1);
    CHECK(buffer.stats(0.3).late == 1);
}

TEST_CASE("net_buffer: the offset recovers after the host pauses and resumes") {
    for (double pause : {0.3, 1.0, 3.0}) {
        StateBuffer buffer;
        buffer.reset(0);
        const double t_pause = 5.0;
        // Paso del host en el instante t (el host se para pause segundos).
        auto host_step = [&](double t) {
            if (t < t_pause) {
                return 240.0 * t;
            }
            if (t < t_pause + pause) {
                return 240.0 * t_pause;
            }
            return 240.0 * (t - pause);
        };
        std::uint32_t next_step = 0;
        double resume_at = t_pause + pause;
        double first_interp_after_resume = -1.0;
        double prev_target = 0.0;
        double worst_back = 0.0;
        bool have_prev = false;
        for (int frame = 0; frame < 60 * 14; ++frame) {
            const double now = frame / 60.0;
            // Llegan los estados cuyo paso ya hizo el host (+ 5 ms de red).
            while (host_step(now - 0.005) >= next_step) {
                // hora a la que el host cruzó ese paso
                double t_cross = next_step / 240.0;
                if (t_cross >= t_pause && next_step > 240.0 * t_pause) {
                    t_cross += pause;
                }
                buffer.push(line_state(next_step, 20.0), t_cross + 0.005);
                next_step += 8;
            }
            Pose pose;
            if (!buffer.sample(now, pose)) {
                continue;
            }
            const bool extrap = buffer.stats(now).extrapolating;
            if (now >= resume_at && first_interp_after_resume < 0.0 && !extrap) {
                first_interp_after_resume = now - resume_at;
            }
            if (first_interp_after_resume >= 0.0) {
                const double target = buffer.target_step(now);
                if (have_prev) {
                    worst_back = std::max(worst_back, prev_target - target);
                }
                prev_target = target;
                have_prev = true;
            }
        }
        std::printf("net_buffer: pausa del host %.1f s: vuelve a interpolar %.3f s despues de "
                    "reanudar; mayor retroceso del objetivo despues: %.2f pasos\n",
                    pause, first_interp_after_resume, worst_back);
        CHECK(first_interp_after_resume >= 0.0);
        CHECK(first_interp_after_resume <= 0.25);
        CHECK(worst_back <= 1.0);
    }
}

TEST_CASE("net_buffer: push, sample and stats do not allocate") {
    StateBuffer buffer;
    buffer.reset(0);
    const std::uint64_t before = rally::tests::global_new_count();
    Pose pose;
    double sink = 0.0;
    for (int i = 0; i < 10000; ++i) {
        const std::uint32_t step = static_cast<std::uint32_t>(i) * 8u;
        CarState s = line_state(step, 20.0);
        s.reset = (i % 997) == 0;
        buffer.push(s, step / 240.0);
        const double now = step / 240.0 + 0.01;
        if (buffer.sample(now, pose)) {
            sink += pose.x;
        }
        sink += static_cast<double>(buffer.stats(now).host_step);
    }
    // Los desordenados y los duplicados también.
    buffer.push(line_state(8, 20.0), 1.0);
    buffer.push(line_state(79992, 20.0), 1.0);
    CHECK(rally::tests::global_new_count() == before);
    CHECK(sink > 0.0);
}

TEST_CASE("net_link: the simulator drops, delays and keeps order of release") {
    LinkSimulator link;
    // Sin defectos: pasa tal cual en el mismo instante.
    CarState out;
    double release = 0.0;
    link.submit(line_state(8, 1.0), 2.0);
    REQUIRE(link.pop_ready(2.0, out, release));
    CHECK(out.step == 8);
    CHECK(release == 2.0);
    CHECK_FALSE(link.pop_ready(2.0, out, release));

    link.set(Impairment{0.100, 0.030, 0.05}, 7);
    int delivered = 0;
    double min_delay = 1e9;
    double max_delay = 0.0;
    const int n = 10000;
    for (int i = 0; i < n; ++i) {
        const double t = i * 0.01;
        link.submit(line_state(static_cast<std::uint32_t>(i), 1.0), t);
        // Se vacía cada 10 ms: nada se acumula.
        double last_release = -1.0;
        while (link.pop_ready(t + 1.0, out, release)) {
            CHECK(release >= last_release);
            last_release = release;
            const double delay = release - out.step * 0.01;
            min_delay = std::min(min_delay, delay);
            max_delay = std::max(max_delay, delay);
            ++delivered;
        }
    }
    CHECK(min_delay >= 0.070 - 1e-9);
    CHECK(max_delay <= 0.130 + 1e-9);
    const double loss = 1.0 - static_cast<double>(delivered) / n;
    CHECK(loss > 0.04);
    CHECK(loss < 0.06);
}
