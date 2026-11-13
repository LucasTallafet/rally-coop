#pragma once

#include "game/direction_changes.h"
#include "game/notes.h"

#include <cstdint>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace rally::io {

// Registro de notas (docs/04, fase 3): un archivo de texto por pasada con una fila por orden
// recibida por el host (también las rechazadas y los borrados). CSV en UTF-8 con BOM: quien lo lea
// con Python usa `utf-8-sig`. Lo escribe la plataforma fuera del paso de simulación; las notas no
// entran en la grabación (.rrec) ni en el CSV de telemetría.
//
// Cabecera: BOM y líneas `# clave=valor` (`# rally notes`, formato, tramo) y la fila de nombres.
// Columnas:
//   step             paso de la simulación en que se aplicó la orden (el mismo que el .rrec)
//   order            curva, peligro, modificador, distancia, borrar o vaciar
//   result           ok, sin nota, cola llena o no valida
//   note_id          identificador de la nota afectada (vacío si no hay)
//   anchor_s         ancla de la nota afectada, m (3 decimales)
//   note             «3 izquierda se cierra», «H derecha», «cuidado»: sin la distancia
//   distance         la elegida: «y», metros o «300+»; vacía si no hay
//   distance_source  rueda o contador (la forma usada)
//   real_distance_m  la distancia real del ancla al siguiente cambio de dirección
//                    (game::real_distance_after); la pone este registro, nunca el juego
//   car_s            `s` del coche en ese paso, m
//   erased           notas quitadas (borrar: 0 o 1; vaciar: las pendientes)
inline constexpr int kNotesLogFormatVersion = 1;

std::string notes_log_header(std::string_view stage_name);

// Añade la fila de una orden a `out` (terminada en '\n'). `changes`: direction_changes() del tramo.
void append_notes_log_row(std::uint64_t step, double car_s, const game::NoteOrder& order,
                          const game::NoteApplied& applied,
                          const std::vector<game::DirectionChange>& changes, std::string& out);

// Escribe cada fila en cuanto llega (una orden es un suceso, ~1 por segundo) y la vuelca: un
// cierre brusco no pierde el registro.
class NotesLog {
public:
    NotesLog() = default;
    NotesLog(const NotesLog&) = delete;
    NotesLog& operator=(const NotesLog&) = delete;

    bool open(const std::string& path, std::string_view header, std::string& error);
    bool is_open() const { return file_.is_open(); }
    void append(std::uint64_t step, double car_s, const game::NoteOrder& order,
                const game::NoteApplied& applied,
                const std::vector<game::DirectionChange>& changes);
    bool close(std::string& error);
    int rows() const { return rows_; }

private:
    std::ofstream file_;
    std::string path_;
    std::string row_;
    int rows_ = 0;
    bool failed_ = false;
};

} // namespace rally::io
