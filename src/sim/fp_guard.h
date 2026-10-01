#pragma once

// Guarda de coma flotante: cualquier unidad de simulación compilada con flags que
// rompan la reproducibilidad no compila. MSVC define _M_FP_FAST con /fp:fast y
// _M_FP_CONTRACT con /fp:contract o /fp:fast (VS 2022+), y _M_FP_PRECISE con /fp:precise.
// fp_guard.cpp también se compila dentro de Jolt para cubrir sus flags.

#if !defined(_MSC_VER) || defined(__clang__)
#error "Rally Coop se compila solo con MSVC."
#endif

#if defined(_M_FP_FAST)
#error "La simulación no puede compilarse con /fp:fast."
#endif

#if defined(_M_FP_CONTRACT)
#error "La simulación no puede compilarse con /fp:contract (contracción FMA)."
#endif

#if !defined(_M_FP_PRECISE)
#error "La simulación debe compilarse con /fp:precise."
#endif

#if defined(__AVX__) || defined(__AVX2__) || defined(__AVX512F__)
#error "Objetivo SSE4.2: la simulación no puede compilarse con /arch:AVX*."
#endif
