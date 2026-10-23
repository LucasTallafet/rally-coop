// Contador de recolocaciones del búfer del copiloto (lote 3, arreglo red-recolocado) y los
// campos de dirección y turbo que salen de sample. Sucesor, en el lado del búfer, del bit de
// recolocado de test_reset_bit.cpp (que prueba el lado del host).

#include "net/state_buffer.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace {

using namespace rally::net;

// Coche parado en `x`, con el contador dado; pasa por el cable.
CarState counted(std::uint32_t step, double x, std::uint8_t count, std::uint8_t generation = 0) {
    CarState s;
    s.generation = generation;
    s.step = step;
    s.x = x;
    s.reset_count = count;
    std::array<std::uint8_t, kCarStateBytes> bytes{};
    encode(s, bytes);
    CarState out;
    REQUIRE(decode(bytes, out));
    return out;
}

double at_target(double target) { return (target + 24.0) / 240.0; } // llegadas a su paso

} // namespace

TEST_CASE("net_reset_counter: a lost reset state still shows as a clean jump, never half way") {
    // a (contador 0) y c (contador 1); el estado b del medio, el primero con el contador nuevo,
    // se perdió.
    StateBuffer buffer;
    buffer.reset(0);
    buffer.push(counted(0, 10.0, 0), 0.0);
    buffer.push(counted(16, 500.0, 1), 16.0 / 240.0);
    Pose pose;
    for (double target = 0.5; target < 16.0; target += 0.5) {
        REQUIRE(buffer.sample(at_target(target), pose));
        CHECK(pose.x == doctest::Approx(10.0)); // a, ni un milímetro hacia c
    }
    REQUIRE(buffer.sample(at_target(16.0), pose));
    CHECK(pose.x == doctest::Approx(500.0));
}

TEST_CASE("net_reset_counter: the counter wraps from 255 to 0 as a change") {
    StateBuffer buffer;
    buffer.reset(0);
    buffer.push(counted(0, 10.0, 255), 0.0);
    buffer.push(counted(8, 500.0, 0), 8.0 / 240.0);
    buffer.push(counted(16, 600.0, 0), 16.0 / 240.0);
    Pose pose;
    REQUIRE(buffer.sample(at_target(4.0), pose));
    CHECK(pose.x == doctest::Approx(10.0));
    REQUIRE(buffer.sample(at_target(12.0), pose)); // entre dos del mismo contador: se mezclan
    CHECK(pose.x == doctest::Approx(550.0));
}

TEST_CASE("net_reset_counter: the interpolated pair decides, not the last state received") {
    Pose pose;
    SUBCASE("a late state of the new count does not make a false blend") {
        StateBuffer buffer;
        buffer.reset(0);
        buffer.push(counted(0, 10.0, 0), 0.0);
        buffer.push(counted(16, 500.0, 1), 16.0 / 240.0);
        buffer.push(counted(8, 500.0, 1), 17.0 / 240.0); // llega tarde
        for (double target = 0.5; target < 8.0; target += 0.5) {
            REQUIRE(buffer.sample(at_target(target), pose));
            CHECK(pose.x == doctest::Approx(10.0));
        }
        REQUIRE(buffer.sample(at_target(12.0), pose));
        CHECK(pose.x == doctest::Approx(500.0));
    }
    SUBCASE("a newer reset does not break the blend of an older pair") {
        StateBuffer buffer;
        buffer.reset(0);
        buffer.push(counted(0, 10.0, 0), 0.0);
        buffer.push(counted(8, 20.0, 0), 8.0 / 240.0);
        buffer.push(counted(16, 500.0, 1), 16.0 / 240.0);
        REQUIRE(buffer.sample(at_target(4.0), pose));
        CHECK(pose.x == doctest::Approx(15.0));
    }
}

TEST_CASE("net_reset_counter: a new generation with other counters blends normally") {
    StateBuffer buffer;
    buffer.reset(1);
    buffer.push(counted(0, 10.0, 7, 1), 0.0);
    buffer.push(counted(8, 500.0, 8, 1), 8.0 / 240.0);
    buffer.reset(2); // mundo nuevo: el contador del host vuelve a empezar
    buffer.push(counted(0, 100.0, 0, 2), 0.0);
    buffer.push(counted(8, 200.0, 0, 2), 8.0 / 240.0);
    Pose pose;
    REQUIRE(buffer.sample(at_target(4.0), pose));
    CHECK(pose.x == doctest::Approx(150.0)); // el mundo nuevo, a la mitad
}

TEST_CASE("net_reset_counter: sample gives the steering blended and the rest from the nearest") {
    StateBuffer buffer;
    buffer.reset(0);
    CarState a = counted(0, 0.0, 0);
    a.steer = 0.2f;
    a.boost_bar = 1.0f;
    a.gear = 2;
    a.rpm = 3000.0f;
    a.surface = {{0, 0, 0, 0}};
    a.compression = {{0.01f, 0.01f, 0.01f, 0.01f}};
    CarState b = counted(8, 0.0, 0);
    b.steer = 0.6f;
    b.boost_bar = 1.4f;
    b.gear = 3;
    b.rpm = 5000.0f;
    b.surface = {{1, 1, -1, -1}};
    b.compression = {{0.05f, 0.05f, 0.05f, 0.05f}};
    buffer.push(a, 0.0);
    buffer.push(b, 8.0 / 240.0);
    Pose pose;
    SampledState e;
    REQUIRE(buffer.sample(at_target(4.0), pose, e));
    CHECK(e.steer == doctest::Approx(0.4f));
    REQUIRE(buffer.sample(at_target(2.0), pose, e)); // t = 0,25: más cerca de a
    CHECK(e.steer == doctest::Approx(0.3f));
    CHECK(e.gear == 2);
    CHECK(e.rpm == 3000.0f);
    CHECK(e.boost_bar == 1.0f);
    CHECK(e.surface[0] == 0);
    CHECK(e.compression[0] == doctest::Approx(0.01f));
    REQUIRE(buffer.sample(at_target(6.0), pose, e)); // t = 0,75: más cerca de b
    CHECK(e.gear == 3);
    CHECK(e.rpm == 5000.0f);
    CHECK(e.boost_bar == 1.4f);
    CHECK(e.surface[2] == -1);
    CHECK(e.compression[3] == doctest::Approx(0.05f));
    // Antes del primero y después del último: el primero y el último.
    REQUIRE(buffer.sample(at_target(-5.0), pose, e));
    CHECK(e.gear == 2);
    REQUIRE(buffer.sample(at_target(100.0), pose, e));
    CHECK(e.gear == 3);
    CHECK(e.steer == doctest::Approx(0.6f));
}

TEST_CASE("net_reset_counter: with a reset between two states everything shown is the earlier") {
    StateBuffer buffer;
    buffer.reset(0);
    CarState a = counted(0, 10.0, 0);
    a.steer = 0.2f;
    a.boost_bar = 1.0f;
    a.gear = 2;
    a.rpm = 3000.0f;
    CarState b = counted(8, 500.0, 1);
    b.steer = -0.6f;
    b.boost_bar = 0.0f;
    b.gear = 1;
    b.rpm = 1000.0f;
    buffer.push(a, 0.0);
    buffer.push(b, 8.0 / 240.0);
    buffer.push(counted(16, 500.0, 1), 16.0 / 240.0);
    Pose pose;
    SampledState e;
    REQUIRE(buffer.sample(at_target(6.0), pose, e)); // t = 0,75
    CHECK(pose.x == doctest::Approx(10.0));
    CHECK(e.steer == doctest::Approx(0.2f));
    CHECK(e.gear == 2);
    CHECK(e.rpm == 3000.0f);
    CHECK(e.boost_bar == 1.0f);
    REQUIRE(buffer.sample(at_target(8.0), pose, e));
    CHECK(pose.x == doctest::Approx(500.0));
    CHECK(e.gear == 1);
    CHECK(e.steer == doctest::Approx(-0.6f));
}

TEST_CASE("net_reset_counter: medium network with a reset every second, no half way pose") {
    // Coche parado que salta 500 m cada segundo entre dos puntos: toda muestra tiene que estar
    // a menos de 1 m de uno de los dos, nunca a mitad de camino.
    StateBuffer buffer;
    buffer.reset(0);
    LinkSimulator link;
    link.set(Impairment{0.100, 0.030, 0.05}, 99);
    std::uint32_t next_step = 0;
    int resets_sent = 0;
    int lost_first_new = 0;
    int samples = 0;
    double worst_off = 0.0;
    std::uint8_t last_count = 0;
    for (int frame = 0; frame < 60 * 60; ++frame) {
        const double now = frame / 60.0;
        while (next_step / 240.0 <= now) {
            const std::uint32_t sec = next_step / 240;
            const CarState s =
                counted(next_step, (sec & 1u) ? 500.0 : 0.0, static_cast<std::uint8_t>(sec));
            const bool first_new = s.reset_count != last_count;
            last_count = s.reset_count;
            const std::uint64_t dropped_before = link.dropped();
            link.submit(s, next_step / 240.0);
            if (first_new) {
                ++resets_sent;
                lost_first_new += link.dropped() > dropped_before ? 1 : 0;
            }
            next_step += 8;
        }
        CarState out;
        double release = 0.0;
        while (link.pop_ready(now, out, release)) {
            buffer.push(out, release);
        }
        Pose pose;
        if (now < 3.0 || !buffer.sample(now, pose)) {
            continue;
        }
        worst_off = std::max(worst_off, std::min(std::abs(pose.x), std::abs(pose.x - 500.0)));
        ++samples;
    }
    std::printf("net_reset_counter: red media, recolocado cada s: %d recolocados, %d con el primer "
                "estado perdido, peor distancia a una de las dos posiciones %.3f m en %d "
                "muestras\n",
                resets_sent, lost_first_new, worst_off, samples);
    CHECK(resets_sent >= 58);
    CHECK(lost_first_new >= 1); // si no, el test no mide el caso del fallo
    CHECK(samples > 3000);
    CHECK(worst_off <= 1.0);
}
