#pragma once

// Toda la matemática de simulación y generación pasa por aquí. De momento es un
// envoltorio fino de std::; si hace falta física idéntica entre PCs (fase 9), se
// sustituye por implementaciones propias sin tocar a quien llama.
// Es el único archivo de src/sim, src/gen y src/net que puede usar <cmath> directamente
// (lo comprueba cmake/CheckSimRules.cmake).

#include "sim/fp_guard.h"

#include <algorithm>
#include <cmath>

namespace rmath {

#define RMATH_UNARY(name)                                                                          \
    inline float name(float x) { return std::name(x); }                                            \
    inline double name(double x) { return std::name(x); }

#define RMATH_BINARY(name)                                                                         \
    inline float name(float a, float b) { return std::name(a, b); }                                \
    inline double name(double a, double b) { return std::name(a, b); }

RMATH_UNARY(sin)
RMATH_UNARY(cos)
RMATH_UNARY(tan)
RMATH_UNARY(asin)
RMATH_UNARY(acos)
RMATH_UNARY(atan)
RMATH_UNARY(sqrt)
RMATH_UNARY(abs)
RMATH_UNARY(floor)
RMATH_UNARY(ceil)
RMATH_UNARY(exp)
RMATH_UNARY(log)
RMATH_BINARY(atan2)
RMATH_BINARY(fmod)
RMATH_BINARY(pow)

#undef RMATH_UNARY
#undef RMATH_BINARY

template <typename T>
constexpr T min(T a, T b) {
    return std::min(a, b);
}

template <typename T>
constexpr T max(T a, T b) {
    return std::max(a, b);
}

template <typename T>
constexpr T clamp(T x, T lo, T hi) {
    return std::clamp(x, lo, hi);
}

} // namespace rmath
