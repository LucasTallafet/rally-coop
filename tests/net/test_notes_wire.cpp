// Mensajes de notas del protocolo 4: ida y vuelta y rechazo de lo mal formado. Sin ENet.

#include "game/notes.h"
#include "net/notes_wire.h"
#include "net/protocol.h"
#include "net/wire.h"
#include "sim/pcg32.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace {

using namespace rally::net;
using namespace rally::game;

std::span<const std::uint8_t> bytes_of(const std::string& s) {
    return {reinterpret_cast<const std::uint8_t*>(s.data()), s.size()};
}

NoteOrder random_order(rally::sim::Pcg32& rng) {
    const std::uint32_t pick = rng.next_u32() % 100;
    const double s = static_cast<double>(rng.next_u32() % 3000000) / 1000.0;
    if (pick < 50) {
        return corner_order(s, static_cast<NoteGrade>(1 + rng.next_u32() % 7),
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

// La orden tal como llega: el ancla pasa por milímetros.
NoteOrder arrived(NoteOrder o) {
    if (o.kind == NoteOrderKind::Corner || o.kind == NoteOrderKind::Hazard) {
        o.s = from_mm(to_mm(o.s));
    }
    return o;
}

NoteQueue random_queue(rally::sim::Pcg32& rng, int orders) {
    NoteQueue q;
    q.reset(3000.0);
    for (int i = 0; i < orders; ++i) {
        q.apply(random_order(rng), 1500.0);
    }
    return q;
}

} // namespace

TEST_CASE("net_notes_wire: every kind of order makes the round trip") {
    rally::sim::Pcg32 rng(3, 4);
    const NoteDistance far = counter_distance(990);
    const NoteOrder orders[] = {
        corner_order(123.4567, NoteGrade::Hairpin, NoteSide::Right),
        corner_order(0.0, NoteGrade::G1, NoteSide::Left),
        corner_order(-3.2, NoteGrade::G6, NoteSide::Left),
        hazard_order(77.7, NoteMark::Crest),
        hazard_order(88.0, NoteMark::Jump),
        hazard_order(99.0, NoteMark::Caution),
        mark_order(NoteMark::Tightens),
        mark_order(NoteMark::Caution),
        distance_order(far),
        distance_order(wheel_distance(0)),
        distance_order(wheel_distance(7)),
        distance_order(counter_distance(0)),
        erase_last_order(),
        clear_pending_order(),
    };
    for (const NoteOrder& o : orders) {
        const std::string msg = encode_note_order(o, 7);
        CHECK(msg.size() == kNoteOrderBytes);
        NoteOrder back;
        std::uint8_t generation = 0;
        REQUIRE(decode_note_order(bytes_of(msg), back, generation));
        CHECK(generation == 7);
        CHECK(back == arrived(o));
    }
    // 200 órdenes al azar.
    for (int i = 0; i < 200; ++i) {
        const NoteOrder o = random_order(rng);
        NoteOrder back;
        std::uint8_t generation = 0;
        const std::string msg = encode_note_order(o, 255);
        REQUIRE(decode_note_order(bytes_of(msg), back, generation));
        CHECK(back == arrived(o));
        CHECK(generation == 255);
    }
}

TEST_CASE("net_notes_wire: the state of the notes makes the round trip with 0, 1 and 256 notes") {
    rally::sim::Pcg32 rng(9, 9);
    for (const int count : {0, 1, 40, kMaxNotes}) {
        NoteQueue q;
        q.reset(3000.0);
        for (int i = 0; i < count; ++i) {
            q.apply(corner_order(static_cast<double>(rng.next_u32() % 3000000) / 1000.0,
                                 static_cast<NoteGrade>(1 + rng.next_u32() % 7), NoteSide::Left),
                    0.0);
        }
        if (count > 0) {
            q.apply(mark_order(NoteMark::Long), 0.0);
            q.apply(distance_order(counter_distance(120)), 0.0);
        }
        const NotesState state = notes_state_from(q, 3);
        CHECK(state.count == count);
        const std::string msg = encode_notes_state(state);
        CHECK(msg.size() == 25 + 4 * static_cast<std::size_t>(count));
        NotesState back;
        REQUIRE(decode_notes_state(bytes_of(msg), back));
        CHECK(back == state);
        CHECK(back.has_last == (count > 0));
        if (count > 0) {
            CHECK(back.last.marks == mark_bit(NoteMark::Long));
            CHECK(back.last.distance.metres == 120);
            CHECK(back.last.distance.source == DistanceSource::Counter);
        }
        CHECK(back.revision == q.revision());
    }
    // 1049 B con 256 notas.
    NoteQueue q;
    q.reset(3000.0);
    for (int i = 0; i < kMaxNotes; ++i) {
        q.apply(corner_order(i, NoteGrade::G3, NoteSide::Left), 0.0);
    }
    CHECK(encode_notes_state(notes_state_from(q, 0)).size() == 1049);
}

TEST_CASE("net_notes_wire: pending notes and the anchors the host has") {
    NoteQueue q;
    q.reset(2000.0);
    q.apply(corner_order(100.0, NoteGrade::G3, NoteSide::Left), 0.0);
    q.apply(corner_order(390.0, NoteGrade::G2, NoteSide::Right), 0.0);
    q.apply(corner_order(520.0, NoteGrade::G4, NoteSide::Left), 0.0);
    const NotesState s = notes_state_from(q, 0);
    CHECK(notes_ahead(s, -5.0) == 3);
    CHECK(notes_ahead(s, 100.0) == 3);
    CHECK(notes_ahead(s, 101.0) == 2);
    CHECK(notes_ahead(s, 600.0) == 0);
    CHECK(notes_has_anchor_near(s, 390.0, kNoteAnchorStepM));
    CHECK(notes_has_anchor_near(s, 390.0004, kNoteAnchorStepM));
    CHECK_FALSE(notes_has_anchor_near(s, 391.0, kNoteAnchorStepM));
}

TEST_CASE("net_notes_wire: malformed orders are rejected") {
    const std::string good =
        encode_note_order(corner_order(10.0, NoteGrade::G3, NoteSide::Left), 1);
    NoteOrder o;
    std::uint8_t g = 0;
    REQUIRE(decode_note_order(bytes_of(good), o, g));
    // Todos los tamaños de 0 a 40 B menos el bueno, cortando y alargando.
    for (std::size_t n = 0; n <= 40; ++n) {
        if (n == kNoteOrderBytes) {
            continue;
        }
        std::string msg = good;
        msg.resize(n, '\0');
        CHECK_FALSE(decode_note_order(bytes_of(msg), o, g));
    }
    auto with = [&](std::size_t index, std::uint8_t value) {
        std::string msg = good;
        msg[index] = static_cast<char>(value);
        return msg;
    };
    // Layout: 0 tipo, 1 generación, 2 clase, 3..6 s, 7 grado, 8 lado, 9 marca, 10 clase de
    // distancia, 11..12 metros, 13 forma.
    CHECK_FALSE(decode_note_order(bytes_of(with(0, 99)), o, g));             // tipo desconocido
    CHECK_FALSE(decode_note_order(bytes_of(with(0, kMsgNotesState)), o, g)); // tipo de otro mensaje
    CHECK_FALSE(decode_note_order(bytes_of(with(2, 0)), o, g));              // clase 0
    CHECK_FALSE(decode_note_order(bytes_of(with(2, 7)), o, g));              // clase 7
    CHECK_FALSE(decode_note_order(bytes_of(with(7, 8)), o, g));              // grado fuera de rango
    CHECK_FALSE(decode_note_order(bytes_of(with(7, 0)), o, g));              // curva sin grado
    CHECK_FALSE(decode_note_order(bytes_of(with(8, 3)), o, g));              // lado fuera de rango
    CHECK_FALSE(decode_note_order(bytes_of(with(8, 0)), o, g));              // curva sin lado
    CHECK_FALSE(decode_note_order(bytes_of(with(9, 7)), o, g));              // marca fuera de rango
    CHECK_FALSE(decode_note_order(bytes_of(with(9, 1)), o, g));              // campo sin usar
    CHECK_FALSE(decode_note_order(bytes_of(with(10, 1)), o, g));             // distancia sin usar
    CHECK_FALSE(decode_note_order(bytes_of(with(13, 1)), o, g));             // forma sin usar
    // Metros fuera de rango y formas incoherentes en una orden de distancia.
    NoteDistance bad = counter_distance(10);
    std::string d = encode_note_order(distance_order(bad), 1);
    d[11] = static_cast<char>(0xDF); // 991 = 0x03DF
    d[12] = 0x03;
    CHECK_FALSE(decode_note_order(bytes_of(d), o, g));
    d = encode_note_order(distance_order(wheel_distance(3)), 1);
    d[13] = 0; // metros con forma None
    CHECK_FALSE(decode_note_order(bytes_of(d), o, g));
    d = encode_note_order(distance_order(wheel_distance(0)), 1);
    d[11] = 5; // «y» con metros
    CHECK_FALSE(decode_note_order(bytes_of(d), o, g));
    // Una orden de borrar con un campo de más.
    std::string e = encode_note_order(erase_last_order(), 1);
    REQUIRE(decode_note_order(bytes_of(e), o, g));
    e[4] = 1;
    CHECK_FALSE(decode_note_order(bytes_of(e), o, g));
    // Un peligro con una marca que no es un peligro.
    std::string h = encode_note_order(hazard_order(5.0, NoteMark::Crest), 1);
    h[9] = static_cast<char>(NoteMark::Long);
    CHECK_FALSE(decode_note_order(bytes_of(h), o, g));
}

TEST_CASE("net_notes_wire: malformed states are rejected") {
    rally::sim::Pcg32 rng(2, 2);
    const NoteQueue q = random_queue(rng, 30);
    REQUIRE(q.size() > 3);
    const std::string good = encode_notes_state(notes_state_from(q, 1));
    NotesState s;
    REQUIRE(decode_notes_state(bytes_of(good), s));
    for (std::size_t n = 0; n < good.size(); ++n) { // cortado en cualquier punto
        CHECK_FALSE(decode_notes_state(bytes_of(good.substr(0, n)), s));
    }
    CHECK_FALSE(decode_notes_state(bytes_of(good + std::string(1, '\0')), s));
    auto with = [&](std::size_t index, std::uint8_t value) {
        std::string msg = good;
        msg[index] = static_cast<char>(value);
        return msg;
    };
    // Layout: 0 tipo, 1 generación, 2..5 revisión, 6 resultado, 7 hay última, 8..22 nota (id, s,
    // grado 16, lado 17, marcas 18, distancia 19..21, forma 22), 23..24 n.
    CHECK_FALSE(decode_notes_state(bytes_of(with(0, kMsgNoteOrder)), s));
    CHECK_FALSE(decode_notes_state(bytes_of(with(6, 4)), s));  // resultado fuera de rango
    CHECK_FALSE(decode_notes_state(bytes_of(with(7, 2)), s));  // «hay última» no es 0 ni 1
    CHECK_FALSE(decode_notes_state(bytes_of(with(7, 0)), s));  // sin última pero con notas
    CHECK_FALSE(decode_notes_state(bytes_of(with(16, 9)), s)); // grado de la última fuera de rango
    CHECK_FALSE(decode_notes_state(bytes_of(with(17, 3)), s)); // lado fuera de rango
    CHECK_FALSE(decode_notes_state(bytes_of(with(18, 0x80)), s)); // marca reservada
    CHECK_FALSE(decode_notes_state(bytes_of(with(23, 5)), s));    // n que no cuadra con el tamaño
    std::string many = good;
    many[24] = 2; // n = 0x0200 = 512 > 256
    many.resize(25 + 4 * 512, '\0');
    CHECK_FALSE(decode_notes_state(bytes_of(many), s));
}

TEST_CASE("net_notes_wire: 10000 random messages are rejected or come back identical") {
    rally::sim::Pcg32 rng(2026, 11);
    rally::sim::Pcg32 queue_rng(1, 1);
    const std::string order = encode_note_order(random_order(queue_rng), 5);
    const std::string state = encode_notes_state(notes_state_from(random_queue(queue_rng, 20), 5));
    int accepted_orders = 0;
    int accepted_states = 0;
    for (int i = 0; i < 10000; ++i) {
        std::string msg;
        if (i % 2 == 0) { // bytes al azar, con el tipo bueno la mitad de las veces
            msg.resize(rng.next_u32() % 1101);
            for (char& c : msg) {
                c = static_cast<char>(rng.next_u32() & 0xFF);
            }
            if (!msg.empty() && rng.next_u32() % 2 == 0) {
                msg[0] =
                    static_cast<char>(rng.next_u32() % 2 == 0 ? kMsgNoteOrder : kMsgNotesState);
            }
        } else { // un mensaje bueno con un byte cambiado
            msg = rng.next_u32() % 2 == 0 ? order : state;
            msg[rng.next_u32() % msg.size()] = static_cast<char>(rng.next_u32() & 0xFF);
        }
        NoteOrder o;
        std::uint8_t g = 0;
        if (decode_note_order(bytes_of(msg), o, g)) {
            ++accepted_orders;
            CHECK(encode_note_order(o, g) == msg);
        }
        NotesState s;
        if (decode_notes_state(bytes_of(msg), s)) {
            ++accepted_states;
            CHECK(encode_notes_state(s) == msg);
        }
    }
    MESSAGE("aceptados: ", accepted_orders, " órdenes y ", accepted_states, " estados");
}
