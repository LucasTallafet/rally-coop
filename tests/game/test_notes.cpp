#include "game/direction_changes.h"
#include "game/notes.h"
#include "new_counter.h"
#include "sim/pcg32.h"
#include "sim/stage_axis.h"
#include "stage_driver.h"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

using namespace rally::game;

namespace {

NoteOrder corner(double s, NoteGrade g, NoteSide side) { return corner_order(s, g, side); }

// Modelo de referencia, escrito aparte de NoteQueue: un vector y las reglas de docs/01 §5.3.
struct Model {
    std::vector<Note> notes;
    std::vector<std::uint32_t> erased_ids;
    std::uint32_t next_id = 1;
    double length = 0.0;

    NoteResult apply(const NoteOrder& o, double car_s) {
        if (!valid(o)) {
            return NoteResult::Invalid;
        }
        switch (o.kind) {
        case NoteOrderKind::Corner:
        case NoteOrderKind::Hazard: {
            if (notes.size() >= static_cast<std::size_t>(kMaxNotes)) {
                return NoteResult::Full;
            }
            Note n;
            n.id = next_id++;
            n.s = o.s < 0.0 ? 0.0 : (o.s > length ? length : o.s);
            if (o.kind == NoteOrderKind::Corner) {
                n.grade = o.grade;
                n.side = o.side;
            } else {
                n.marks = static_cast<std::uint8_t>(1u << static_cast<unsigned>(o.mark));
            }
            notes.push_back(n);
            return NoteResult::Applied;
        }
        case NoteOrderKind::Mark:
            if (notes.empty()) {
                return NoteResult::NoNote;
            }
            notes.back().marks = static_cast<std::uint8_t>(notes.back().marks |
                                                           (1u << static_cast<unsigned>(o.mark)));
            return NoteResult::Applied;
        case NoteOrderKind::Distance:
            if (notes.empty()) {
                return NoteResult::NoNote;
            }
            notes.back().distance = o.distance;
            return NoteResult::Applied;
        case NoteOrderKind::EraseLast:
            if (notes.empty()) {
                return NoteResult::NoNote;
            }
            erased_ids.push_back(notes.back().id);
            notes.pop_back();
            return NoteResult::Applied;
        case NoteOrderKind::ClearPending: {
            std::vector<Note> keep;
            for (const Note& n : notes) {
                if (n.s >= car_s) {
                    erased_ids.push_back(n.id);
                } else {
                    keep.push_back(n);
                }
            }
            notes = keep;
            return NoteResult::Applied;
        }
        }
        return NoteResult::Invalid;
    }

    // Las dos primeras por delante, por ancla y, a igual ancla, por llegada.
    NotesView view(double car_s) const {
        NotesView v;
        std::vector<Note> ahead;
        for (const Note& n : notes) {
            if (n.s >= car_s) {
                ahead.push_back(n);
            }
        }
        for (std::size_t i = 1; i < ahead.size(); ++i) { // inserción estable
            for (std::size_t j = i; j > 0 && ahead[j].s < ahead[j - 1].s; --j) {
                std::swap(ahead[j], ahead[j - 1]);
            }
        }
        v.ahead = static_cast<int>(ahead.size());
        if (!ahead.empty()) {
            v.has_next = true;
            v.next = ahead[0];
        }
        if (ahead.size() > 1) {
            v.has_after = true;
            v.after = ahead[1];
        }
        return v;
    }
};

NoteOrder random_order(rally::sim::Pcg32& rng, double length) {
    const std::uint32_t pick = rng.next_u32() % 100;
    const double s = static_cast<double>(rng.next_u32() % 100000) / 100000.0 * length;
    if (pick < 50) {
        return corner(s, static_cast<NoteGrade>(1 + rng.next_u32() % 7),
                      rng.next_u32() % 2 == 0 ? NoteSide::Left : NoteSide::Right);
    }
    if (pick < 58) {
        const NoteMark hazards[] = {NoteMark::Crest, NoteMark::Jump, NoteMark::Caution};
        return hazard_order(s, hazards[rng.next_u32() % 3]);
    }
    if (pick < 70) {
        return mark_order(static_cast<NoteMark>(rng.next_u32() % kNoteMarkCount));
    }
    if (pick < 85) {
        return rng.next_u32() % 2 == 0
                   ? distance_order(wheel_distance(static_cast<int>(rng.next_u32() % 8)))
                   : distance_order(counter_distance(static_cast<int>(rng.next_u32() % 100) * 10));
    }
    if (pick < 96) {
        return erase_last_order();
    }
    return clear_pending_order();
}

void check_same(const NoteQueue& q, const Model& m) {
    REQUIRE(q.size() == static_cast<int>(m.notes.size()));
    for (int i = 0; i < q.size(); ++i) {
        CHECK(q.at(i) == m.notes[static_cast<std::size_t>(i)]);
    }
}

} // namespace

TEST_CASE("notes: each order does what the design says") {
    NoteQueue q;
    q.reset(1000.0);

    // Casos límite con la cola vacía: no hacen nada.
    CHECK(q.apply(mark_order(NoteMark::Tightens), 0.0).result == NoteResult::NoNote);
    CHECK(q.apply(distance_order(wheel_distance(3)), 0.0).result == NoteResult::NoNote);
    CHECK(q.apply(erase_last_order(), 0.0).result == NoteResult::NoNote);
    CHECK(q.size() == 0);
    CHECK(q.revision() == 3); // se cuentan aunque no hagan nada
    CHECK(q.last_result() == NoteResult::NoNote);

    // Curva.
    NoteApplied a = q.apply(corner(100.0, NoteGrade::G3, NoteSide::Left), 0.0);
    CHECK(a.result == NoteResult::Applied);
    REQUIRE(a.has_note);
    CHECK(a.note.id == 1);
    CHECK(a.note.s == 100.0);
    CHECK(a.note.grade == NoteGrade::G3);
    CHECK(a.note.side == NoteSide::Left);
    CHECK(a.note.marks == 0);
    CHECK(a.note.distance.kind == DistanceKind::None);

    // Modificadores a la última: se acumulan y repetir uno no cambia nada.
    q.apply(mark_order(NoteMark::Tightens), 0.0);
    q.apply(mark_order(NoteMark::Long), 0.0);
    a = q.apply(mark_order(NoteMark::Tightens), 0.0);
    CHECK(a.result == NoteResult::Applied);
    CHECK(q.last().marks == (mark_bit(NoteMark::Tightens) | mark_bit(NoteMark::Long)));
    CHECK(q.last().grade == NoteGrade::G3);

    // Distancia a la última, y una segunda la sustituye.
    q.apply(distance_order(wheel_distance(0)), 0.0);
    CHECK(q.last().distance.kind == DistanceKind::Link);
    CHECK(q.last().distance.source == DistanceSource::Wheel);
    q.apply(distance_order(counter_distance(990)), 0.0);
    CHECK(q.last().distance.kind == DistanceKind::Metres);
    CHECK(q.last().distance.metres == 990);
    CHECK(q.last().distance.source == DistanceSource::Counter);
    q.apply(distance_order(counter_distance(0)), 0.0);
    CHECK(q.last().distance.metres == 0);
    CHECK(q.last().distance.kind == DistanceKind::Metres);

    // Peligro solo, y dos notas en la misma ancla: las dos, en orden de llegada.
    a = q.apply(hazard_order(100.0, NoteMark::Crest), 0.0);
    REQUIRE(a.has_note);
    CHECK(a.note.grade == NoteGrade::None);
    CHECK(a.note.side == NoteSide::None);
    CHECK(a.note.marks == mark_bit(NoteMark::Crest));
    REQUIRE(q.size() == 2);
    CHECK(q.at(0).id == 1);
    CHECK(q.at(1).id == 2);
    CHECK(q.at(0).s == q.at(1).s);
    // Un modificador y una distancia valen también sobre un peligro solo: el juego no filtra.
    q.apply(mark_order(NoteMark::Caution), 0.0);
    CHECK(q.last().marks == (mark_bit(NoteMark::Crest) | mark_bit(NoteMark::Caution)));

    // Borrar la última: la quita y la deja para tachar, una vez.
    CHECK(q.erased_total() == 0);
    a = q.apply(erase_last_order(), 0.0);
    CHECK(a.result == NoteResult::Applied);
    CHECK(a.erased == 1);
    CHECK(a.note.id == 2);
    CHECK(q.size() == 1);
    CHECK(q.erased_total() == 1);
    CHECK(q.erased(0).id == 2);

    // Vaciar quita solo las pendientes (ancla por delante del coche).
    q.apply(corner(300.0, NoteGrade::G5, NoteSide::Right), 0.0);
    q.apply(corner(500.0, NoteGrade::Hairpin, NoteSide::Right), 0.0);
    a = q.apply(clear_pending_order(), 250.0);
    CHECK(a.result == NoteResult::Applied);
    CHECK(a.erased == 2);
    REQUIRE(q.size() == 1);
    CHECK(q.at(0).id == 1); // la de 100 m ya estaba pasada
    CHECK(q.erased_total() == 3);
    // Vaciar sin pendientes tampoco falla.
    a = q.apply(clear_pending_order(), 250.0);
    CHECK(a.result == NoteResult::Applied);
    CHECK(a.erased == 0);

    // reset(): ids, revisión y borradas vuelven a empezar.
    q.reset(500.0);
    CHECK(q.size() == 0);
    CHECK(q.revision() == 0);
    CHECK(q.erased_total() == 0);
    CHECK(q.apply(corner(1.0, NoteGrade::G1, NoteSide::Left), 0.0).note.id == 1);
}

TEST_CASE("notes: invalid orders are rejected and change nothing") {
    NoteQueue q;
    q.reset(1000.0);
    q.apply(corner(10.0, NoteGrade::G4, NoteSide::Right), 0.0);
    const Note before = q.last();
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    NoteDistance far = counter_distance(10);
    far.metres = 991;
    NoteDistance no_source = wheel_distance(3);
    no_source.source = DistanceSource::None;
    NoteDistance metres_on_link = wheel_distance(0);
    metres_on_link.metres = 5;
    const NoteOrder bad[] = {
        corner(10.0, NoteGrade::None, NoteSide::Left),
        corner(10.0, static_cast<NoteGrade>(8), NoteSide::Left),
        corner(10.0, NoteGrade::G2, NoteSide::None),
        corner(nan, NoteGrade::G2, NoteSide::Left),
        corner(inf, NoteGrade::G2, NoteSide::Left),
        hazard_order(10.0, NoteMark::Long), // «larga» no es un peligro
        hazard_order(nan, NoteMark::Jump),
        mark_order(static_cast<NoteMark>(7)),
        distance_order(far),
        distance_order(no_source),
        distance_order(metres_on_link),
    };
    for (const NoteOrder& o : bad) {
        CHECK_FALSE(valid(o));
        const NoteApplied a = q.apply(o, 0.0);
        CHECK(a.result == NoteResult::Invalid);
        CHECK_FALSE(a.has_note);
    }
    CHECK(q.size() == 1);
    CHECK(q.last() == before);
    CHECK(q.erased_total() == 0);
}

TEST_CASE("notes: the anchor is clamped to the stage and nothing else changes") {
    NoteQueue q;
    q.reset(1077.7);
    NoteApplied a = q.apply(corner(-3.0, NoteGrade::G2, NoteSide::Left), 0.0);
    CHECK(a.note.s == 0.0);
    CHECK(a.note.grade == NoteGrade::G2);
    CHECK(a.note.side == NoteSide::Left);
    a = q.apply(corner(5000.0, NoteGrade::G6, NoteSide::Right), 0.0);
    CHECK(a.note.s == 1077.7);
    CHECK(a.note.grade == NoteGrade::G6);
    a = q.apply(hazard_order(1077.7, NoteMark::Jump), 0.0);
    CHECK(a.note.s == 1077.7); // dentro del tramo: tal cual, bit a bit
    a = q.apply(corner(123.456789, NoteGrade::G4, NoteSide::Left), 0.0);
    CHECK(a.note.s == 123.456789);
}

TEST_CASE("notes: next and after for a car that advances, jumps ahead and goes back") {
    NoteQueue q;
    q.reset(2000.0);
    q.apply(corner(100.0, NoteGrade::G3, NoteSide::Right), 0.0);
    q.apply(corner(390.0, NoteGrade::G2, NoteSide::Left), 0.0);
    q.apply(corner(520.0, NoteGrade::G4, NoteSide::Right), 0.0);

    NotesView v = q.view(-5.0); // antes de la línea
    CHECK(v.ahead == 3);
    CHECK(v.next.s == 100.0);
    CHECK(v.after.s == 390.0);

    // Avanzando: la fila corre al pasar el ancla (una nota en `s` exacta aún está por delante).
    v = q.view(100.0);
    CHECK(v.next.s == 100.0);
    v = q.view(101.0);
    CHECK(v.ahead == 2);
    CHECK(v.next.s == 390.0);
    CHECK(v.after.s == 520.0);
    v = q.view(450.0);
    CHECK(v.ahead == 1);
    CHECK(v.has_next);
    CHECK_FALSE(v.has_after);
    CHECK(v.next.s == 520.0);

    // Atajo: salta hacia delante, pasa varias de golpe.
    v = q.view(600.0);
    CHECK(v.ahead == 0);
    CHECK_FALSE(v.has_next);
    CHECK_FALSE(v.has_after);

    // Recolocación por detrás: las notas reaparecen, no se gastaron.
    v = q.view(50.0);
    CHECK(v.ahead == 3);
    CHECK(v.next.s == 100.0);
    CHECK(v.after.s == 390.0);

    // Una nota anclada por detrás del coche (el cursor llega con retraso): se acepta, no sale en
    // la vista, no cuenta y reaparece al volver atrás.
    const NoteApplied a = q.apply(corner(200.0, NoteGrade::G5, NoteSide::Left), 300.0);
    CHECK(a.result == NoteResult::Applied);
    v = q.view(300.0);
    CHECK(v.ahead == 2);
    CHECK(v.next.s == 390.0);
    v = q.view(150.0);
    CHECK(v.ahead == 3);
    CHECK(v.next.s == 200.0); // por ancla, no por llegada
    CHECK(v.after.s == 390.0);

    // A igual ancla, la que llegó antes va primero.
    q.reset(2000.0);
    q.apply(corner(100.0, NoteGrade::G1, NoteSide::Left), 0.0);
    q.apply(corner(100.0, NoteGrade::G6, NoteSide::Right), 0.0);
    v = q.view(0.0);
    CHECK(v.next.grade == NoteGrade::G1);
    CHECK(v.after.grade == NoteGrade::G6);
}

TEST_CASE("notes: events are seen exactly once however late the panel asks") {
    for (const int period : {1, 7, 50}) {
        INFO("el panel pregunta cada ", period, " órdenes");
        rally::sim::Pcg32 rng(77, 5);
        NoteQueue q;
        Model model;
        const double length = 3000.0;
        q.reset(length);
        model.length = length;

        std::uint32_t seen_erased = 0;
        std::vector<std::uint32_t> panel_erased;
        std::vector<std::uint32_t> panel_next; // ids que el panel vio como «nueva»
        std::vector<std::uint32_t> dense_next; // lo mismo, preguntando tras cada orden
        std::uint32_t last_next = 0, last_dense = 0;
        double car_s = -5.0;
        for (int i = 1; i <= 600; ++i) {
            car_s += 4.0; // el coche avanza
            const NoteOrder o = random_order(rng, length);
            model.apply(o, car_s);
            q.apply(o, car_s);
            const NotesView v = q.view(car_s);
            const std::uint32_t dense_id = v.has_next ? v.next.id : 0;
            if (dense_id != last_dense) {
                dense_next.push_back(dense_id);
                last_dense = dense_id;
            }
            if (i % period == 0) {
                const std::uint32_t id = v.has_next ? v.next.id : 0;
                if (id != last_next) {
                    panel_next.push_back(id);
                    last_next = id;
                }
                REQUIRE(q.erased_total() - seen_erased <= static_cast<std::uint32_t>(kMaxNotes));
                for (; seen_erased < q.erased_total(); ++seen_erased) {
                    panel_erased.push_back(q.erased(seen_erased).id);
                }
            }
        }
        // Todas las borradas, una vez cada una y en orden (las del modelo, no las de la cola).
        std::uint32_t pending = q.erased_total();
        for (; seen_erased < pending; ++seen_erased) {
            panel_erased.push_back(q.erased(seen_erased).id);
        }
        CHECK(panel_erased == model.erased_ids);
        CHECK(model.erased_ids.size() > 10); // el test ejercita el caso
        // Las notas nuevas: una subsecuencia de las que vería quien pregunta siempre, sin repetir
        // dos veces seguidas la misma.
        std::size_t at = 0;
        for (std::size_t k = 0; k < panel_next.size(); ++k) {
            if (k > 0) {
                CHECK(panel_next[k] != panel_next[k - 1]);
            }
            while (at < dense_next.size() && dense_next[at] != panel_next[k]) {
                ++at;
            }
            CHECK(at < dense_next.size());
            ++at;
        }
        CHECK(panel_next.size() >= 1);
    }

    // Un vaciado de 256 notas se ve entero.
    NoteQueue q;
    q.reset(5000.0);
    for (int i = 0; i < kMaxNotes; ++i) {
        q.apply(corner(1000.0 + i, NoteGrade::G3, NoteSide::Left), 0.0);
    }
    const NoteApplied a = q.apply(clear_pending_order(), 0.0);
    CHECK(a.erased == kMaxNotes);
    CHECK(q.erased_total() == static_cast<std::uint32_t>(kMaxNotes));
    for (std::uint32_t i = 0; i < q.erased_total(); ++i) {
        CHECK(q.erased(i).id == i + 1);
    }
}

TEST_CASE("notes: a full queue rejects the note and the next one fits after a delete") {
    NoteQueue q;
    q.reset(5000.0);
    for (int i = 0; i < kMaxNotes; ++i) {
        CHECK(q.apply(corner(i, NoteGrade::G4, NoteSide::Right), 0.0).result ==
              NoteResult::Applied);
    }
    CHECK(q.size() == kMaxNotes);
    const std::uint32_t revision = q.revision();
    NoteApplied a = q.apply(corner(300.0, NoteGrade::G1, NoteSide::Left), 0.0);
    CHECK(a.result == NoteResult::Full);
    CHECK_FALSE(a.has_note);
    CHECK(q.size() == kMaxNotes);
    CHECK(q.last_result() == NoteResult::Full);
    CHECK(q.revision() == revision + 1);
    a = q.apply(hazard_order(300.0, NoteMark::Crest), 0.0);
    CHECK(a.result == NoteResult::Full);
    // Modificadores y distancias siguen valiendo con la cola llena.
    CHECK(q.apply(mark_order(NoteMark::Opens), 0.0).result == NoteResult::Applied);
    q.apply(erase_last_order(), 0.0);
    CHECK(q.apply(corner(300.0, NoteGrade::G1, NoteSide::Left), 0.0).result == NoteResult::Applied);
}

TEST_CASE("notes: applying orders and asking the queue make no allocations") {
    rally::sim::Pcg32 rng(5, 9);
    NoteQueue q;
    q.reset(2000.0);
    std::vector<NoteOrder> orders;
    for (int i = 0; i < 10000; ++i) {
        orders.push_back(random_order(rng, 2000.0));
    }
    const std::uint64_t before = rally::tests::global_new_count();
    int ahead = 0;
    for (int i = 0; i < 10000; ++i) {
        q.apply(orders[static_cast<std::size_t>(i)], static_cast<double>(i % 2000));
        ahead += q.view(static_cast<double>((i * 7) % 2000)).ahead;
        ahead += static_cast<int>(q.erased_total() & 1u);
    }
    CHECK(rally::tests::global_new_count() == before);
    CHECK(ahead > 0);
}

TEST_CASE("notes: the game never changes a note (300 random orders against a reference)") {
    for (const std::uint64_t seed : {1u, 2u, 3u}) {
        rally::sim::Pcg32 rng(seed, 11);
        NoteQueue q;
        Model m;
        q.reset(1500.0);
        m.length = 1500.0;
        double car_s = 0.0;
        for (int i = 0; i < 300; ++i) {
            car_s = static_cast<double>(rng.next_u32() % 1500);
            const NoteOrder o = random_order(rng, 1500.0);
            const NoteResult expected = m.apply(o, car_s);
            const NoteApplied got = q.apply(o, car_s);
            REQUIRE(got.result == expected);
            check_same(q, m);
            const NotesView v = q.view(car_s);
            const NotesView ref = m.view(car_s);
            CHECK(v.ahead == ref.ahead);
            CHECK(v.has_next == ref.has_next);
            CHECK(v.has_after == ref.has_after);
            if (v.has_next) {
                CHECK(v.next == ref.next);
            }
            if (v.has_after) {
                CHECK(v.after == ref.after);
            }
        }
        REQUIRE(q.erased_total() == m.erased_ids.size());
        for (std::uint32_t i = 0; i < q.erased_total(); ++i) {
            CHECK(q.erased(i).id == m.erased_ids[i]);
        }
    }
}

TEST_CASE("notes: the text of a note") {
    auto make = [](NoteGrade g, NoteSide s, std::uint8_t marks, NoteDistance d) {
        Note n;
        n.id = 1;
        n.grade = g;
        n.side = s;
        n.marks = marks;
        n.distance = d;
        return n;
    };
    const NoteDistance none;
    CHECK(note_text(make(NoteGrade::G3, NoteSide::Left, mark_bit(NoteMark::Long),
                         counter_distance(80))) == "3 izquierda larga 80");
    CHECK(note_text(make(NoteGrade::Hairpin, NoteSide::Right, 0, none)) == "H derecha");
    CHECK(note_text(make(NoteGrade::None, NoteSide::None, mark_bit(NoteMark::Caution), none)) ==
          "cuidado");
    CHECK(note_text(make(NoteGrade::G6, NoteSide::Right, mark_bit(NoteMark::Crest), none)) ==
          "6 derecha cresta");
    CHECK(note_text(make(NoteGrade::G6, NoteSide::Right, mark_bit(NoteMark::Crest),
                         wheel_distance(0))) == "6 derecha cresta y");
    CHECK(note_text(make(NoteGrade::G2, NoteSide::Left,
                         mark_bit(NoteMark::Tightens) | mark_bit(NoteMark::DontCut),
                         wheel_distance(7))) == "2 izquierda se cierra no cortes 300+");
    CHECK(note_text(make(NoteGrade::G1, NoteSide::Right, mark_bit(NoteMark::Opens), none)) ==
          "1 derecha se abre");
    CHECK(note_text(make(NoteGrade::G4, NoteSide::Left, 0, counter_distance(0))) ==
          "4 izquierda 0");
    CHECK(note_text(make(NoteGrade::G5, NoteSide::Right, 0, wheel_distance(6))) == "5 derecha 200");
    CHECK(note_text(make(NoteGrade::None, NoteSide::None,
                         mark_bit(NoteMark::Jump) | mark_bit(NoteMark::Crest),
                         counter_distance(990))) == "cresta salto 990");
    CHECK(note_call_text(make(NoteGrade::G3, NoteSide::Left, 0, counter_distance(50))) ==
          "3 izquierda");
    CHECK(distance_text(none) == "");
    CHECK(distance_text(wheel_distance(0)) == "y");
    CHECK(distance_text(wheel_distance(4)) == "100");
    CHECK(distance_text(wheel_distance(7)) == "300+");
    // La rueda: y, 30, 50, 80, 100, 150, 200, 300+.
    const char* const wheel[8] = {"y", "30", "50", "80", "100", "150", "200", "300+"};
    for (int i = 0; i < 8; ++i) {
        CHECK(distance_text(wheel_distance(i)) == wheel[i]);
        CHECK(valid(wheel_distance(i)));
    }
    CHECK(counter_distance(2000).metres == 990);
    CHECK(counter_distance(-4).metres == 0);
}

TEST_CASE("notes: real distance in lazadas against the plan by hand") {
    rally::sim::StageAxis axis;
    const rally::sim::StageParseResult r =
        rally::sim::StageAxis::parse(rally::tests::lazadas_stage_text(), axis);
    INFO(r.error);
    REQUIRE(r.ok);
    const std::vector<DirectionChange> c = direction_changes(axis);
    REQUIRE(c.size() == 6);
    double metres = -1.0;
    // Del final de cada cambio al principio del siguiente, con la aritmética de [plan]:
    // 390−250, 520−520, 730−650, 70 de recta entre las dos horquillas, 50 de la salida.
    const double expected[5] = {140.0, 0.0, 80.0, 70.0, 50.0};
    for (std::size_t i = 0; i < 5; ++i) {
        INFO("cambio ", i + 1);
        REQUIRE(real_distance_after(c, c[i].s_start, metres));
        CHECK(std::abs(metres - expected[i]) <= 1e-6);
    }
    // El último cambio no tiene siguiente.
    CHECK_FALSE(real_distance_after(c, c[5].s_start, metres));
    // Un ancla en plena recta toma el cambio más cercano: a 50 m del 1 y a 90 m del 2.
    REQUIRE(real_distance_after(c, 300.0, metres));
    CHECK(std::abs(metres - 140.0) <= 1e-6);
    // Un ancla dentro de un cambio.
    REQUIRE(real_distance_after(c, 0.5 * (c[0].s_start + c[0].s_end), metres));
    CHECK(std::abs(metres - 140.0) <= 1e-6);
    // Sin cambios no hay distancia real.
    CHECK_FALSE(real_distance_after(std::vector<DirectionChange>{}, 10.0, metres));
}
