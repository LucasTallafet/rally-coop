#pragma once

// Mensajes de notas del protocolo 4 (lote 7). Canal fiable (0) en los dos sentidos: las órdenes
// del copiloto al host y el estado de las notas del host al copiloto. Little-endian, campo a
// campo; el primer u8 es el tipo (kMsgNoteOrder y kMsgNotesState, en net/protocol.h). Sin ENet.
//
// Las anclas viajan en milímetros (i32, como la posición del coche): el host trabaja con el ancla
// ya cuantizada. Quien compare un ancla con otra `s` (por ejemplo, el cursor de la tablet con el
// s_start de un cambio de dirección) usa una tolerancia de al menos kNoteAnchorStepM.

#include "game/notes.h"
#include "net/protocol.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace rally::net {

// tipo, generación, clase, s (i32), grado, lado, marca, clase de distancia, metros (u16), forma.
inline constexpr std::size_t kNoteOrderBytes = 14;
inline constexpr double kNoteAnchorStepM = 0.001; // el ancla viaja en mm

// Lo que el copiloto muestra en su interfaz; viene entero del host (el copiloto no lleva su
// propia cuenta). Las anclas, en orden de llegada y ya cuantizadas.
struct NotesState {
    std::uint8_t generation = 0;
    std::uint32_t revision = 0; // NoteQueue::revision()
    game::NoteResult last_result = game::NoteResult::Applied;
    bool has_last = false; // la última nota que sigue en la cola
    game::Note last;
    int count = 0;
    std::array<double, game::kMaxNotes> anchors{};
    friend bool operator==(const NotesState&, const NotesState&) = default;
};

NotesState notes_state_from(const game::NoteQueue& queue, std::uint8_t generation);
// Cuántas notas hay por delante de `car_s` (ancla >= car_s): las pendientes.
int notes_ahead(const NotesState& state, double car_s);
// ¿Hay una nota con ancla a menos de `tolerance` m de `s`? Para que el cursor salte al siguiente
// cambio de dirección sin nota (docs/01 §5.2).
bool notes_has_anchor_near(const NotesState& state, double s, double tolerance);

// Mensaje de una orden (14 B). `generation`: la del mundo del copiloto; el host descarta las de
// otro mundo.
std::string encode_note_order(const game::NoteOrder& order, std::uint8_t generation);
// false si el tamaño, el tipo o un campo no cuadran, o la orden no es válida (game::valid), o un
// campo que la clase de orden no usa no vale 0. No lee fuera del mensaje.
bool decode_note_order(std::span<const std::uint8_t> in, game::NoteOrder& order,
                       std::uint8_t& generation);

// 25 + 4·n B. Mismas garantías.
std::string encode_notes_state(const NotesState& state);
bool decode_notes_state(std::span<const std::uint8_t> in, NotesState& out);

} // namespace rally::net
