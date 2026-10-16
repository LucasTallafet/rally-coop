#include "car_worlds.h"
#include "game/actions.h"
#include "io/recording.h"
#include "io/replay.h"
#include "new_counter.h"
#include "platform/fixed_step_clock.h"
#include "sim/demo_world.h"
#include "sim/jolt_runtime.h"
#include "sim/sim_input.h"
#include "sim/sim_version.h"
#include "sim/sim_world.h"

#include <doctest/doctest.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

// Formato 7 del .rrec: disposición, lectura segura, compatibilidad con los formatos 1–6 y
// reservas. Las pruebas con la simulación y las acciones están en test_actions_recording.cpp.

using rally::game::ActionEvent;
using rally::game::EventKind;
using rally::game::Player;
using rally::io::Recording;

namespace {

std::string actions_file() {
    std::string bytes;
    std::string error;
    REQUIRE_MESSAGE(rally::io::read_file(RALLY_GAME_DATA_DIR "/actions.txt", bytes, error), error);
    return bytes;
}

// Una grabación de formato 7 construida a mano, sin simulación: para probar el formato.
Recording handmade() {
    Recording r;
    r.header.commit = "test";
    r.header.world =
        rally::io::serialize_world(rally::sim::demo_arena(0.0, rally::tests::reference_car()));
    r.header.car_text = rally::tests::reference_car_text();
    r.header.settings = "x = 1\n";
    r.header.actions_text = "texto de las acciones";
    r.runs = {rally::io::InputRun{100, rally::sim::SimInput{}},
              rally::io::InputRun{50, rally::sim::SimInput{1000, 2000, 0, 1}}};
    r.events = {ActionEvent{0, Player::Copilot, EventKind::Request, 0},
                ActionEvent{7, Player::Driver, EventKind::Request, 1},
                ActionEvent{7, Player::Copilot, EventKind::Cancel, 0},
                ActionEvent{150, Player::Copilot, EventKind::Request, 1}};
    r.footer.complete = true;
    r.footer.total_steps = 150;
    r.footer.final_hash = 0x1111;
    r.footer.timed_steps = -1;
    r.footer.actions_hash = 0x2222;
    return r;
}

} // namespace

TEST_CASE("format7: the file is header plus runs plus events plus footer, and round-trips") {
    const Recording r = handmade();
    const std::string bytes = rally::io::serialize(r);
    const rally::io::RecordingHeader& h = r.header;
    // Firma, dos versiones, cuatro cadenas, hash de la zona, texto de acciones, n.º de tramos.
    const std::size_t header = 8 + 4 + 4 + (4 + h.commit.size()) + (4 + h.world.size()) +
                               (4 + h.car_text.size()) + (4 + h.settings.size()) + 8 +
                               (4 + h.actions_text.size()) + 4;
    CHECK(rally::io::header_bytes(h) == header);
    CHECK(rally::io::kEventBytes == 11);
    CHECK(rally::io::kFooterBytes == 56);
    CHECK(rally::io::footer_bytes(7) == 56);
    CHECK(rally::io::footer_bytes(6) == 48);
    CHECK(rally::io::footer_bytes(5) == 32);
    CHECK(bytes.size() ==
          header + 12 * r.runs.size() + 4 + 11 * r.events.size() + rally::io::kFooterBytes);

    const rally::io::ParseResult parsed = rally::io::parse(bytes);
    REQUIRE_MESSAGE(parsed.ok, parsed.error);
    CHECK(parsed.recording.events == r.events);
    CHECK(parsed.recording.header.actions_text == h.actions_text);
    CHECK(parsed.recording.footer.actions_hash == 0x2222);
    CHECK(rally::io::serialize(parsed.recording) == bytes);
}

TEST_CASE("format7: a truncated file is an error at every byte, never an overread") {
    const std::string bytes = rally::io::serialize(handmade());
    REQUIRE(rally::io::parse(bytes).ok);
    for (std::size_t size = 0; size < bytes.size(); ++size) {
        // Búfer exacto: una lectura fuera de rango la detecta la CRT en Debug.
        const std::vector<char> cut(bytes.begin(), bytes.begin() + static_cast<long long>(size));
        CHECK_FALSE(rally::io::parse(std::string_view(cut.data(), cut.size())).ok);
    }
    CHECK_FALSE(rally::io::parse(bytes + "x").ok);
}

TEST_CASE("format7: bad events are errors") {
    const Recording good = handmade();
    const std::size_t events_at = rally::io::header_bytes(good.header) + 12 * good.runs.size();
    const std::string bytes = rally::io::serialize(good);

    // Número de eventos enorme: error sin reservar lo que dice.
    std::string huge = bytes;
    huge[events_at + 3] = '\x7f';
    CHECK_FALSE(rally::io::parse(huge).ok);
    // Jugador o tipo fuera de rango (primer evento: u64 paso, u8 jugador, u8 tipo, u8 acción).
    std::string bad_player = bytes;
    bad_player[events_at + 4 + 8] = 2;
    CHECK_FALSE(rally::io::parse(bad_player).ok);
    std::string bad_kind = bytes;
    bad_kind[events_at + 4 + 9] = 2;
    CHECK_FALSE(rally::io::parse(bad_kind).ok);
    // Pasos que retroceden y paso posterior al último.
    Recording backwards = good;
    backwards.events[3].step = 3;
    CHECK_FALSE(rally::io::parse(rally::io::serialize(backwards)).ok);
    Recording late = good;
    late.events[3].step = 151;
    CHECK_FALSE(rally::io::parse(rally::io::serialize(late)).ok);
    // Eventos sin texto de acciones.
    Recording no_text = good;
    no_text.header.actions_text.clear();
    CHECK_FALSE(rally::io::parse(rally::io::serialize(no_text)).ok);
    // Sin eventos y sin texto: válida.
    Recording none = good;
    none.header.actions_text.clear();
    none.events.clear();
    none.footer.actions_hash = 0;
    CHECK(rally::io::parse(rally::io::serialize(none)).ok);
}

TEST_CASE("format7: an in-memory recording with bad events is a replay problem, not an abort") {
    const rally::sim::JoltRuntime runtime;
    Recording r = handmade();
    r.header.actions_text = actions_file();
    r.header.world =
        rally::io::serialize_world(rally::sim::demo_arena(0.0, rally::tests::reference_car()));
    r.events = {ActionEvent{0, Player::Copilot, EventKind::Request, 0}};
    r.footer.final_hash = 0;
    {
        const rally::io::ReplayResult ok = rally::io::replay(r);
        CHECK(ok.ran);
        CHECK(ok.actions_ran);
    }
    {
        Recording x = r;
        x.events = {ActionEvent{10, Player::Copilot, EventKind::Request, 0},
                    ActionEvent{5, Player::Copilot, EventKind::Cancel, 0}};
        const rally::io::ReplayResult result = rally::io::replay(x);
        CHECK_FALSE(result.ran);
        CHECK_FALSE(result.problem.empty());
    }
    {
        Recording x = r;
        x.events = {ActionEvent{151, Player::Copilot, EventKind::Request, 0}};
        CHECK_FALSE(rally::io::replay(x).ran);
    }
    {
        Recording x = r;
        x.events = {ActionEvent{3, static_cast<Player>(2), EventKind::Request, 0}};
        CHECK_FALSE(rally::io::replay(x).ran);
    }
    {
        Recording x = r;
        x.header.actions_text.clear();
        CHECK_FALSE(rally::io::replay(x).ran);
    }
    {
        Recording x = r;
        x.header.actions_text = "clave = rara\n";
        const rally::io::ReplayResult result = rally::io::replay(x);
        CHECK_FALSE(result.ran);
        CHECK(result.problem.find("línea 1") != std::string::npos);
    }
    {
        // Una acción que no existe la rechaza la máquina (determinista), no la repetición.
        Recording x = r;
        x.events = {ActionEvent{0, Player::Copilot, EventKind::Request, 9}};
        CHECK(rally::io::replay(x).ran);
    }
}

TEST_CASE("format7: the world description carries ABS, traction control and suspension") {
    rally::sim::WorldDesc desc = rally::sim::demo_arena(0.0, rally::tests::reference_car());
    desc.aids.abs = true;
    desc.aids.traction = rally::sim::TractionControl::Low;
    desc.aids.suspension = rally::sim::SuspensionSetting::Hard;
    const std::string bytes = rally::io::serialize_world(desc);
    rally::sim::WorldDesc back;
    std::string error;
    REQUIRE_MESSAGE(rally::io::parse_world(bytes, back, error), error);
    CHECK(back.aids.abs);
    CHECK(back.aids.traction == rally::sim::TractionControl::Low);
    CHECK(back.aids.suspension == rally::sim::SuspensionSetting::Hard);
    // Rangos: ABS 0/1, control 0..2, suspensión 0..2.
    for (int field = 0; field < 3; ++field) {
        std::string bad = bytes;
        bad[bad.size() - 3 + static_cast<std::size_t>(field)] = field == 0 ? 2 : 3;
        CHECK_FALSE(rally::io::parse_world(bad, back, error));
    }
}

TEST_CASE("format7: worlds of formats 3 to 6 read with ABS off, no traction control and medium "
          "suspension") {
    rally::sim::WorldDesc desc = rally::sim::demo_arena(0.0, rally::tests::reference_car());
    desc.test_zone = true;
    desc.road_surface = rally::sim::Surface::Asphalt;
    desc.aids.steer_assist = true;
    desc.aids.gearbox = rally::sim::GearboxMode::Sequential;
    desc.aids.abs = true;
    desc.aids.traction = rally::sim::TractionControl::High;
    desc.aids.suspension = rally::sim::SuspensionSetting::Soft;
    const std::string v7 = rally::io::serialize_world(desc);
    // Cada formato anterior quita del final: 3 (la 7), 1 (calzada, la 6), 2 (caja y marcha, la
    // 5) y 1 (ayuda de dirección, la 4).
    const std::size_t cut[] = {0, 3, 4, 6, 7};
    const std::uint32_t formats[] = {7, 6, 5, 4, 3};
    for (int i = 0; i < 5; ++i) {
        INFO("formato ", formats[i]);
        const std::string bytes = v7.substr(0, v7.size() - cut[i]);
        rally::sim::WorldDesc back;
        std::string error;
        REQUIRE_MESSAGE(rally::io::parse_world(bytes, back, error, formats[i]), error);
        CHECK(back.aids.abs == (formats[i] >= 7));
        CHECK(back.aids.traction == (formats[i] >= 7 ? rally::sim::TractionControl::High
                                                     : rally::sim::TractionControl::Off));
        CHECK(back.aids.suspension == (formats[i] >= 7 ? rally::sim::SuspensionSetting::Soft
                                                       : rally::sim::SuspensionSetting::Medium));
        CHECK(back.aids.steer_assist == (formats[i] >= 4));
        // Los bytes de un formato no valen para otro.
        if (formats[i] < 7) {
            CHECK_FALSE(rally::io::parse_world(bytes, back, error, 7));
        }
    }
}

TEST_CASE("format7: the stored recordings of formats 1 to 6 still read and replay as before") {
    const rally::sim::JoltRuntime runtime;
    const char* const names[] = {"bench_lap", "road_off", "road_run", "zone_lap",
                                 "zone_lap_other_zone"};
    for (const char* name : names) {
        INFO(name);
        std::string bytes;
        std::string error;
        REQUIRE_MESSAGE(
            rally::io::read_file(std::string(RALLY_TEST_DATA_DIR) + "/recordings/" + name + ".rrec",
                                 bytes, error),
            error);
        const rally::io::ParseResult parsed = rally::io::parse(bytes);
        REQUIRE_MESSAGE(parsed.ok, parsed.error);
        const Recording& r = parsed.recording;
        CHECK(r.header.format_version == 6);
        CHECK(r.header.actions_text.empty());
        CHECK(r.events.empty());
        CHECK(r.footer.actions_hash == 0);
        // Vuelve a serializarse igual (formato 6 con su pie de 48 bytes).
        CHECK(rally::io::serialize(r) == bytes);
        const rally::io::ReplayResult replayed = rally::io::replay(r);
        CHECK(replayed.ran);
        CHECK_FALSE(replayed.actions_ran);
        if (std::string(name) == "zone_lap_other_zone") {
            CHECK(replayed.zone_mismatch);
            CHECK_FALSE(replayed.hash_ok);
        } else {
            CHECK_MESSAGE(replayed.hash_ok, replayed.problem);
        }
        if (std::string(name) == "zone_lap") {
            // rewrite: pasa al formato 7 y sigue dando hash OK.
            Recording rewritten = r;
            const rally::io::ReplayResult result = rally::io::rewrite_footer(rewritten);
            CHECK(rewritten.header.format_version == 7);
            CHECK_MESSAGE(result.hash_ok, result.problem);
            CHECK(rally::io::parse(rally::io::serialize(rewritten)).ok);
        }
    }
}

TEST_CASE("format7: requests at the same steps at 30 and at 60 fps give the same timeline") {
    // Las peticiones llegan por fotograma: se piden al principio del fotograma en que el reloj
    // ya ha dado el paso objetivo, y el paso en que se aplican es el siguiente a simular.
    const rally::game::ActionParseResult parsed = rally::game::parse_actions(actions_file());
    REQUIRE_MESSAGE(parsed.ok, parsed.message);
    const std::uint64_t targets[] = {240, 480, 1200, 1536}; // múltiplos de 8: un fotograma a 30 fps
    const std::uint8_t actions[] = {0, 1, 0, 1};
    struct Outcome {
        std::vector<ActionEvent> events;
        std::uint64_t hash = 0;
        std::uint64_t steps = 0;
    };
    Outcome outcomes[2];
    const std::int64_t frame_ns[] = {16'666'667, 33'333'334};
    for (int run = 0; run < 2; ++run) {
        rally::platform::FixedStepClock clock;
        rally::game::ActionMachine machine(parsed.table);
        rally::io::Recorder recorder;
        rally::io::RecordingHeader header;
        header.commit = "test";
        header.actions_text = "x";
        recorder.begin(std::move(header), 16);
        std::size_t next = 0;
        while (recorder.steps() < 2400) {
            const rally::platform::FrameSteps steps = clock.advance(frame_ns[run]);
            recorder.ensure_capacity(steps.count);
            if (next < 4 && recorder.steps() >= targets[next]) {
                const ActionEvent event{recorder.steps(), Player::Copilot, EventKind::Request,
                                        actions[next]};
                recorder.append_event(event);
                machine.apply(event);
                ++next;
            }
            for (int i = 0; i < steps.count; ++i) {
                recorder.append(rally::sim::SimInput{});
                machine.advance(rally::game::CarState{});
            }
        }
        const std::uint64_t hash = machine.timeline_hash();
        outcomes[run].steps = recorder.steps();
        outcomes[run].events = recorder.finish(0, -1, {}, hash).events;
        outcomes[run].hash = hash;
    }
    REQUIRE(outcomes[0].events.size() == 4);
    CHECK(outcomes[0].events == outcomes[1].events);
    CHECK(outcomes[0].hash == outcomes[1].hash);
    for (std::size_t i = 0; i < 4; ++i) {
        CHECK(outcomes[0].events[i].step == targets[i]);
    }
}

TEST_CASE("format7: appending steps and events allocates nothing once capacity is ensured") {
    rally::io::Recorder recorder;
    rally::io::RecordingHeader header;
    header.actions_text = "x";
    recorder.begin(std::move(header), 4);
    int allocations = 0;
    std::uint32_t seed = 1;
    for (int frame = 0; frame < 3000; ++frame) {
        recorder.ensure_capacity(8);
        const std::uint64_t before = rally::tests::global_new_count();
        // Hasta 3 eventos al principio del fotograma y 8 pasos con entradas distintas.
        for (int e = 0; e < (frame % 4); ++e) {
            recorder.append_event(
                ActionEvent{recorder.steps(), Player::Copilot, EventKind::Request, 0});
        }
        for (int i = 0; i < 8; ++i) {
            seed = seed * 1664525u + 1013904223u;
            rally::sim::SimInput input;
            input.steer = static_cast<std::int16_t>(seed >> 20);
            recorder.append(input);
        }
        allocations += static_cast<int>(rally::tests::global_new_count() - before);
    }
    CHECK(allocations == 0);
    CHECK(recorder.steps() == 24000);
    CHECK(recorder.recording().events.size() == 3000 / 4 * 6);
}
