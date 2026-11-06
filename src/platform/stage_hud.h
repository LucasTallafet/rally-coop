#pragma once

#include "sim/stage_track.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace rally::platform {

// Lo que el modo tramo enseña de la pasada (tarea tramo-modo, lote 5). Sin raylib. Todo sale de
// StageRunState, que solo se lee: el tiempo son pasos de la simulación entre 240, sin penalización
// (mensajes/lote-5.md §8.3).

// Pasos como m:ss,cc, **truncado** a la centésima (decisión del consultor, 2026-11-06): 21345
// pasos = 88,9375 s → «1:28,93». La misma cuenta que rally_replay info (pasos × 100 / 240, en
// enteros). Negativo: «--».
std::string clock_text(std::int64_t steps);

// La primera parte del indicador de abajo, común a todos los modos: velocidad, marcha, caja, rpm,
// turbo y, si `car_name` no está vacío, el coche. `gear` ya lleva el cambio en curso («1>2»).
std::string hud_base_text(double speed_ms, std::string_view gear, bool sequential, double rpm,
                          double boost_pa, std::string_view car_name);

// Pasos de la pasada: 0 antes de la salida, los que lleva desde ella y, tras la meta, los de la
// salida a la meta.
std::int64_t run_clock_steps(const sim::StageRunState& run);

// Nombre del tramo para el indicador: cabe en el ancho de `max_chars` cifras (con fit_text: nunca
// parte un carácter) y lleva «...» si se corta.
std::string short_stage_name(std::string_view name, int max_chars = 16);

// El texto del tramo en el indicador de abajo: «tramo <nombre> · m:ss,cc» desde la salida y, tras
// la meta, «meta m:ss,cc · recol. N · atajos N».
std::string stage_indicator_text(std::string_view name, const sim::StageRunState& run);

// Las dos líneas de F3 del tramo (una sola no cabía en el panel a 1920×1080): «tramo: s … / L m,
// t … m, en carretera / fuera» y «recolocación en s = … m · recol. N · atajos N».
std::string stage_debug_line(const sim::StageRunState& run, double length);
std::string stage_debug_counts_line(const sim::StageRunState& run);

// Lo que dice el tramo en un fotograma: los sucesos de la pasada y, aparte, la meta (su propio
// hueco de mensaje, que dura más y no lo borra un atajo del fotograma siguiente).
struct StageMessages {
    std::string events;
    std::string finish;
    bool empty() const { return events.empty() && finish.empty(); }
};

// Mensajes por cambio de contador (recolocaciones, atajos, parciales y meta): sucesos más cortos
// que un fotograma que se verían por un instante si se mirase el estado. Se llama una vez por
// fotograma, tras sus pasos.
class StageEvents {
public:
    // Mundo nuevo: los contadores vuelven a cero.
    void reset() { *this = StageEvents(); }
    // Los sucesos desde la llamada anterior (parciales, recolocaciones y atajos unidos con « · »;
    // la meta, aparte). Dos recolocaciones en el mismo fotograma dan «2 recolocaciones…», nunca
    // ninguna. Si un contador baja (otro mundo sin que se llamase a reset), se empieza de nuevo
    // sin mensajes.
    StageMessages observe(const sim::StageRunState& run);

private:
    std::int32_t resets_ = 0;
    std::int32_t shortcuts_ = 0;
    std::int32_t splits_ = 0;
    bool finished_ = false;
};

} // namespace rally::platform
