// Panel del salpicadero (lote 7, tarea panel): qué va en cada hueco, cuándo se tacha una nota
// borrada y cuándo suena un pitido y por qué lado. Sin ventana.

#include "game/notes.h"
#include "platform/note_panel.h"

#include <doctest/doctest.h>

#include <cstdint>

using namespace rally;
using namespace rally::platform;
using game::DistanceKind;
using game::NoteGrade;
using game::NoteMark;
using game::NoteSide;

namespace {

constexpr std::int64_t kFrame30 = 33'333'333; // un fotograma a 30 fps
constexpr std::int64_t kMs = 1'000'000;

game::NoteQueue make_queue() {
    game::NoteQueue q;
    q.reset(2000.0);
    return q;
}

void sing(game::NoteQueue& q, double s, NoteGrade grade, NoteSide side) {
    q.apply(game::corner_order(s, grade, side), 0.0);
}

void sing_hazard(game::NoteQueue& q, double s, NoteMark mark) {
    q.apply(game::hazard_order(s, mark), 0.0);
}

// Cuatro notas: 3 izquierda a 100 m (80 m detrás), 5 derecha a 200 m (y), H izquierda a 300 m
// (300+) y un salto a 400 m.
game::NoteQueue four_notes() {
    game::NoteQueue q = make_queue();
    sing(q, 100.0, NoteGrade::G3, NoteSide::Left);
    q.apply(game::distance_order(game::counter_distance(80)), 0.0);
    sing(q, 200.0, NoteGrade::G5, NoteSide::Right);
    q.apply(game::distance_order(game::wheel_distance(0)), 0.0);
    sing(q, 300.0, NoteGrade::Hairpin, NoteSide::Left);
    q.apply(game::distance_order(game::wheel_distance(7)), 0.0);
    sing_hazard(q, 400.0, NoteMark::Jump);
    return q;
}

} // namespace

TEST_CASE("note panel: the three slots follow a car that advances") {
    const game::NoteQueue q = four_notes();
    NotePanel panel;
    std::int64_t t = 0;
    const auto at = [&](double s) -> const PanelView& {
        panel.update(q, s, t);
        t += kFrame30;
        return panel.view();
    };
    // Antes de la primera: la 3 izquierda a la izquierda, su distancia al centro y la 5 a la
    // derecha.
    PanelView v = at(0.0);
    CHECK(v.left.kind == PanelSlotKind::Corner);
    CHECK(v.left.note.grade == NoteGrade::G3);
    CHECK(v.left.note.side == NoteSide::Left);
    CHECK(v.has_center);
    CHECK(v.center.kind == DistanceKind::Metres);
    CHECK(v.center.metres == 80);
    CHECK(v.right.note.grade == NoteGrade::G5);
    // Una nota en su ancla exacta sigue por delante: la fila no corre hasta pasarla.
    v = at(100.0);
    CHECK(v.left.note.grade == NoteGrade::G3);
    // Al pasar el ancla la fila corre: la 5 a la izquierda, con «y», y la H a la derecha.
    v = at(100.001);
    CHECK(v.left.note.grade == NoteGrade::G5);
    CHECK(v.center.kind == DistanceKind::Link);
    CHECK(v.right.note.grade == NoteGrade::Hairpin);
    v = at(250.0);
    CHECK(v.left.note.grade == NoteGrade::Hairpin);
    CHECK(v.center.kind == DistanceKind::Over300);
    // Un peligro sin curva ocupa un hueco de nota.
    CHECK(v.right.kind == PanelSlotKind::Hazard);
    CHECK((v.right.note.marks & game::mark_bit(NoteMark::Jump)) != 0);
    v = at(350.0);
    CHECK(v.left.kind == PanelSlotKind::Hazard);
    CHECK_FALSE(v.has_center); // el peligro no lleva distancia
    CHECK(v.right.kind == PanelSlotKind::Empty);
    // Tras la última, vacío.
    v = at(450.0);
    CHECK(v.left.kind == PanelSlotKind::Empty);
    CHECK(v.right.kind == PanelSlotKind::Empty);
    CHECK_FALSE(v.has_center);
}

TEST_CASE("note panel: a jump forward skips notes and a relocation behind brings them back") {
    const game::NoteQueue q = four_notes();
    NotePanel panel;
    std::int64_t t = 0;
    panel.update(q, 0.0, t);
    CHECK(panel.view().left.note.grade == NoteGrade::G3);
    // Un salto hacia delante de 310 m: salta tres notas.
    t += kFrame30;
    panel.update(q, 310.0, t);
    CHECK(panel.view().left.kind == PanelSlotKind::Hazard);
    // Recolocar por detrás de todo: vuelven las cuatro.
    t += kFrame30;
    panel.update(q, 0.0, t);
    CHECK(panel.view().left.note.grade == NoteGrade::G3);
    CHECK(panel.view().right.note.grade == NoteGrade::G5);
    // Y por detrás solo de la segunda: la segunda y la tercera.
    t += kFrame30;
    panel.update(q, 150.0, t);
    CHECK(panel.view().left.note.grade == NoteGrade::G5);
    CHECK(panel.view().right.note.grade == NoteGrade::Hairpin);
}

TEST_CASE("note panel: a note beeps once on its own side") {
    game::NoteQueue q = four_notes();
    NotePanel panel;
    std::int64_t t = 0;
    panel.update(q, 0.0, t);
    CHECK(panel.sounds().beeps_left == 1); // la 3 izquierda
    CHECK(panel.sounds().beeps_right == 0);
    // Preguntar otra vez, y otra, sin que cambie nada: no suena más.
    for (int i = 0; i < 20; ++i) {
        t += kFrame30;
        panel.update(q, 1.0 + i, t);
    }
    CHECK(panel.sounds().beeps_left == 1);
    // La 5 derecha entra al pasar la primera.
    t += kFrame30;
    panel.update(q, 100.5, t);
    CHECK(panel.sounds().beeps_left == 1);
    CHECK(panel.sounds().beeps_right == 1);
    // La H izquierda.
    t += kFrame30;
    panel.update(q, 200.5, t);
    CHECK(panel.sounds().beeps_left == 2);
    // El peligro sin curva: un solo suceso para los dos lados, con su propio tono.
    t += kFrame30;
    panel.update(q, 300.5, t);
    CHECK(panel.sounds().hazard_beeps == 1);
    CHECK(panel.sounds().beeps_left == 2);
    CHECK(panel.sounds().beeps_right == 1);
    CHECK(panel.sounds().erase_beeps == 0);
}

TEST_CASE("note panel: a late frame beeps only the note that reaches the slot") {
    const game::NoteQueue q = four_notes();
    NotePanel panel;
    panel.update(q, 0.0, 0);
    CHECK(panel.sounds().beeps_left == 1);
    // Un fotograma tardío: el coche pasa tres anclas de golpe. Las saltadas nunca estuvieron en el
    // hueco izquierdo: solo suena la que llega, el peligro, una vez.
    panel.update(q, 310.0, 500 * kMs);
    panel.update(q, 310.0, 500 * kMs + kFrame30);
    CHECK(panel.sounds().beeps_left == 1);
    CHECK(panel.sounds().beeps_right == 0);
    CHECK(panel.sounds().hazard_beeps == 1);
    // Recolocarse por detrás: la primera vuelve y suena otra vez al entrar.
    panel.update(q, 0.0, 600 * kMs);
    CHECK(panel.sounds().beeps_left == 2);
}

TEST_CASE("note panel: two notes in the same frame give one beep, the other on arrival") {
    game::NoteQueue q = make_queue();
    NotePanel panel;
    panel.update(q, 0.0, 0);
    CHECK(panel.sounds() == PanelSounds{});
    sing(q, 120.0, NoteGrade::G4, NoteSide::Right);
    sing(q, 60.0, NoteGrade::G2, NoteSide::Left);
    panel.update(q, 0.0, kFrame30);
    // La de 60 m queda a la izquierda y pita por la izquierda; la de 120 m, a la derecha, espera.
    CHECK(panel.view().left.note.grade == NoteGrade::G2);
    CHECK(panel.view().right.note.grade == NoteGrade::G4);
    CHECK(panel.sounds().beeps_left == 1);
    CHECK(panel.sounds().beeps_right == 0);
    panel.update(q, 61.0, 2 * kFrame30);
    CHECK(panel.sounds().beeps_right == 1);
    CHECK(panel.sounds().beeps_left == 1);
}

TEST_CASE("note panel: erasing gives one tone and the struck note shows for half a second") {
    game::NoteQueue q = make_queue();
    sing(q, 100.0, NoteGrade::G3, NoteSide::Left);
    sing(q, 200.0, NoteGrade::G5, NoteSide::Right);
    NotePanel panel;
    std::int64_t t = 0;
    panel.update(q, 0.0, t);
    CHECK_FALSE(panel.view().right.struck);
    // El copiloto borra la última (la derecha del panel).
    q.apply(game::erase_last_order(), 0.0);
    const std::int64_t erased_at = t + kFrame30;
    t = erased_at;
    panel.update(q, 0.0, t);
    CHECK(panel.sounds().erase_beeps == 1);
    CHECK(panel.view().right.struck);
    CHECK(panel.view().right.note.grade == NoteGrade::G5);
    CHECK(panel.view().left.note.grade == NoteGrade::G3);
    // La tachadura dura 0,5 s (± un fotograma a 30 fps) y desaparece.
    std::int64_t first_clear = -1;
    std::int64_t last_struck = erased_at;
    for (int i = 1; i < 40; ++i) {
        t = erased_at + i * kFrame30;
        panel.update(q, 0.0, t);
        if (panel.view().right.struck) {
            last_struck = t;
        } else if (first_clear < 0) {
            first_clear = t;
        }
    }
    REQUIRE(first_clear > 0);
    CHECK(last_struck - erased_at <= 500 * kMs);
    CHECK(last_struck - erased_at >= 500 * kMs - kFrame30);
    CHECK(first_clear - erased_at >= 500 * kMs);
    CHECK(first_clear - erased_at <= 500 * kMs + kFrame30);
    CHECK(panel.view().right.kind == PanelSlotKind::Empty);
    CHECK(panel.sounds().erase_beeps == 1); // un solo tono
}

TEST_CASE("note panel: a struck left note makes the live one wait, then it beeps on entering") {
    game::NoteQueue q = make_queue();
    sing(q, 100.0, NoteGrade::G3, NoteSide::Left);
    NotePanel panel;
    panel.update(q, 0.0, 0);
    CHECK(panel.sounds().beeps_left == 1);
    q.apply(game::erase_last_order(), 0.0);
    sing(q, 150.0, NoteGrade::G6, NoteSide::Right);
    panel.update(q, 0.0, 100 * kMs);
    // La borrada, tachada a la izquierda; la nueva espera en el hueco derecho, sin pitar.
    CHECK(panel.view().left.struck);
    CHECK(panel.view().left.note.grade == NoteGrade::G3);
    CHECK(panel.view().right.note.grade == NoteGrade::G6);
    CHECK_FALSE(panel.view().has_center);
    CHECK(panel.sounds().beeps_right == 0);
    CHECK(panel.sounds().erase_beeps == 1);
    // Acabada la tachadura, la viva entra a la izquierda y pita por su lado.
    panel.update(q, 0.0, 700 * kMs);
    CHECK_FALSE(panel.view().left.struck);
    CHECK(panel.view().left.note.grade == NoteGrade::G6);
    CHECK(panel.sounds().beeps_right == 1);
}

TEST_CASE("note panel: a hidden note that is erased only gives the tone") {
    game::NoteQueue q = make_queue();
    sing(q, 100.0, NoteGrade::G3, NoteSide::Left);
    sing(q, 200.0, NoteGrade::G4, NoteSide::Left);
    sing(q, 300.0, NoteGrade::G5, NoteSide::Left); // oculta: no cabe en los dos huecos
    NotePanel panel;
    panel.update(q, 0.0, 0);
    q.apply(game::erase_last_order(), 0.0);
    panel.update(q, 0.0, kFrame30);
    CHECK_FALSE(panel.view().left.struck);
    CHECK_FALSE(panel.view().right.struck);
    CHECK(panel.sounds().erase_beeps == 1);
}

TEST_CASE("note panel: clearing the pending notes strikes both slots with a single tone") {
    game::NoteQueue q = make_queue();
    sing(q, 100.0, NoteGrade::G3, NoteSide::Left);
    sing(q, 200.0, NoteGrade::G4, NoteSide::Right);
    NotePanel panel;
    panel.update(q, 0.0, 0);
    q.apply(game::clear_pending_order(), 0.0);
    panel.update(q, 0.0, kFrame30);
    CHECK(panel.view().left.struck);
    CHECK(panel.view().right.struck);
    CHECK(panel.sounds().erase_beeps == 1);
    panel.update(q, 0.0, kFrame30 + 600 * kMs);
    CHECK(panel.view().left.kind == PanelSlotKind::Empty);
    CHECK(panel.view().right.kind == PanelSlotKind::Empty);
}

TEST_CASE("note panel: a new world starts from nothing") {
    game::NoteQueue q = four_notes();
    NotePanel panel;
    panel.update(q, 0.0, 0);
    q.apply(game::erase_last_order(), 0.0);
    panel.update(q, 0.0, kFrame30);
    REQUIRE(panel.sounds().beeps_left > 0);
    panel.reset();
    CHECK(panel.sounds() == PanelSounds{});
    q.reset(2000.0); // la cola vuelve a 0 borradas
    panel.update(q, 0.0, 2 * kFrame30);
    CHECK(panel.view().left.kind == PanelSlotKind::Empty);
    CHECK(panel.sounds() == PanelSounds{});
}
