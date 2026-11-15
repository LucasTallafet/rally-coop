#pragma once

#include "game/direction_changes.h"
#include "game/notes.h"
#include "io/notes_log.h"
#include "net/host.h"

#include <cstdint>
#include <string>
#include <vector>

namespace rally::platform {

// El lado host de las notas del copiloto (lote 7): la cola de notas de la partida y su registro.
// Sin raylib ni SDL; todo ocurre fuera del paso de simulación y solo lee la `s` del coche. La cola
// empieza vacía con cada mundo; el registro (un archivo por pasada, junto a las grabaciones) se
// abre al llegar la primera orden, para no dejar archivos vacíos.
class NotesSession {
public:
    NotesSession() = default;
    NotesSession(const NotesSession&) = delete;
    NotesSession& operator=(const NotesSession&) = delete;

    // Mundo nuevo con tramo: cola vacía; cierra el registro anterior. `log_path`: dónde irá el
    // registro (UTF-8); `stage_name`, a su cabecera.
    void start_stage(double stage_length, std::vector<game::DirectionChange> changes,
                     std::string stage_name, std::string log_path);
    // Mundo sin tramo (zona o recinto): no hay notas; las órdenes que lleguen se descartan.
    void start_without_stage();

    // Una vez por fotograma, tras `host.service()`: aplica las órdenes recibidas, las registra y
    // publica el estado de las notas. `step`: pasos de la simulación; `car_s`: `s` del coche (la de
    // SimWorld::stage_run()). Devuelve cuántas órdenes se aplicaron en este fotograma.
    int pump(net::Host& host, std::uint64_t step, double car_s);

    const game::NoteQueue& queue() const { return queue_; }
    bool log_open() const { return log_.is_open(); }
    int log_rows() const { return log_.rows(); }
    // Último error del registro (vacío si ninguno); se dice en pantalla una vez.
    std::string take_error();
    // Cierra el registro (al salir).
    void close();

private:
    void open_log();

    game::NoteQueue queue_;
    io::NotesLog log_;
    std::vector<game::DirectionChange> changes_;
    std::string stage_name_;
    std::string log_path_;
    std::string error_;
    bool stage_ = false;
};

} // namespace rally::platform
