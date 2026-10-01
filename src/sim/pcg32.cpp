#include "sim/pcg32.h"

#include <cassert>

namespace rally::sim {

namespace {
constexpr std::uint64_t kMultiplier = 6364136223846793005ull;
} // namespace

Pcg32::Pcg32(std::uint64_t initstate, std::uint64_t initseq) { seed(initstate, initseq); }

// Igual que pcg32_srandom_r de la implementación de referencia.
void Pcg32::seed(std::uint64_t initstate, std::uint64_t initseq) {
    state_ = 0;
    inc_ = (initseq << 1u) | 1u;
    next_u32();
    state_ += initstate;
    next_u32();
}

std::uint32_t Pcg32::next_u32() {
    const std::uint64_t old = state_;
    state_ = old * kMultiplier + inc_;
    const auto xorshifted = static_cast<std::uint32_t>(((old >> 18u) ^ old) >> 27u);
    const auto rot = static_cast<std::uint32_t>(old >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((0u - rot) & 31u));
}

// Igual que pcg32_boundedrand_r: descarta los valores por debajo de 2^32 mod bound.
std::uint32_t Pcg32::bounded(std::uint32_t bound) {
    assert(bound > 0);
    const std::uint32_t threshold = (0u - bound) % bound;
    for (;;) {
        const std::uint32_t r = next_u32();
        if (r >= threshold) {
            return r % bound;
        }
    }
}

float Pcg32::next_float01() { return static_cast<float>(next_u32() >> 8u) * 0x1.0p-24f; }

} // namespace rally::sim
