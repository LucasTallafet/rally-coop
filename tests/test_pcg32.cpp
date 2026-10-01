#include "sim/pcg32.h"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

using rally::sim::Pcg32;

// Valores de referencia: test-high/expected/check-pcg32.out de la implementación
// oficial en C (github.com/imneme/pcg-c), sembrada con pcg32_srandom_r(42, 54).
// En ese programa, tras los 6 valores de 32 bits se tiran 65 monedas con
// pcg32_boundedrand_r(2) y 33 dados con pcg32_boundedrand_r(6) + 1.

TEST_CASE("pcg32: reference values from pcg-c (seed 42, stream 54)") {
    Pcg32 rng(42u, 54u);

    const std::array<std::uint32_t, 6> expected = {
        0xa15c02b7u, 0x7b47f409u, 0xba1d3330u, 0x83d2f293u, 0xbfa4784bu, 0xcbed606eu,
    };
    for (const std::uint32_t value : expected) {
        CHECK(rng.next_u32() == value);
    }

    std::string coins;
    for (int i = 0; i < 65; ++i) {
        coins += rng.bounded(2) != 0 ? 'H' : 'T';
    }
    CHECK(coins == "HHTTTHTHHHTHTTTHHHHHTTTHHHTHTHTHTTHTTTHHHHHHTTTTHHTTTTTHTTTTTTTHT");

    const std::array<std::uint32_t, 33> rolls = {3, 4, 1, 1, 2, 2, 3, 2, 4, 3, 2, 4, 3, 3, 5, 2, 3,
                                                 1, 3, 1, 5, 1, 4, 1, 5, 6, 4, 6, 6, 2, 6, 3, 3};
    for (const std::uint32_t roll : rolls) {
        CHECK(rng.bounded(6) + 1 == roll);
    }
}

TEST_CASE("pcg32: same seed gives the same sequence") {
    Pcg32 a(0x853c49e6748fea9bull, 0xda3e39cb94b95bdbull);
    Pcg32 b(0x853c49e6748fea9bull, 0xda3e39cb94b95bdbull);
    for (int i = 0; i < 10000; ++i) {
        REQUIRE(a.next_u32() == b.next_u32());
    }
}

TEST_CASE("pcg32: reseeding restarts the sequence") {
    Pcg32 rng(7u, 3u);
    std::vector<std::uint32_t> first;
    for (int i = 0; i < 100; ++i) {
        first.push_back(rng.next_u32());
    }
    rng.seed(7u, 3u);
    for (const std::uint32_t value : first) {
        REQUIRE(rng.next_u32() == value);
    }
}

TEST_CASE("pcg32: different seed or stream gives a different sequence") {
    auto first_values = [](std::uint64_t seed, std::uint64_t stream) {
        Pcg32 rng(seed, stream);
        std::array<std::uint32_t, 8> values{};
        for (auto& v : values) {
            v = rng.next_u32();
        }
        return values;
    };
    CHECK(first_values(42u, 54u) != first_values(43u, 54u));
    CHECK(first_values(42u, 54u) != first_values(42u, 55u));
}

TEST_CASE("pcg32: bounded stays in range") {
    Pcg32 rng(1u, 1u);
    for (const std::uint32_t bound : {1u, 2u, 3u, 6u, 7u, 1000u, 0x80000001u, 0xffffffffu}) {
        for (int i = 0; i < 2000; ++i) {
            REQUIRE(rng.bounded(bound) < bound);
        }
    }
}

// Sin corchetes en los nombres de test: rompen las listas de CMake al descubrirlos.
TEST_CASE("pcg32: next_float01 is at least 0 and below 1") {
    Pcg32 rng(9u, 2u);
    for (int i = 0; i < 10000; ++i) {
        const float f = rng.next_float01();
        REQUIRE(f >= 0.0f);
        REQUIRE(f < 1.0f);
    }
}
