#include "car_worlds.h"
#include "net/car_state.h"
#include "sim/jolt_runtime.h"
#include "sim/pcg32.h"
#include "sim/sim_input.h"
#include "sim/sim_world.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>

namespace {

using namespace rally::net;
using rally::sim::Pcg32;

double uniform(Pcg32& rng, double lo, double hi) {
    // 24 bits por llamada; dos llamadas dan ~48 bits (de sobra para metros y mm).
    const double u = (static_cast<double>(rng.next_u32() >> 8) * 16777216.0 +
                      static_cast<double>(rng.next_u32() >> 8)) /
                     281474976710656.0;
    return lo + (hi - lo) * u;
}

CarState random_state(Pcg32& rng) {
    CarState s;
    s.generation = static_cast<std::uint8_t>(rng.bounded(256));
    s.step = rng.next_u32();
    s.x = uniform(rng, -20000.0, 20000.0);
    s.y = uniform(rng, -20000.0, 20000.0);
    s.z = uniform(rng, -20000.0, 20000.0);
    double q[4];
    double n2 = 0.0;
    do {
        n2 = 0.0;
        for (double& c : q) {
            c = uniform(rng, -1.0, 1.0);
            n2 += c * c;
        }
    } while (n2 > 1.0 || n2 < 0.01);
    const double n = std::sqrt(n2);
    s.qx = q[0] / n;
    s.qy = q[1] / n;
    s.qz = q[2] / n;
    s.qw = q[3] / n;
    s.vx = static_cast<float>(uniform(rng, -60.0, 60.0));
    s.vy = static_cast<float>(uniform(rng, -60.0, 60.0));
    s.vz = static_cast<float>(uniform(rng, -60.0, 60.0));
    for (std::size_t i = 0; i < 4; ++i) {
        s.compression[i] = static_cast<float>(uniform(rng, 0.0, 0.3));
        const std::uint32_t pick = rng.bounded(6);
        s.surface[i] = pick == 5 ? std::int8_t{-1} : static_cast<std::int8_t>(pick);
    }
    s.gear = static_cast<std::int8_t>(static_cast<int>(rng.bounded(7)) - 1);
    s.rpm = static_cast<float>(uniform(rng, 0.0, 9000.0));
    s.steer = static_cast<float>(uniform(rng, -1.0, 1.0));
    s.boost_bar = static_cast<float>(uniform(rng, 0.0, 2.55));
    s.reset_count = static_cast<std::uint8_t>(rng.bounded(256));
    return s;
}

double angle_between(const CarState& a, const CarState& b) {
    const double dot = a.qx * b.qx + a.qy * b.qy + a.qz * b.qz + a.qw * b.qw;
    return 2.0 * std::acos(std::min(1.0, std::abs(dot)));
}

struct Errors {
    double pos = 0, angle = 0, vel = 0, compression = 0, rpm = 0;
};

Errors roundtrip(const CarState& in, CarState& out) {
    std::array<std::uint8_t, kCarStateBytes> bytes{};
    encode(in, bytes);
    REQUIRE(decode(bytes, out));
    Errors e;
    e.pos = std::max({std::abs(in.x - out.x), std::abs(in.y - out.y), std::abs(in.z - out.z)});
    e.angle = angle_between(in, out);
    e.vel = std::max({std::abs(static_cast<double>(in.vx) - out.vx),
                      std::abs(static_cast<double>(in.vy) - out.vy),
                      std::abs(static_cast<double>(in.vz) - out.vz)});
    for (std::size_t i = 0; i < 4; ++i) {
        e.compression = std::max(
            e.compression, std::abs(static_cast<double>(in.compression[i]) - out.compression[i]));
    }
    e.rpm = std::abs(static_cast<double>(in.rpm) - out.rpm);
    return e;
}

} // namespace

TEST_CASE("net_state: the car state is exactly 50 bytes") {
    CHECK(kCarStateBytes == 50);
    CarState s;
    std::array<std::uint8_t, kCarStateBytes> bytes{};
    encode(s, bytes);
    CHECK(bytes[0] == 1);
}

TEST_CASE("net_state: 10000 random states round trip within the contract table") {
    Pcg32 rng(12345, 1);
    Errors worst;
    double worst_steer = 0.0;
    double worst_boost = 0.0;
    int omitted_seen[4] = {0, 0, 0, 0};
    for (int i = 0; i < 10000; ++i) {
        const CarState in = random_state(rng);
        CarState out;
        const Errors e = roundtrip(in, out);
        worst.pos = std::max(worst.pos, e.pos);
        worst.angle = std::max(worst.angle, e.angle);
        worst.vel = std::max(worst.vel, e.vel);
        worst.compression = std::max(worst.compression, e.compression);
        worst.rpm = std::max(worst.rpm, e.rpm);
        CHECK(out.generation == in.generation);
        CHECK(out.step == in.step);
        CHECK(out.surface == in.surface);
        CHECK(out.gear == in.gear);
        CHECK(out.reset_count == in.reset_count);
        worst_steer = std::max(worst_steer, std::abs(static_cast<double>(in.steer) - out.steer));
        worst_boost =
            std::max(worst_boost, std::abs(static_cast<double>(in.boost_bar) - out.boost_bar));
        std::array<std::uint8_t, kCarStateBytes> bytes{};
        encode(in, bytes);
        ++omitted_seen[bytes[18] & 3];
    }
    std::printf(
        "net_state: peor error: pos %.4f mm, rot %.2e rad, vel %.4f cm/s, compresion %.4f mm, "
        "rpm %.3f\n",
        worst.pos * 1000.0, worst.angle, worst.vel * 100.0, worst.compression * 1000.0, worst.rpm);
    CHECK(worst.pos <= 0.5e-3 + 1e-9);
    CHECK(worst.angle <= 1e-4);
    CHECK(worst.vel <= 0.005 + 1e-5);
    CHECK(worst.compression <= 0.05e-3 + 1e-8);
    CHECK(worst.rpm <= 0.5 + 1e-3);
    std::printf("net_state: peor error de direccion %.2e, turbo %.4f bar\n", worst_steer,
                worst_boost);
    CHECK(worst_steer <= 1.0 / 32767.0 + 1e-7);
    CHECK(worst_boost <= 0.005 + 1e-6);
    for (int k = 0; k < 4; ++k) {
        CHECK(omitted_seen[k] > 100);
    }
}

TEST_CASE("net_state: each omitted component and the sign of q") {
    // Una rotación con cada componente como la mayor, en las dos versiones de signo.
    const double base[4][4] = {{0.9, 0.2, -0.3, 0.1},
                               {0.1, -0.8, 0.3, 0.4},
                               {-0.2, 0.3, 0.7, 0.5},
                               {0.1, -0.2, 0.3, -0.9}};
    for (int k = 0; k < 4; ++k) {
        for (double sign : {1.0, -1.0}) {
            CarState in;
            in.qx = sign * base[k][0];
            in.qy = sign * base[k][1];
            in.qz = sign * base[k][2];
            in.qw = sign * base[k][3];
            const double n =
                std::sqrt(in.qx * in.qx + in.qy * in.qy + in.qz * in.qz + in.qw * in.qw);
            in.qx /= n;
            in.qy /= n;
            in.qz /= n;
            in.qw /= n;
            std::array<std::uint8_t, kCarStateBytes> bytes{};
            encode(in, bytes);
            CHECK(bytes[18] == k);
            CarState out;
            REQUIRE(decode(bytes, out));
            CHECK(angle_between(in, out) < 1e-4);
            // La omitida sale positiva: el decodificado es q o -q con la omitida >= 0.
            const double omitted[4] = {out.qx, out.qy, out.qz, out.qw};
            CHECK(omitted[k] >= 0.0);
        }
    }
}

TEST_CASE("net_state: decode rejects wrong sizes, types, omitted index and surfaces") {
    CarState s;
    std::array<std::uint8_t, kCarStateBytes + 1> bytes{};
    encode(s, std::span<std::uint8_t, kCarStateBytes>(bytes.data(), kCarStateBytes));
    CarState out;
    for (std::size_t n = 0; n <= 49; ++n) {
        CHECK_FALSE(decode(std::span<const std::uint8_t>(bytes.data(), n), out));
    }
    CHECK_FALSE(decode(std::span<const std::uint8_t>(bytes.data(), 51), out));
    CHECK(decode(std::span<const std::uint8_t>(bytes.data(), 50), out));
    for (int type = 0; type < 256; ++type) {
        auto copy = bytes;
        copy[0] = static_cast<std::uint8_t>(type);
        CHECK(decode(std::span<const std::uint8_t>(copy.data(), 50), out) == (type == 1));
    }
    auto bad_omitted = bytes;
    bad_omitted[18] = 4;
    CHECK_FALSE(decode(std::span<const std::uint8_t>(bad_omitted.data(), 50), out));
    auto bad_surface = bytes;
    bad_surface[39] = 5; // primera superficie: 5 no existe; 255 es el aire
    CHECK_FALSE(decode(std::span<const std::uint8_t>(bad_surface.data(), 50), out));
}

TEST_CASE("net_state: car_state_from copies the fields from the world") {
    const rally::sim::JoltRuntime runtime;
    rally::sim::WorldDesc desc =
        rally::tests::flat_world(rally::sim::Surface::Gravel, false, 15.0f);
    rally::sim::SimWorld world;
    world.build(desc);
    world.seal();
    world.set_telemetry(true);
    rally::sim::SimInput input;
    input.throttle = rally::sim::kAxisMax;
    input.steer = rally::sim::kAxisMax / 4;
    for (int i = 0; i < 480; ++i) {
        world.step(input);
    }
    const CarState s = car_state_from(world, 480, 7, 9);
    const auto pose = world.car_pose();
    const auto vel = world.car_velocity();
    const auto& tel = world.telemetry();
    CHECK(s.generation == 7);
    CHECK(s.step == 480);
    CHECK(s.reset_count == 9);
    CHECK(tel.steer != 0.0f);
    CHECK(tel.boost > 0.0f);
    CHECK(s.steer == tel.steer);
    CHECK(s.boost_bar == doctest::Approx(tel.boost / 1.0e5f));
    CHECK(s.x == pose.x);
    CHECK(s.y == pose.y);
    CHECK(s.z == pose.z);
    CHECK(s.qx == pose.qx);
    CHECK(s.qy == pose.qy);
    CHECK(s.qz == pose.qz);
    CHECK(s.qw == pose.qw);
    CHECK(s.vx == vel.vx);
    CHECK(s.vy == vel.vy);
    CHECK(s.vz == vel.vz);
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK(s.compression[i] == tel.wheels[i].compression);
        CHECK(s.surface[i] == tel.wheels[i].surface);
        CHECK(s.surface[i] == static_cast<std::int8_t>(rally::sim::Surface::Gravel));
    }
    CHECK(s.gear == tel.gear);
    CHECK(s.gear >= 1);
    // rpm = ω · 60 / 2π
    CHECK(s.rpm == doctest::Approx(tel.engine_omega * 60.0 / 6.283185307179586).epsilon(1e-5));
    CHECK(s.rpm > 500.0f);
    CHECK(s.compression[0] > 0.0f);
}

TEST_CASE("net_state: a wheel in the air is -1 and goes as 255 on the wire") {
    const rally::sim::JoltRuntime runtime;
    rally::sim::WorldDesc desc = rally::tests::flat_world(rally::sim::Surface::Asphalt);
    desc.car_spawn.y = 1.0;
    rally::sim::SimWorld world;
    world.build(desc);
    world.seal();
    world.set_telemetry(true);
    world.step(rally::sim::SimInput{});
    const CarState s = car_state_from(world, 1, 0, 0);
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK(s.surface[i] == -1);
    }
    std::array<std::uint8_t, kCarStateBytes> bytes{};
    encode(s, bytes);
    CHECK(bytes[39] == 255);
    CHECK(bytes[42] == 255);
}

TEST_CASE("net_state: the new fields sit at bytes 46 to 49 and saturate") {
    CarState s;
    s.steer = 0.5f;
    s.boost_bar = 1.23f;
    s.reset_count = 201;
    std::array<std::uint8_t, kCarStateBytes> bytes{};
    encode(s, bytes);
    const int steer = static_cast<std::int16_t>(bytes[46] | (bytes[47] << 8));
    CHECK(steer == 16384); // 0,5 · 32767 = 16383,5 -> 16384
    CHECK(bytes[48] == 123);
    CHECK(bytes[49] == 201);

    s.steer = 1.2f;
    s.boost_bar = 3.0f; // los coches llegan a 5 bar en el lector: satura, no da la vuelta
    encode(s, bytes);
    CHECK(static_cast<std::int16_t>(bytes[46] | (bytes[47] << 8)) == 32767);
    CHECK(bytes[48] == 255);
    s.steer = -1.2f;
    encode(s, bytes);
    CHECK(static_cast<std::int16_t>(bytes[46] | (bytes[47] << 8)) == -32767);
}
