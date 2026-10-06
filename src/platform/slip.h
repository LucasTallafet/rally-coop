#pragma once

#include "sim/car_data.h"

namespace rally::platform {

// Deslizamiento normalizado de una rueda (docs/02 §4.3): ρ = |(κ_r / κ_pico, tan α_r /
// tan α_pico)|, con κ_r y α_r de la telemetría y los picos del neumático en la superficie que
// pisa. ρ = 1 en el pico de agarre de cualquier superficie; por encima, la rueda desliza. Solo
// lo usan la vibración y el sonido (tarea 2.7): la simulación tiene su propio cálculo.
float normalized_slip(float kappa, float alpha, const sim::TireSurface& tire);

} // namespace rally::platform
