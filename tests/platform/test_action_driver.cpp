#include "car_worlds.h"
#include "game/actions.h"
#include "io/recording.h"
#include "io/replay.h"
#include "new_counter.h"
#include "platform/action_driver.h"
#include "sim/demo_world.h"
#include "sim/jolt_runtime.h"
#include "sim/sim_input.h"
#include "sim/sim_world.h"

#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <string>
#include <vector>

// ActionDriver (tarea 2.9, parte P): el pegamento entre la máquina, el grabador y las teclas.
// Una sesión hecha como la hace la plataforma tiene que repetirse con «hash OK» y con la misma
// línea de tiempo.

using rally::game::ApplyResult;
using rally::game::EventKind;
using rally::game::Player;
using rally::game::Reject;
using rally::io::Recorder;
using rally::platform::ActionDriver;

namespace {

// guardar_tablet = 0 y sacar_tablet = 1, como data/game/actions.txt (200 / 400 / 200 ms), y una
// acción de prueba que pide el coche parado, prueba_parado = 2 (el archivo de datos no tiene
// ninguna: este texto es del test).
const std::string kText = R"(action = guardar_tablet
car = any
tablet_requires = out
tablet_result = stowed
branch = copilot
hands = 2
position = copilot_seat
prepare_ms = 200
execute_ms = 400
collect_ms = 200

action = sacar_tablet
car = any
tablet_requires = stowed
tablet_result = out
branch = copilot
hands = 2
position = copilot_seat
prepare_ms = 200
execute_ms = 400
collect_ms = 200

action = prueba_parado
car = stopped
tablet_requires = any
tablet_result = unchanged
branch = driver
hands = 1
position = driver_seat
prepare_ms = 100
execute_ms = 200
collect_ms = 100
)";
constexpr std::uint8_t kStow = 0;
constexpr std::uint8_t kTake = 1;
constexpr std::uint8_t kStopped = 2;

rally::sim::WorldDesc session_world() {
    return rally::sim::zone_world(rally::sim::Surface::Gravel, rally::tests::reference_car());
}

// Lo que la plataforma tiene por mundo: mundo, grabador y ActionDriver con la grabación de
// formato 7 abierta, y el bucle de pasos como DemoWorld::step.
struct Rig {
    explicit Rig(const std::string& text) {
        const rally::sim::WorldDesc desc = session_world();
        world.build(desc);
        world.seal();
        driver.load(text);
        driver.start(world);
        rally::io::RecordingHeader header;
        header.commit = "test";
        header.world = rally::io::serialize_world(desc);
        header.car_text = rally::tests::reference_car_text();
        header.zone_hash = world.zone_hash();
        header.actions_text = driver.text();
        recorder.begin(std::move(header), 64);
    }

    ApplyResult press(Player player, EventKind kind, std::uint8_t action) {
        return driver.submit(&recorder, player, kind, action);
    }
    void step(int count = 1, bool drive = false) {
        for (int i = 0; i < count; ++i) {
            if (drive) {
                input.throttle = rally::sim::kAxisMax;
            }
            recorder.ensure_capacity(1);
            world.step(input);
            recorder.append(input);
            driver.advance(world);
        }
    }
    rally::io::Recording finish() {
        return recorder.finish(world.state_hash(), rally::io::timed_steps(world),
                               rally::io::road_result(world), driver.timeline_hash());
    }

    rally::sim::SimWorld world;
    rally::sim::SimInput input;
    Recorder recorder;
    ActionDriver driver;
};

} // namespace

TEST_CASE(
    "action_driver: a session made like the platform replays with hash OK and same timeline") {
    const rally::sim::JoltRuntime runtime;
    Rig rig(kText);
    REQUIRE(rig.driver.enabled());
    // Al principio del fotograma, antes de simular; dos teclas en el mismo paso (orden) y otras
    // dos con la simulación en pausa (no hay pasos entre ellas).
    CHECK(rig.press(Player::Copilot, EventKind::Request, kStow).ok()); // paso 0
    rig.step(20);
    CHECK(rig.press(Player::Copilot, EventKind::Cancel, kStow).ok());  // al preparar
    CHECK(rig.press(Player::Copilot, EventKind::Request, kStow).ok()); // mismo paso (20)
    rig.step(100);
    CHECK(rig.press(Player::Copilot, EventKind::Cancel, kStow).ok());        // al ejecutar
    CHECK_FALSE(rig.press(Player::Copilot, EventKind::Request, kTake).ok()); // tablet fuera
    rig.step(40);
    CHECK(rig.press(Player::Copilot, EventKind::Request, kStow).ok());
    rig.step(300);
    CHECK(rig.press(Player::Copilot, EventKind::Request, kTake).ok());
    rig.step(300);
    // Coche parado: acepta; arranca y la cancela solo (el coche en marcha).
    CHECK(rig.press(Player::Driver, EventKind::Request, kStopped).ok());
    rig.step(1000, true);
    CHECK_FALSE(rig.press(Player::Driver, EventKind::Request, kStopped).ok()); // en marcha
    // Pausa: dos teclas seguidas con el mismo paso, y otra tras el último paso.
    CHECK(rig.press(Player::Copilot, EventKind::Request, kStow).ok());
    CHECK_FALSE(rig.press(Player::Copilot, EventKind::Request, kStow).ok()); // manos ocupadas

    const std::uint64_t live_hash = rig.driver.timeline_hash();
    const std::uint64_t last_step = rig.recorder.steps();
    const rally::io::Recording recording = rig.finish();
    REQUIRE(recording.events.size() == 11);
    CHECK(recording.events.front().step == 0);
    CHECK(recording.events[1].step == 20);
    CHECK(recording.events[2].step == 20);
    CHECK(recording.events[1].kind == EventKind::Cancel);
    CHECK(recording.events[2].kind == EventKind::Request);
    CHECK(recording.events.back().step == last_step);
    CHECK(recording.events[recording.events.size() - 2].step == last_step);
    CHECK(live_hash != 0);
    CHECK(recording.footer.actions_hash == live_hash);

    const rally::io::ParseResult parsed = rally::io::parse(rally::io::serialize(recording));
    REQUIRE_MESSAGE(parsed.ok, parsed.error);
    const rally::io::ReplayResult replayed = rally::io::replay(parsed.recording);
    CHECK_MESSAGE(replayed.hash_ok, replayed.problem);
    CHECK(replayed.actions_ran);
    CHECK(replayed.actions_hash == live_hash);
    // El coche cancela una acción por sí solo (se dice en pantalla con su motivo).
    std::array<rally::game::Transition, 64> pending{};
    bool by_car = false;
    for (std::size_t i = 0, n = rig.driver.drain(pending); i < n; ++i) {
        by_car = by_car || (pending[i].kind == rally::game::TransitionKind::Cancelled &&
                            pending[i].reason == rally::game::CancelReason::CarMoving);
    }
    CHECK(by_car);
}

TEST_CASE("action_driver: each event is recorded at the step about to be simulated") {
    const rally::sim::JoltRuntime runtime;
    Rig rig(kText);
    for (int i = 0; i < 5; ++i) {
        rig.step(7);
        const std::uint64_t before = rig.recorder.steps();
        CHECK(rig.driver.now() == static_cast<std::int64_t>(before));
        rig.press(Player::Copilot, EventKind::Cancel, kStow); // rechazada: también se graba
        REQUIRE(rig.recorder.recording().events.size() == static_cast<std::size_t>(i) + 1);
        CHECK(rig.recorder.recording().events.back().step == before);
    }
}

TEST_CASE("action_driver: start with a new world begins a new machine at step 0") {
    const rally::sim::JoltRuntime runtime;
    Rig rig(kText);
    rig.press(Player::Copilot, EventKind::Request, kStow);
    rig.step(100);
    CHECK(rig.driver.now() == 100);
    CHECK(rig.driver.copilot_progress().active);
    rig.driver.start(rig.world);
    CHECK(rig.driver.now() == 0);
    CHECK_FALSE(rig.driver.copilot_progress().active);
    CHECK_FALSE(rig.driver.tablet_stowed());
    std::array<rally::game::Transition, 8> pending{};
    CHECK(rig.driver.drain(pending) == 0); // nada de la máquina anterior
}

TEST_CASE("action_driver: stowing takes 192 steps and the ring fills evenly") {
    const rally::sim::JoltRuntime runtime;
    Rig rig(kText);
    CHECK_FALSE(rally::platform::ring_state(rig.driver.copilot_progress()).visible);
    const ApplyResult result = rig.press(Player::Copilot, EventKind::Request, kStow);
    REQUIRE(result.ok());
    CHECK(result.end - result.start == 192);
    CHECK(rally::platform::duration_text(result.end - result.start) == "0,80 s");
    rally::platform::RingState ring = rally::platform::ring_state(rig.driver.copilot_progress());
    CHECK(ring.visible);
    CHECK(ring.fraction == 0.0f);
    CHECK(rally::platform::phase_name(ring.phase) == "preparar");
    rig.step(96);
    ring = rally::platform::ring_state(rig.driver.copilot_progress());
    CHECK(ring.fraction == doctest::Approx(0.5f));
    CHECK(rally::platform::phase_name(ring.phase) == "ejecutar");
    rig.step(95);
    ring = rally::platform::ring_state(rig.driver.copilot_progress());
    CHECK(ring.visible);
    CHECK(rally::platform::phase_name(ring.phase) == "recoger");
    CHECK(ring.fraction == doctest::Approx(191.0f / 192.0f));
    rig.step(1);
    CHECK_FALSE(rally::platform::ring_state(rig.driver.copilot_progress()).visible);
    CHECK(rig.driver.tablet_stowed());
    // El efecto sobre la tablet se dice; los cambios de fase y el fin, no.
    std::array<rally::game::Transition, 16> pending{};
    const std::size_t n = rig.driver.drain(pending);
    std::vector<std::string> said;
    for (std::size_t i = 0; i < n; ++i) {
        const std::string text = rally::platform::transition_text(pending[i], rig.driver.table());
        if (!text.empty()) {
            said.push_back(text);
        }
    }
    REQUIRE(said.size() == 1);
    CHECK(said[0] == "tablet guardada");
}

TEST_CASE("action_driver: rejections say why") {
    const rally::sim::JoltRuntime runtime;
    Rig rig(kText);
    const auto& table = rig.driver.table();
    // Sacar con la tablet fuera.
    ApplyResult r = rig.press(Player::Copilot, EventKind::Request, kTake);
    CHECK(r.reject == Reject::TabletState);
    CHECK(rally::platform::reject_text(r.reject, &table.actions[kTake]) ==
          "la tablet no está guardada");
    // Guardar y, con la acción en curso, guardar otra vez: la tablet sigue fuera, pero las dos
    // manos están ocupadas (la máquina mira la tablet antes que las manos).
    CHECK(rig.press(Player::Copilot, EventKind::Request, kStow).ok());
    rig.step(10);
    r = rig.press(Player::Copilot, EventKind::Request, kStow);
    CHECK(r.reject == Reject::NoHands);
    CHECK(rally::platform::reject_text(r.reject, &table.actions[kStow]) ==
          "las manos están ocupadas");
    // Guardar con la tablet ya guardada.
    rig.step(200);
    r = rig.press(Player::Copilot, EventKind::Request, kStow);
    CHECK(r.reject == Reject::TabletState);
    CHECK(rally::platform::reject_text(r.reject, &table.actions[kStow]) ==
          "la tablet ya está guardada");
    CHECK(rally::platform::reject_text(Reject::CarMoving, nullptr) ==
          "el coche tiene que estar parado");
    CHECK(rally::platform::reject_text(Reject::NothingToCancel, nullptr) ==
          "no hay nada que cancelar");
    CHECK(rally::platform::reject_text(Reject::Ok, nullptr).empty());
}

TEST_CASE("action_driver: cancelling says what was lost") {
    const rally::sim::JoltRuntime runtime;
    Rig rig(kText);
    rig.press(Player::Copilot, EventKind::Request, kStow);
    rig.step(10);
    CHECK(rig.press(Player::Copilot, EventKind::Cancel, kStow).ok()); // al preparar
    rig.step(2);
    rig.press(Player::Copilot, EventKind::Request, kStow);
    rig.step(100); // 48 preparar + 52 ejecutar
    CHECK(rig.press(Player::Copilot, EventKind::Cancel, kStow).ok());
    std::array<rally::game::Transition, 16> pending{};
    const std::size_t n = rig.driver.drain(pending);
    std::vector<std::string> said;
    for (std::size_t i = 0; i < n; ++i) {
        const std::string text = rally::platform::transition_text(pending[i], rig.driver.table());
        if (!text.empty()) {
            said.push_back(text);
        }
    }
    REQUIRE(said.size() == 2);
    CHECK(said[0] == "acción cancelada (sin coste)");
    CHECK(said[1].rfind("acción cancelada: se pierden ", 0) == 0);
    CHECK_FALSE(rig.driver.copilot_progress().active);
}

TEST_CASE("action_driver: without an actions text there is no machine and nothing is recorded") {
    const rally::sim::JoltRuntime runtime;
    Rig rig("");
    CHECK_FALSE(rig.driver.enabled());
    const ApplyResult r = rig.press(Player::Copilot, EventKind::Request, kStow);
    CHECK_FALSE(r.ok());
    CHECK(r.reject == Reject::UnknownAction);
    rig.step(10);
    CHECK(rig.recorder.recording().events.empty());
    CHECK(rig.driver.timeline_hash() == 0);
    CHECK_FALSE(rally::platform::ring_state(rig.driver.copilot_progress()).visible);
    const rally::io::Recording recording = rig.finish();
    CHECK(recording.footer.actions_hash == 0);
    const rally::io::ReplayResult replayed = rally::io::replay(recording);
    CHECK_MESSAGE(replayed.hash_ok, replayed.problem);
    CHECK_FALSE(replayed.actions_ran);
}

TEST_CASE("action_driver: pending transitions that are not drained drop the oldest") {
    const rally::sim::JoltRuntime runtime;
    Rig rig(kText);
    // Pedir y cancelar al preparar da dos cambios cada vez (inicio de fase y cancelación).
    for (int i = 0; i < 40; ++i) {
        rig.press(Player::Copilot, EventKind::Request, kStow);
        rig.step(1);
        rig.press(Player::Copilot, EventKind::Cancel, kStow);
        rig.step(1);
    }
    CHECK(rig.driver.dropped() > 0);
    std::array<rally::game::Transition, ActionDriver::kPendingTransitions + 8> pending{};
    const std::size_t n = rig.driver.drain(pending);
    CHECK(n == ActionDriver::kPendingTransitions);
    for (std::size_t i = 1; i < n; ++i) {
        CHECK(pending[i].step >= pending[i - 1].step); // de la más vieja a la más nueva
    }
    CHECK(pending[n - 1].step > 70); // quedan las últimas, no las primeras
    CHECK(rig.driver.drain(pending) == 0);
    rig.driver.start(rig.world);
    CHECK(rig.driver.dropped() == 0);
}

TEST_CASE("action_driver: advance, the observer and the ring do not allocate") {
    const rally::sim::JoltRuntime runtime;
    Rig rig(kText);
    rig.press(Player::Copilot, EventKind::Request, kStow);
    rig.press(Player::Driver, EventKind::Request, kStopped);
    std::uint64_t allocations = 0;
    for (int i = 0; i < 500; ++i) {
        rig.input.throttle = i > 100 ? rally::sim::kAxisMax : 0;
        rig.recorder.ensure_capacity(1);
        rig.world.step(rig.input);
        rig.recorder.append(rig.input);
        const std::uint64_t before = rally::tests::global_new_count();
        rig.driver.advance(rig.world); // cambios de fase, efecto, fin y cancelación del coche
        const rally::platform::RingState ring =
            rally::platform::ring_state(rig.driver.copilot_progress());
        allocations += rally::tests::global_new_count() - before;
        (void)ring;
    }
    CHECK(allocations == 0);
    CHECK(rig.driver.tablet_stowed());
}
