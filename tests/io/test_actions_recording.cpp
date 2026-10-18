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

#include <cstdint>
#include <cstdlib>
#include <optional>
#include <string>
#include <vector>

// Formato 7 del .rrec (lote 1): texto de acciones, ABS / control / suspensión en el mundo,
// eventos de acción y hash de su línea de tiempo en el pie.

using rally::game::ActionEvent;
using rally::game::EventKind;
using rally::game::Player;
using rally::io::Recorder;
using rally::io::Recording;
using rally::sim::SimInput;

namespace {

constexpr int kSessionSteps = 3600;
constexpr int kThrottleFrom = 1410;

// data/game/actions.txt tal cual (guardar_tablet = 0, sacar_tablet = 1) más una acción de prueba
// que pide el coche parado (prueba_parado = 2): el texto va en la cabecera de la grabación y es el
// que usa la repetición, no el del archivo de datos.
std::string actions_text(const char* car_requirement = "stopped") {
    std::string bytes;
    std::string error;
    REQUIRE_MESSAGE(rally::io::read_file(RALLY_GAME_DATA_DIR "/actions.txt", bytes, error), error);
    bytes += "\naction = prueba_parado\ncar = ";
    bytes += car_requirement;
    bytes += "\ntablet_requires = any\ntablet_result = unchanged\nbranch = driver\nhands = 1\n"
             "position = driver_seat\nprepare_ms = 100\nexecute_ms = 200\ncollect_ms = 100\n";
    return bytes;
}

ActionEvent request(std::uint64_t step, Player player, std::uint8_t action) {
    return ActionEvent{step, player, EventKind::Request, action};
}
ActionEvent cancel(std::uint64_t step, Player player, std::uint8_t action) {
    return ActionEvent{step, player, EventKind::Cancel, action};
}

// 10 peticiones y 2 cancelaciones: aceptadas y rechazadas, una cancelación al preparar y otra al
// ejecutar, un evento en el paso 0, otro tras el último paso y una acción que el coche, al
// arrancar, cancela solo.
std::vector<ActionEvent> session_events() {
    return {
        request(0, Player::Copilot, 0),            // guardar: aceptada
        cancel(20, Player::Copilot, 0),            // al preparar: sin coste
        request(100, Player::Copilot, 0),          // guardar: aceptada
        cancel(160, Player::Copilot, 0),           // al ejecutar: pierde lo avanzado
        request(200, Player::Copilot, 1),          // sacar con la tablet fuera: rechazada
        request(300, Player::Copilot, 0),          // guardar: aceptada
        request(400, Player::Copilot, 0),          // guardar otra vez: rechazada
        request(600, Player::Copilot, 1),          // sacar con la tablet guardada: aceptada
        request(1000, Player::Driver, 2),          // prueba_parado con el coche quieto: aceptada
        request(1400, Player::Driver, 2),          // aceptada; el coche arranca y la cancela
        request(2000, Player::Driver, 2),          // con el coche en marcha: rechazada
        request(kSessionSteps, Player::Copilot, 0) // tras el último paso
    };
}

rally::sim::WorldDesc session_world() {
    rally::sim::WorldDesc desc =
        rally::sim::zone_world(rally::sim::Surface::Gravel, rally::tests::reference_car());
    desc.aids.steer_assist = true;
    desc.aids.abs = true;
    desc.aids.traction = rally::sim::TractionControl::High;
    desc.aids.suspension = rally::sim::SuspensionSetting::Soft;
    return desc;
}

// Cuenta lo que ve la plataforma (sin reservar memoria).
class CountingObserver final : public rally::game::ActionObserver {
public:
    void on_transition(const rally::game::Transition& t) override {
        if (t.kind == rally::game::TransitionKind::Cancelled) {
            ++cancelled;
            if (t.reason == rally::game::CancelReason::CarMoving) {
                ++cancelled_by_car;
            }
        }
    }
    int cancelled = 0;
    int cancelled_by_car = 0;
};

struct Session {
    Recording recording;
    int rejected = 0;
    int cancelled = 0;
    int cancelled_by_car = 0;
};

// La sesión como la graba la partida: eventos al principio del paso, antes de simularlo, y la
// máquina avanza después (rally/src/game/actions.h).
Session record_session(const std::string& text, const rally::sim::WorldDesc& desc,
                       const std::vector<ActionEvent>& events, int total_steps) {
    SimInput input;
    rally::sim::SimWorld world;
    world.build(desc);
    world.seal();

    rally::game::ActionParseResult parsed;
    std::optional<rally::game::ActionMachine> machine;
    CountingObserver observer;
    if (!text.empty()) {
        parsed = rally::game::parse_actions(text);
        REQUIRE_MESSAGE(parsed.ok, parsed.message);
        rally::game::MachineSetup setup;
        setup.car = rally::game::car_state_from(world);
        machine.emplace(parsed.table, setup);
        machine->set_observer(&observer);
    }

    rally::io::RecordingHeader header;
    header.commit = "test";
    header.world = rally::io::serialize_world(desc);
    header.car_text = rally::tests::reference_car_text();
    header.zone_hash = world.zone_hash();
    header.actions_text = text;
    Recorder recorder;
    recorder.begin(std::move(header), 64);

    Session session;
    std::size_t next = 0;
    const auto apply_due = [&](std::uint64_t step) {
        while (next < events.size() && events[next].step == step) {
            recorder.ensure_capacity(1);
            recorder.append_event(events[next]);
            if (!machine->apply(events[next]).ok()) {
                ++session.rejected;
            }
            ++next;
        }
    };
    for (int step = 0; step < total_steps; ++step) {
        apply_due(static_cast<std::uint64_t>(step));
        if (step >= kThrottleFrom) {
            input.throttle = rally::sim::kAxisMax;
            input.steer = static_cast<std::int16_t>((step / 240 % 2 == 0) ? 3000 : -3000);
        }
        recorder.ensure_capacity(1);
        world.step(input);
        recorder.append(input);
        if (machine) {
            machine->advance(rally::game::car_state_from(world));
        }
    }
    apply_due(static_cast<std::uint64_t>(total_steps));
    session.cancelled = observer.cancelled;
    session.cancelled_by_car = observer.cancelled_by_car;
    session.recording =
        recorder.finish(world.state_hash(), rally::io::timed_steps(world),
                        rally::io::road_result(world), machine ? machine->timeline_hash() : 0);
    return session;
}

const Session& the_session() {
    static const Session session =
        record_session(actions_text(), session_world(), session_events(), kSessionSteps);
    return session;
}

Recording reparsed(const Recording& recording) {
    const rally::io::ParseResult parsed = rally::io::parse(rally::io::serialize(recording));
    REQUIRE_MESSAGE(parsed.ok, parsed.error);
    return parsed.recording;
}

} // namespace

TEST_CASE(
    "actions_recording: a session with 10 requests and 2 cancellations replays with hash OK") {
    const rally::sim::JoltRuntime runtime;
    const Session& session = the_session();
    CHECK(session.recording.header.format_version == 7);
    CHECK(session.recording.events.size() == 12);
    CHECK_FALSE(session.recording.header.actions_text.empty());
    // No es degenerada: hay rechazos, cancelaciones del jugador y una del coche al arrancar.
    CHECK(session.rejected == 3);
    CHECK(session.cancelled == 3);
    CHECK(session.cancelled_by_car == 1);

    const Recording back = reparsed(session.recording);
    CHECK(rally::io::serialize(back) == rally::io::serialize(session.recording));
    const rally::io::ReplayResult replayed = rally::io::replay(back);
    CHECK_MESSAGE(replayed.hash_ok, replayed.problem);
    CHECK(replayed.actions_ran);
    CHECK(replayed.actions_hash == session.recording.footer.actions_hash);
    CHECK(replayed.hash == session.recording.footer.final_hash);

    // La grabación de tests/data/recordings se genera una vez con RALLY_WRITE_ACTIONS_REC=<ruta>.
    char* path = nullptr;
    std::size_t path_size = 0;
    if (_dupenv_s(&path, &path_size, "RALLY_WRITE_ACTIONS_REC") == 0 && path != nullptr) {
        std::string error;
        CHECK_MESSAGE(rally::io::write_file(path, rally::io::serialize(session.recording), error),
                      error);
    }
    std::free(path);
}

TEST_CASE("actions_recording: the stored format 7 recording replays with hash OK") {
    const rally::sim::JoltRuntime runtime;
    std::string bytes;
    std::string error;
    REQUIRE_MESSAGE(
        rally::io::read_file(RALLY_TEST_DATA_DIR "/recordings/actions_v7.rrec", bytes, error),
        error);
    const rally::io::ParseResult parsed = rally::io::parse(bytes);
    REQUIRE_MESSAGE(parsed.ok, parsed.error);
    const Recording& recording = parsed.recording;
    CHECK(recording.header.format_version == 7);
    CHECK(recording.header.sim_version == rally::sim::kSimVersion);
    CHECK(recording.events.size() == 12);
    CHECK_FALSE(recording.header.actions_text.empty());
    rally::sim::WorldDesc desc;
    REQUIRE(rally::io::parse_world(recording.header.world, desc, error));
    CHECK(desc.aids.abs);
    CHECK(desc.aids.traction == rally::sim::TractionControl::High);
    CHECK(desc.aids.suspension == rally::sim::SuspensionSetting::Soft);
    const rally::io::ReplayResult replayed = rally::io::replay(recording);
    CHECK_MESSAGE(replayed.hash_ok, replayed.problem);
    CHECK(replayed.actions_ran);
}

TEST_CASE(
    "actions_recording: changing an event, the actions hash or the header text breaks hash OK") {
    const rally::sim::JoltRuntime runtime;
    const Recording& original = the_session().recording;
    const auto check_fails = [](const Recording& recording, const char* what) {
        const rally::io::ReplayResult result = rally::io::replay(recording);
        CAPTURE(what);
        CHECK(result.ran);
        CHECK_FALSE(result.hash_ok);
        CHECK(result.problem.find("acciones") != std::string::npos);
    };
    {
        Recording moved = original;
        moved.events[5].step += 1; // guardar en 301
        check_fails(moved, "paso de un evento");
    }
    {
        Recording other = original;
        other.events[8].player = Player::Copilot; // prueba_parado pedida por quien no puede
        check_fails(other, "jugador de un evento");
    }
    {
        Recording other = original;
        other.footer.actions_hash ^= 1;
        check_fails(other, "hash del pie");
    }
    {
        // La repetición usa el texto de la cabecera: con la acción de prueba sin pedir el coche
        // parado, la petición con el coche en marcha se acepta y la línea de tiempo cambia.
        Recording other = original;
        other.header.actions_text = actions_text("any");
        check_fails(other, "texto de la cabecera");
    }
    {
        // Sin cambiar nada, cuadra.
        CHECK(rally::io::replay(original).hash_ok);
    }
}

TEST_CASE(
    "actions_recording: ABS, traction control and suspension travel in the world description") {
    const rally::sim::JoltRuntime runtime;
    Recording other = the_session().recording;
    rally::sim::WorldDesc desc;
    std::string error;
    REQUIRE(rally::io::parse_world(other.header.world, desc, error));
    CHECK(desc.aids.abs);
    CHECK(desc.aids.traction == rally::sim::TractionControl::High);
    CHECK(desc.aids.suspension == rally::sim::SuspensionSetting::Soft);
    desc.aids.abs = false;
    desc.aids.traction = rally::sim::TractionControl::Off;
    desc.aids.suspension = rally::sim::SuspensionSetting::Medium;
    other.header.world = rally::io::serialize_world(desc);
    const rally::io::ReplayResult result = rally::io::replay(other);
    CHECK_FALSE(result.hash_ok);
    CHECK(result.hash != the_session().recording.footer.final_hash);
    CHECK(result.problem.find("hash final distinto") != std::string::npos);
}

TEST_CASE("actions_recording: a recording without actions does not compare the actions hash") {
    const rally::sim::JoltRuntime runtime;
    const Session session = record_session(std::string{}, session_world(), {}, 600);
    Recording recording = session.recording;
    // Sin texto, sin eventos y pie a 0.
    CHECK(recording.header.actions_text.empty());
    CHECK(recording.events.empty());
    CHECK(recording.footer.actions_hash == 0);
    CHECK(rally::io::replay(recording).hash_ok);
    CHECK_FALSE(rally::io::replay(recording).actions_ran);
    recording.footer.actions_hash = 0x77; // sin acciones, no se compara
    CHECK(rally::io::replay(reparsed(recording)).hash_ok);
}

TEST_CASE(
    "actions_recording: the replay builds the machine with the car as built (launched start)") {
    const rally::sim::JoltRuntime runtime;
    // El coche sale a 20 m/s: una acción que pide el coche parado, pedida en el paso 0, se
    // rechaza. Con MachineSetup{} a secas (coche parado) se aceptaría.
    const std::vector<ActionEvent> events = {request(0, Player::Driver, 2)};
    const rally::sim::WorldDesc desc =
        rally::tests::flat_world(rally::sim::Surface::Asphalt, false, 20.0f);
    const Session session = record_session(actions_text(), desc, events, 20);
    CHECK(session.rejected == 1);
    const rally::io::ReplayResult replayed = rally::io::replay(reparsed(session.recording));
    CHECK_MESSAGE(replayed.hash_ok, replayed.problem);

    const rally::game::ActionParseResult parsed = rally::game::parse_actions(actions_text());
    REQUIRE_MESSAGE(parsed.ok, parsed.message);
    rally::game::ActionMachine default_setup(parsed.table);
    CHECK(default_setup.apply(events[0]).ok());
    CHECK(default_setup.timeline_hash() != session.recording.footer.actions_hash);
}
