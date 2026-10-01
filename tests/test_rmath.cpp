#include "sim/rmath.h"

#include <doctest/doctest.h>

#include <bit>
#include <cmath>
#include <cstdint>

// rmath es un envoltorio fino: debe dar exactamente lo mismo que std::, bit a bit.

namespace {

constexpr double kInputs[] = {-7.25, -1.0, -0.5, -1e-300, 0.0,     1e-12, 0.3,
                              0.5,   0.75, 1.0,  2.0,     3.14159, 10.0,  123.456};

bool same_bits(double a, double b) {
    return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}

bool same_bits(float a, float b) {
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

} // namespace

TEST_CASE("rmath: unary functions match std bit for bit") {
    for (const double x : kInputs) {
        CAPTURE(x);
        const auto xf = static_cast<float>(x);
        CHECK(same_bits(rmath::sin(x), std::sin(x)));
        CHECK(same_bits(rmath::cos(x), std::cos(x)));
        CHECK(same_bits(rmath::tan(x), std::tan(x)));
        CHECK(same_bits(rmath::atan(x), std::atan(x)));
        CHECK(same_bits(rmath::abs(x), std::abs(x)));
        CHECK(same_bits(rmath::floor(x), std::floor(x)));
        CHECK(same_bits(rmath::ceil(x), std::ceil(x)));
        CHECK(same_bits(rmath::exp(x), std::exp(x)));
        CHECK(same_bits(rmath::sin(xf), std::sin(xf)));
        CHECK(same_bits(rmath::cos(xf), std::cos(xf)));
        if (x >= -1.0 && x <= 1.0) {
            CHECK(same_bits(rmath::asin(x), std::asin(x)));
            CHECK(same_bits(rmath::acos(x), std::acos(x)));
        }
        if (x >= 0.0) {
            CHECK(same_bits(rmath::sqrt(x), std::sqrt(x)));
            CHECK(same_bits(rmath::sqrt(xf), std::sqrt(xf)));
        }
        if (x > 0.0) {
            CHECK(same_bits(rmath::log(x), std::log(x)));
        }
    }
}

TEST_CASE("rmath: binary functions match std bit for bit") {
    for (const double a : kInputs) {
        for (const double b : kInputs) {
            CAPTURE(a);
            CAPTURE(b);
            CHECK(same_bits(rmath::atan2(a, b), std::atan2(a, b)));
            if (b != 0.0) {
                CHECK(same_bits(rmath::fmod(a, b), std::fmod(a, b)));
            }
            if (a > 0.0) {
                CHECK(same_bits(rmath::pow(a, b), std::pow(a, b)));
            }
        }
    }
}

TEST_CASE("rmath: min, max and clamp") {
    CHECK(rmath::min(1.5, -2.0) == -2.0);
    CHECK(rmath::max(1.5, -2.0) == 1.5);
    CHECK(rmath::clamp(3.0, 0.0, 1.0) == 1.0);
    CHECK(rmath::clamp(-3.0, 0.0, 1.0) == 0.0);
    CHECK(rmath::clamp(0.25, 0.0, 1.0) == 0.25);
}
