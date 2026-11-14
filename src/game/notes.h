#pragma once

#include "game/direction_changes.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace rally::game {

// Notas del copiloto (docs/01 §5): el modelo, las órdenes y la cola del host. Puro: sin red ni
// plataforma, sin reloj ni matemática fuera de comparaciones. Es estado del juego, no de la
// simulación: no entra en el SaveState (fase 5) ni en state_hash() ni en el .rrec.
//
// El juego nunca modifica una nota (docs/01 §5.4): ni corrige el grado, ni ajusta los metros, ni
// descarta, ni une. Lo único que hace con el ancla es acotarla al tramo. Que una nota esté pasada
// es una comparación con la `s` del coche (`s_nota < car_s`), no un campo: si el coche se recoloca
// por detrás de un ancla, la nota vuelve a estar por delante.

inline constexpr int kMaxNotes = 256; // capacidad fija de la cola
inline constexpr int kMaxNoteDistanceM = 990;

// None = peligro solo. 1..6 y H (Hairpin) como en docs/01 §5.1.
enum class NoteGrade : std::uint8_t { None = 0, G1, G2, G3, G4, G5, G6, Hairpin };
enum class NoteSide : std::uint8_t { None = 0, Left, Right };

// Lo que añade el dial secundario; el valor es el bit en Note::marks. Los cuatro primeros son
// modificadores y los tres últimos, peligros.
enum class NoteMark : std::uint8_t { Tightens = 0, Opens, DontCut, Long, Crest, Jump, Caution };
inline constexpr int kNoteMarkCount = 7;
constexpr bool is_hazard(NoteMark mark) {
    return mark == NoteMark::Crest || mark == NoteMark::Jump || mark == NoteMark::Caution;
}
constexpr std::uint8_t mark_bit(NoteMark mark) {
    return static_cast<std::uint8_t>(1u << static_cast<unsigned>(mark));
}

// Qué lleva pegado después de la nota: nada, «y» (enlaza), metros o «300+».
enum class DistanceKind : std::uint8_t { None = 0, Link, Metres, Over300 };
// La forma de elegirla (lote 7 §2.3): la rueda de 8 valores o el contador de 10 en 10.
enum class DistanceSource : std::uint8_t { None = 0, Wheel, Counter };

struct NoteDistance {
    DistanceKind kind = DistanceKind::None;
    std::uint16_t metres = 0; // 0..990; solo con Metres, si no 0
    DistanceSource source = DistanceSource::None;
    friend bool operator==(const NoteDistance&, const NoteDistance&) = default;
};

// Rueda de 8 valores: índice 0 = «y», 1..6 = 30, 50, 80, 100, 150, 200 m, 7 = «300+».
NoteDistance wheel_distance(int index);
// Contador: metros de 0 a 990 (se acota; el juego no elige por el copiloto, solo evita salirse
// del rango del modelo).
NoteDistance counter_distance(int metres);

struct Note {
    std::uint32_t id = 0; // desde 1, creciente; vuelve a empezar con reset()
    double s = 0.0;       // ancla, m sobre el esqueleto
    NoteGrade grade = NoteGrade::None;
    NoteSide side = NoteSide::None;
    std::uint8_t marks = 0; // bits de NoteMark
    NoteDistance distance;
    friend bool operator==(const Note&, const Note&) = default;
};

enum class NoteOrderKind : std::uint8_t {
    Corner = 1,   // nota de curva: s, grade, side
    Hazard,       // peligro solo: s, mark (Crest, Jump o Caution)
    Mark,         // modificador o peligro a la última nota: mark
    Distance,     // distancia a la última nota: distance
    EraseLast,    // borra la última
    ClearPending, // vacía las pendientes (ancla por delante del coche)
};

// Los campos que una orden no usa valen su valor por defecto.
struct NoteOrder {
    NoteOrderKind kind = NoteOrderKind::Corner;
    double s = 0.0;
    NoteGrade grade = NoteGrade::None;
    NoteSide side = NoteSide::None;
    NoteMark mark = NoteMark::Tightens;
    NoteDistance distance;
    friend bool operator==(const NoteOrder&, const NoteOrder&) = default;
};

NoteOrder corner_order(double s, NoteGrade grade, NoteSide side);
NoteOrder hazard_order(double s, NoteMark hazard);
NoteOrder mark_order(NoteMark mark);
NoteOrder distance_order(const NoteDistance& distance);
NoteOrder erase_last_order();
NoteOrder clear_pending_order();

// Rangos válidos. Una orden que no lo es, la cola la rechaza (Invalid) y la red no la manda ni
// la acepta. La forma de la distancia es None si y solo si la clase es None; los metros valen 0
// salvo con Metres y no pasan de 990.
bool valid(const NoteDistance& distance);
bool valid(const NoteOrder& order);

enum class NoteResult : std::uint8_t {
    Applied = 0,
    NoNote,  // modificador, distancia o borrado sin nota previa
    Full,    // cola llena
    Invalid, // fuera de rango
};

// Lo que hizo una orden; también lo que necesita el registro.
struct NoteApplied {
    NoteResult result = NoteResult::Applied;
    bool has_note = false; // `note`: la añadida o cambiada; en EraseLast, la borrada tal como era
    Note note;
    int erased = 0; // notas quitadas (EraseLast: 0 o 1; ClearPending: 0..kMaxNotes)
};

// Lo que dibuja el panel, función pura de la cola y de la `s` del coche.
struct NotesView {
    bool has_next = false;
    bool has_after = false;
    Note
        next; // la de ancla más cercana por delante (s >= car_s); a igual ancla, la que llegó antes
    Note after;    // la siguiente a esa
    int ahead = 0; // cuántas hay por delante, contando esas dos
};

class NoteQueue {
public:
    // Mundo nuevo: cola vacía, ids desde 1, revisión y borradas a 0. `stage_length`: longitud del
    // eje (tope del ancla).
    void reset(double stage_length);

    // Aplica una orden en el orden en que llega. Sin reservas. `car_s`: la de
    // SimWorld::stage_run() del host (solo la usa ClearPending).
    NoteApplied apply(const NoteOrder& order, double car_s);

    // Sin reservas.
    NotesView view(double car_s) const;

    int size() const { return count_; }
    const Note& at(int index) const { return notes_[static_cast<std::size_t>(index)]; } // llegada
    bool has_last() const { return count_ > 0; }
    const Note& last() const { return notes_[static_cast<std::size_t>(count_ - 1)]; }

    // Sube en 1 en cada apply(), se aplique o no (la red manda el estado si cambia).
    std::uint32_t revision() const { return revision_; }
    NoteResult last_result() const { return last_result_; }

    // Borradas, para tacharlas una vez cada una: contador total y las últimas kMaxNotes. El panel
    // guarda el último total visto y lee de ahí hasta erased_total(). Vuelve a 0 con reset().
    std::uint32_t erased_total() const { return erased_total_; }
    const Note& erased(std::uint32_t index) const { return erased_[index % kMaxNotes]; }

private:
    void push_erased(const Note& note);

    std::array<Note, kMaxNotes> notes_{};
    std::array<Note, kMaxNotes> erased_{};
    int count_ = 0;
    double length_ = 0.0;
    std::uint32_t next_id_ = 1;
    std::uint32_t revision_ = 0;
    std::uint32_t erased_total_ = 0;
    NoteResult last_result_ = NoteResult::Applied;
};

// Textos en español, para el registro y los tests. Las marcas, en un orden fijo: se cierra, se
// abre, no cortes, larga, cresta, salto, cuidado.
//   note_call_text: «3 izquierda se cierra», «H derecha», «cuidado», «6 derecha cresta».
//   distance_text: «», «y», «80», «300+».
//   note_text: las dos, separadas por un espacio: «3 izquierda larga 80».
std::string note_call_text(const Note& note);
std::string distance_text(const NoteDistance& distance);
std::string note_text(const Note& note);

// Distancia real para el registro: del final del cambio de dirección más cercano al ancla al
// principio del siguiente. «Más cercano»: el de menor distancia del ancla a [s_start, s_end] (0 si
// cae dentro); a igual distancia, el de s_start mayor. false si no hay cambios o el más cercano es
// el último. No reserva.
bool real_distance_after(const std::vector<DirectionChange>& changes, double anchor_s,
                         double& metres);

} // namespace rally::game
