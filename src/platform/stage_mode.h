#pragma once

#include "sim/car_data.h"
#include "sim/sim_world.h"
#include "sim/stage_track.h"

#include <cstdint>
#include <string>

namespace rally::platform {

// Modo tramo del juego (tarea tramo-modo, lote 5; mensajes/lote-5.md §2.6). Sin raylib: la lógica
// de K, la selección del tramo y la descripción del mundo se prueban sin ventana.

// Dónde sale el coche en el tramo: 5 m antes de la línea de salida (s = 0); la pasada empieza al
// pasar s = 0,10 m. Provisional, hasta la salida parada con cuenta atrás de la fase 5.
inline constexpr double kGameStageSpawnS = -5.0;

// Qué mundo se juega: K los recorre en este orden.
enum class WorldKind : std::uint8_t { Zone, Stage, Arena };

// El siguiente modo de K: zona → tramo → recinto → zona. Si el siguiente es el tramo y no se puede
// cargar (`stage_available` falso), el modo se queda como estaba.
WorldKind cycle_world_kind(WorldKind current, bool stage_available);

// El tramo elegido: su archivo (--tramo o data/stages/lazadas.stage), su texto ya validado y lo
// que se enseña de él.
struct StageSelection {
    std::string path;      // UTF-8; vacío hasta la primera lectura
    std::string text;      // el del archivo; vacío: sin tramo cargado
    std::string name;      // StageAxis::name
    double length = 0.0;   // m (sin prolongaciones)
    sim::StageTrack track; // el que se juega (`played_text()`)
    // Tecla de la irregularidad (lote 7): se juega el mismo tramo sin su sección [irregularity].
    // Sigue activa al cargar otro archivo; en un tramo sin sección no cambia nada.
    bool flat = false;
    bool has_irregularity_ = false; // calculado al leer el texto (no en cada fotograma)
    std::string played; // el texto que se juega: `text`, o `text` sin la irregularidad si `flat`

    bool loaded() const { return !text.empty(); }
    // El texto del archivo tiene la sección [irregularity].
    bool has_irregularity() const;
    const std::string& played_text() const { return played; }
    // Valida el texto que se jugaría con `new_text` con StageTrack::build. Si vale, la guarda y
    // devuelve true; si no, deja la selección como estaba y `error` es el de StageTrack::build
    // («línea N: …»).
    bool set_text(std::string new_text, std::string& error);
    // Pone o quita la irregularidad (reconstruye la pista); false con el error y sin cambios.
    bool set_flat(bool value, std::string& error);
};

// Lo que pasa al pulsar la tecla de la irregularidad.
struct IrregularityToggle {
    bool changed = false; // hay que reconstruir el mundo
    std::string message;
};
// Quita la irregularidad si se juega con ella y la pone si no. En un tramo sin sección: nada
// cambia y el mensaje lo dice.
IrregularityToggle toggle_irregularity(StageSelection& selection);
// Lo que añade el indicador del tramo: « · con irregularidad», « · sin irregularidad» o nada si
// el tramo no la tiene.
std::string irregularity_suffix(const StageSelection& selection);

// Lo que K encontró al leer el archivo del tramo (lo lee la plataforma; la lógica no toca
// archivos).
struct StageFile {
    std::string path; // vacío: no se encontró data/stages/lazadas.stage
    std::string text;
    std::string error; // no vacío: no se pudo leer
};

// Lo que pasa al pulsar K: el modo nuevo (o el mismo, si el tramo no se puede cargar), si cambia,
// si hay que quitar el modo a ciegas (al entrar en el tramo) y el mensaje de error, con la línea
// del archivo, si lo hay.
struct WorldStep {
    WorldKind kind = WorldKind::Zone;
    bool changed = false;
    bool clear_blind = false;
    std::string message;
};
// Un paso de K desde `current`. Solo mira `file` si el siguiente modo es el tramo; entonces lo
// valida y, si vale, lo guarda en `stage` (con su ruta); si no, deja `stage` y el modo como
// estaban.
WorldStep step_world_kind(WorldKind current, StageSelection& stage, const StageFile& file);

// Descripción del mundo del modo tramo: el texto, el coche parado en stage_spawn(track,
// kGameStageSpawnS), sin zona de pruebas, y las ayudas del jugador.
sim::WorldDesc stage_world_desc(const StageSelection& selection, const sim::CarData& car,
                                const sim::DriverAids& aids);

} // namespace rally::platform
