#pragma once

#include <cstdint>

namespace rally::sim {

// PCG32 (PCG-XSH-RR 64/32) de M. O'Neill, pcg-random.org. Único generador de
// aleatorios de la simulación y el generador; nunca <random> ni rand().
// Misma semilla y mismo stream dan la misma secuencia en cualquier PC.
class Pcg32 {
public:
    // initstate: semilla; initseq: stream (secuencias independientes con la misma semilla).
    Pcg32(std::uint64_t initstate, std::uint64_t initseq);

    void seed(std::uint64_t initstate, std::uint64_t initseq);

    // Siguiente valor uniforme en [0, 2^32).
    std::uint32_t next_u32();

    // Uniforme en [0, bound) sin sesgo. bound > 0.
    std::uint32_t bounded(std::uint32_t bound);

    // Uniforme en [0, 1) con 24 bits (todos los float representables a paso 2^-24).
    float next_float01();

private:
    std::uint64_t state_ = 0;
    std::uint64_t inc_ = 0;
};

} // namespace rally::sim
