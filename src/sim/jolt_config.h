#pragma once

// Único punto de entrada a Jolt para el código del juego. Las definiciones JPH_* llegan
// del target de Jolt (PUBLIC); aquí solo se comprueba que son las esperadas.

#include "sim/fp_guard.h"

#include <Jolt/Jolt.h>

#if !defined(JPH_DOUBLE_PRECISION)
#error "Jolt debe compilarse con JPH_DOUBLE_PRECISION (DOUBLE_PRECISION=ON)."
#endif

#if !defined(JPH_CROSS_PLATFORM_DETERMINISTIC)
#error "Jolt debe compilarse con JPH_CROSS_PLATFORM_DETERMINISTIC."
#endif

#if defined(JPH_USE_FMADD) || defined(JPH_USE_AVX) || defined(JPH_USE_AVX2) ||                     \
    defined(JPH_USE_AVX512)
#error "Objetivo SSE4.2: Jolt no puede usar AVX ni FMA."
#endif
