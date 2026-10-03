#pragma once

#include "io/recording.h"
#include "sim/sim_world.h"

#include <cstdint>
#include <string>

namespace rally::io {

// Repetición sin bucle ni ventana: construye el mundo de la cabecera y aplica cada entrada
// grabada a su paso. Necesita un JoltRuntime vivo.

// Construye y sella el mundo de la grabación. false (con el motivo) si la descripción no
// es válida.
bool build_world(const Recording& recording, sim::SimWorld& world, std::string& error);

// Tiempo cronometrado del pie de la grabación: la mejor vuelta completa en pasos, o -1.
std::int64_t timed_steps(const sim::SimWorld& world);

struct ReplayResult {
    bool ran = false;    // se pudo repetir (si no, `problem` dice por qué)
    std::string problem; // primer problema; vacío si todo cuadra
    std::uint64_t steps = 0;
    std::uint64_t hash = 0;        // SimWorld::state_hash tras el último paso
    std::int64_t timed_steps = -1; // mejor vuelta repetida, en pasos (-1 si ninguna)
    // La grabación es de otra kSimVersion: se repite, pero no se compara el hash.
    bool version_mismatch = false;
    // Grabación incompleta (guardada al saltar un RALLY_CHECK): se repite hasta su último
    // paso y no hay hash con el que comparar.
    bool incomplete = false;
    // Hash y pasos iguales a los del pie (solo si es completa y de la misma versión).
    bool hash_ok = false;
};

ReplayResult replay(const Recording& recording);

// Repite la grabación y le pone la versión actual de la simulación y el pie que da (pasos,
// hash y completa). Solo para grabaciones sintéticas de tests: una grabación real de otra
// versión no tiene por qué dar el mismo recorrido.
ReplayResult rewrite_footer(Recording& recording);

} // namespace rally::io
