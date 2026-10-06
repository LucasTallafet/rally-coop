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

// Estado del cronómetro de la carretera de prueba (de punto a punto), en pasos.
struct RoadRunState {
    std::int64_t steps = 0;       // pasos contados
    std::int64_t run_start = -1;  // paso en que empezó la pasada en curso; -1 si no hay
    std::int32_t resets = 0;      // recolocaciones en la pasada en curso
    std::int32_t start_debt = 0;  // cruces hacia atrás de la salida sin deshacer
    std::int32_t finish_debt = 0; // cruces hacia atrás de la meta sin deshacer
    std::int32_t runs = 0;        // pasadas completas
    std::int64_t last_run = -1;   // pasos de la última pasada completa; -1 si ninguna
    std::int32_t last_resets = 0; // recolocaciones de la última pasada completa
    std::int64_t best_clean = -1; // la mejor pasada completa sin recolocar; -1 si ninguna

    // Pasos de la pasada en curso; -1 si no hay.
    std::int64_t current() const { return run_start < 0 ? -1 : steps - run_start; }
};

// Cronómetro de la carretera de prueba. Cruzar la salida hacia delante empieza una pasada (la
// que hubiera se descarta); cruzar la meta hacia delante con una pasada en curso la cierra.
// Hacia atrás no cuenta y deja una deuda en esa línea, como LapTimer. Recolocar no anula la
// pasada: la marca y cuenta cuántas veces (y olvida las deudas: un salto no es un cruce).
// Cuenta pasos: no lee ningún reloj.
class RoadTimer {
public:
    RoadTimer(const FinishLine& start, const FinishLine& finish) : start_(start), finish_(finish) {}

    // Tras cada paso, como LapTimer::update.
    void update(double x0, double z0, double x1, double z1, bool reset);

    const RoadRunState& state() const { return state_; }

private:
    FinishLine start_;
    FinishLine finish_;
    RoadRunState state_;
};

// Cruce de una línea en un paso de (x0, z0) a (x1, z1): +1 hacia delante, −1 hacia atrás, 0 si
// no la cruza (o la cruza fuera de su longitud).
int line_crossing(const FinishLine& line, double x0, double z0, double x1, double z1);

} // namespace rally::sim
