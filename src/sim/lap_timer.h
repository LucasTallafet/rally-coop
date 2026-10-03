#pragma once

#include "sim/test_zone.h"

#include <cstdint>

namespace rally::sim {

// Estado del cronómetro: pasos desde que se construyó el mundo y vueltas en pasos.
struct LapState {
    std::int64_t steps = 0;      // pasos contados
    std::int64_t lap_start = -1; // paso en que empezó la vuelta en curso; -1 si no hay
    std::int32_t debt = 0;       // cruces hacia atrás que aún no ha deshecho uno hacia delante
    std::int32_t laps = 0;       // vueltas completas
    std::int64_t last_lap = -1;  // pasos de la última vuelta completa; -1 si ninguna
    std::int64_t best_lap = -1;  // pasos de la mejor vuelta completa; -1 si ninguna

    // Pasos de la vuelta en curso; -1 si no hay.
    std::int64_t current() const { return lap_start < 0 ? -1 : steps - lap_start; }
};

// Cronómetro del recorrido cronometrado. El cuerpo controlado cruza la línea de meta hacia
// delante: empieza una vuelta y, si había una en curso, la termina. Un cruce hacia atrás no
// cuenta y deja una deuda: el siguiente cruce hacia delante solo la salda (ir y volver sobre
// la línea no es una vuelta). Recolocar anula la vuelta en curso y la deuda. Cuenta pasos: no
// lee ningún reloj.
class LapTimer {
public:
    explicit LapTimer(const FinishLine& line) : line_(line) {}

    // Tras cada paso, con la posición del cuerpo (en el plano) antes y después del paso.
    // `reset`: el paso recolocó el cuerpo (el salto no es un cruce).
    void update(double x0, double z0, double x1, double z1, bool reset);

    const LapState& state() const { return state_; }

private:
    FinishLine line_;
    LapState state_;
};

} // namespace rally::sim
