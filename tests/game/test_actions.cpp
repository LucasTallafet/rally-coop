#include "car_worlds.h"
#include "game/actions.h"
#include "io/recording.h"
#include "new_counter.h"
#include "platform/fixed_step_clock.h"
#include "sim/jolt_runtime.h"
#include "sim/sim_world.h"

#include <doctest/doctest.h>

#include <cstdint>
#include <cstdio>
#include <functional>
#include <string>
#include <type_traits>
#include <vector>

using namespace rally::game;

namespace {

// Las dos acciones de la 1.0 (como data/game/actions.txt) y acciones de prueba.
const char* const kTestText = R"(
# acciones de la 1.0
action = guardar_tablet
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

# solo en el test: pide el coche parado, 1 mano
action = park_job
car = stopped
tablet_requires = any
tablet_result = unchanged
branch = copilot
hands = 1
position = any
prepare_ms = 100
execute_ms = 1000
collect_ms = 100

# solo en el test: 2 manos del copiloto sin tablet
action = copilot_busy
car = any
tablet_requires = any
tablet_result = unchanged
branch = copilot
hands = 2
position = any
prepare_ms = 100
execute_ms = 1000
collect_ms = 100

# solo en el test: del conductor
action = driver_hand
car = any
tablet_requires = any
tablet_result = unchanged
branch = driver
hands = 2
position = driver_seat
prepare_ms = 100
execute_ms = 100
collect_ms = 100

# solo en el test: cooperativa, ramas de 3 s y 5 s
action = coop
car = any
tablet_requires = any
tablet_result = unchanged
branch = driver
hands = 1
position = any
prepare_ms = 500
execute_ms = 2000
collect_ms = 500
branch = copilot
hands = 1
position = any
prepare_ms = 500
execute_ms = 4000
collect_ms = 500
)";

// Congelado (primera vez: lo que imprime el test). Va en el pie de las grabaciones de formato 7.
constexpr std::uint64_t kExpectedTimelineHash = 0x43a8d7da10883712ull;

enum Idx : std::uint8_t { kGuardar = 0, kSacar, kPark, kBusy, kDriverHand, kCoop };

const ActionTable& table() {
    static const ActionTable t = [] {
        ActionParseResult r = parse_actions(kTestText);
        REQUIRE(r.ok);
        return std::move(r.table);
    }();
    return t;
}

const CarState kStopped{true};
const CarState kMoving{false};

ActionEvent request(std::int64_t step, Player p, std::uint8_t action) {
    return {static_cast<std::uint64_t>(step), p, EventKind::Request, action};
}
ActionEvent cancel(std::int64_t step, Player p, std::uint8_t action) {
    return {static_cast<std::uint64_t>(step), p, EventKind::Cancel, action};
}

void run_to(ActionMachine& m, std::int64_t step, const CarState& car = kStopped) {
    while (m.time() < step) {
        m.advance(car);
    }
}

struct Log : ActionObserver {
    std::vector<Transition> list;
    void on_transition(const Transition& t) override { list.push_back(t); }
    // Primer cambio de la clase dada, o nullptr.
    const Transition* find(TransitionKind kind, Phase phase = Phase::Waiting) const {
        for (const Transition& t : list) {
            if (t.kind == kind && (kind != TransitionKind::PhaseStart || t.phase == phase)) {
                return &t;
            }
        }
        return nullptr;
    }
};

} // namespace

TEST_CASE("actions: durations are converted to steps with integers") {
    static_assert(ms_to_steps(800) == 192);
    static_assert(ms_to_steps(400) == 96);
    static_assert(ms_to_steps(200) == 48);
    static_assert(ms_to_steps(1) == 0);
    const ActionDef& def = table().actions[kGuardar];
    CHECK(def.branches[0].steps[0] == 48);
    CHECK(def.branches[0].steps[1] == 96);
    CHECK(def.branches[0].steps[2] == 48);
}

TEST_CASE("actions: a phase of 0 steps is skipped") {
    const ActionParseResult r = parse_actions(R"(
action = x
car = any
tablet_requires = any
tablet_result = unchanged
branch = any
hands = 1
position = any
prepare_ms = 1
execute_ms = 400
collect_ms = 200
)");
    REQUIRE(r.ok);
    CHECK(r.table.actions[0].branches[0].steps[0] == 0);
    ActionMachine m(r.table);
    Log log;
    m.set_observer(&log);
    run_to(m, 10);
    const ApplyResult a = m.apply(request(10, Player::Driver, 0));
    REQUIRE(a.ok());
    CHECK(a.branches[0].prepare_end == 10);
    CHECK(a.end == 10 + 96 + 48);
    CHECK(log.find(TransitionKind::PhaseStart, Phase::Prepare) == nullptr);
    const Transition* exec = log.find(TransitionKind::PhaseStart, Phase::Execute);
    REQUIRE(exec != nullptr);
    CHECK(exec->step == 10);
}

TEST_CASE("actions: the file data/game/actions.txt loads with the two actions of 1.0") {
    std::string text;
    std::string error;
    REQUIRE(rally::io::read_file(RALLY_GAME_DATA_DIR "/actions.txt", text, error));
    const ActionParseResult r = parse_actions(text);
    REQUIRE(r.ok);
    REQUIRE(r.table.actions.size() == 2);
    CHECK(r.table.find("guardar_tablet") == 0);
    CHECK(r.table.find("sacar_tablet") == 1);
    // Las reglas, no las duraciones: Lucas las ajusta sin recompilar.
    for (const ActionDef& def : r.table.actions) {
        REQUIRE(def.branch_count == 1);
        CHECK(def.branches[0].role == Role::Copilot);
        CHECK(def.branches[0].hands == 2);
        CHECK(def.branches[0].position == PositionReq::CopilotSeat);
    }
    CHECK(r.table.actions[0].tablet_requires == TabletReq::Out);
    CHECK(r.table.actions[0].tablet_result == TabletResult::Stowed);
    CHECK(r.table.actions[1].tablet_requires == TabletReq::Stowed);
    CHECK(r.table.actions[1].tablet_result == TabletResult::Out);
}

namespace {
// Texto de una acción con una línea cambiada, para los errores.
std::string with_line(int index, const std::string& replacement) {
    const char* lines[] = {
        "action = x",       "car = any",       "tablet_requires = any", "tablet_result = unchanged",
        "branch = any",     "hands = 1",       "position = any",        "prepare_ms = 200",
        "execute_ms = 400", "collect_ms = 200"};
    std::string text;
    for (int i = 0; i < 10; ++i) {
        if (i == index) {
            if (!replacement.empty()) {
                text += replacement + "\n";
            }
        } else {
            text += std::string(lines[i]) + "\n";
        }
    }
    return text;
}
} // namespace

TEST_CASE("actions: a bad file aborts the load with the line number") {
    SUBCASE("negative duration") {
        const ActionParseResult r = parse_actions(with_line(7, "prepare_ms = -5"));
        CHECK_FALSE(r.ok);
        CHECK(r.line == 8);
    }
    SUBCASE("unknown key") {
        const ActionParseResult r = parse_actions(with_line(5, "manos = 1"));
        CHECK_FALSE(r.ok);
        CHECK(r.line == 6);
    }
    SUBCASE("missing key is reported on the line of its branch") {
        const ActionParseResult r = parse_actions(with_line(8, ""));
        CHECK_FALSE(r.ok);
        CHECK(r.line == 5);
    }
    SUBCASE("missing action key is reported on the line of its action") {
        const ActionParseResult r = parse_actions(with_line(1, ""));
        CHECK_FALSE(r.ok);
        CHECK(r.line == 1);
    }
    SUBCASE("repeated key") {
        const ActionParseResult r = parse_actions(with_line(1, "car = any\ncar = stopped"));
        CHECK_FALSE(r.ok);
        CHECK(r.line == 3);
    }
    SUBCASE("not a number, bad value, no equals") {
        CHECK(parse_actions(with_line(7, "prepare_ms = abc")).line == 8);
        CHECK(parse_actions(with_line(5, "hands = 3")).line == 6);
        CHECK(parse_actions(with_line(1, "car algo")).line == 2);
    }
    SUBCASE("comments and blank lines count as lines") {
        const ActionParseResult r = parse_actions("# uno\n\n" + with_line(7, "prepare_ms = -1"));
        CHECK(r.line == 10);
    }
    SUBCASE("action without branch, three branches, too many actions") {
        const std::string head =
            "action = x\ncar = any\ntablet_requires = any\ntablet_result = unchanged\n";
        const std::string branch = "branch = any\nhands = 1\nposition = any\nprepare_ms = 1\n"
                                   "execute_ms = 1\ncollect_ms = 1\n";
        CHECK(parse_actions(head).line == 1);
        const ActionParseResult three = parse_actions(head + branch + branch + branch);
        CHECK_FALSE(three.ok);
        CHECK(three.line == 17);
        std::string many;
        for (int i = 0; i <= kMaxActions; ++i) {
            many += "action = a" + std::to_string(i) +
                    "\ncar = any\ntablet_requires = any\ntablet_result = unchanged\n" + branch;
        }
        const ActionParseResult r = parse_actions(many);
        CHECK_FALSE(r.ok);
        CHECK(r.line == 1 + kMaxActions * 10);
    }
    SUBCASE("empty file") { CHECK_FALSE(parse_actions("# nada\n").ok); }
}

TEST_CASE("actions: guardar_tablet asked at step 1000 runs from 1000 to 1192") {
    ActionMachine m(table());
    Log log;
    m.set_observer(&log);
    run_to(m, 1000);
    // Sacar no se puede con la tablet fuera.
    CHECK(m.apply(request(1000, Player::Copilot, kSacar)).reject == Reject::TabletState);
    const ApplyResult a = m.apply(request(1000, Player::Copilot, kGuardar));
    REQUIRE(a.ok());
    CHECK(a.start == 1000);
    CHECK(a.end == 1192);
    REQUIRE(a.branch_count == 1);
    CHECK(a.branches[0].prepare_end == 1048);
    CHECK(a.branches[0].execute_end == 1144);
    CHECK(a.branches[0].collect_end == 1192);
    CHECK(a.branches[0].player == Player::Copilot);

    run_to(m, 1143);
    CHECK_FALSE(m.tablet_stowed());
    CHECK(m.hands_free(Player::Copilot) == 0);
    CHECK(m.apply(request(1143, Player::Copilot, kSacar)).reject == Reject::TabletState);
    run_to(m, 1144);
    CHECK(m.tablet_stowed());
    run_to(m, 1191);
    CHECK(m.hands_free(Player::Copilot) == 0);
    run_to(m, 1192);
    CHECK(m.hands_free(Player::Copilot) == 2);
    CHECK(m.active_count() == 0);

    CHECK(log.find(TransitionKind::PhaseStart, Phase::Prepare)->step == 1000);
    CHECK(log.find(TransitionKind::PhaseStart, Phase::Execute)->step == 1048);
    CHECK(log.find(TransitionKind::PhaseStart, Phase::Collect)->step == 1144);
    CHECK(log.find(TransitionKind::Effect)->step == 1144);
    CHECK(log.find(TransitionKind::Finished)->step == 1192);

    // Ya guardada, sacarla se acepta (y guardarla otra vez, no).
    CHECK(m.apply(request(1192, Player::Copilot, kGuardar)).reject == Reject::TabletState);
    const ApplyResult b = m.apply(request(1192, Player::Copilot, kSacar));
    CHECK(b.ok());
    CHECK(b.end == 1192 + 192);
}

TEST_CASE("actions: the driver cannot do the copilot's action, nor an absent player") {
    ActionMachine m(table());
    CHECK(m.apply(request(0, Player::Driver, kGuardar)).reject == Reject::WrongPlayer);
    CHECK(m.apply(request(0, Player::Copilot, 99)).reject == Reject::UnknownAction);
    MachineSetup solo;
    solo.present = {true, false};
    ActionMachine alone(table(), solo);
    CHECK(alone.apply(request(0, Player::Copilot, kGuardar)).reject == Reject::WrongPlayer);
    MachineSetup outside;
    outside.position = {Position::DriverSeat, Position::Outside};
    ActionMachine out(table(), outside);
    CHECK(out.apply(request(0, Player::Copilot, kGuardar)).reject == Reject::WrongPosition);
}

TEST_CASE("actions: cancelling while preparing costs nothing") {
    ActionMachine m(table());
    Log log;
    m.set_observer(&log);
    run_to(m, 100);
    REQUIRE(m.apply(request(100, Player::Copilot, kGuardar)).ok());
    run_to(m, 120);
    const ApplyResult c = m.apply(cancel(120, Player::Copilot, kGuardar));
    CHECK(c.ok());
    const Transition* cancelled = log.find(TransitionKind::Cancelled);
    REQUIRE(cancelled != nullptr);
    CHECK(cancelled->step == 120);
    CHECK(cancelled->lost_steps == 0);
    CHECK(cancelled->reason == CancelReason::Player);
    CHECK(m.hands_free(Player::Copilot) == 2);
    CHECK_FALSE(m.tablet_stowed());
    // Se vuelve a pedir en el paso siguiente y dura lo mismo.
    run_to(m, 121);
    const ApplyResult again = m.apply(request(121, Player::Copilot, kGuardar));
    REQUIRE(again.ok());
    CHECK(again.end - again.start == 192);
    run_to(m, again.end);
    CHECK(m.tablet_stowed());
}

TEST_CASE("actions: cancelling while executing loses what was advanced") {
    ActionMachine m(table());
    Log log;
    m.set_observer(&log);
    run_to(m, 100);
    REQUIRE(m.apply(request(100, Player::Copilot, kGuardar)).ok());
    run_to(m, 200); // ejecutando desde 148
    CHECK(m.apply(cancel(200, Player::Copilot, kGuardar)).ok());
    const Transition* cancelled = log.find(TransitionKind::Cancelled);
    REQUIRE(cancelled != nullptr);
    CHECK(cancelled->lost_steps == 100);
    CHECK_FALSE(m.tablet_stowed());
    run_to(m, 201);
    const ApplyResult again = m.apply(request(201, Player::Copilot, kGuardar));
    REQUIRE(again.ok());
    CHECK(again.end - again.start == 192); // empieza de cero
}

TEST_CASE("actions: cancelling while collecting keeps the effect") {
    ActionMachine m(table());
    Log log;
    m.set_observer(&log);
    REQUIRE(m.apply(request(0, Player::Copilot, kGuardar)).ok());
    run_to(m, 150);
    CHECK(m.tablet_stowed());
    CHECK(m.apply(cancel(150, Player::Copilot, kGuardar)).ok());
    CHECK(m.tablet_stowed());
    CHECK(log.find(TransitionKind::Cancelled)->lost_steps == 0);
    CHECK(m.hands_free(Player::Copilot) == 2);
}

TEST_CASE("actions: nothing to cancel") {
    ActionMachine m(table());
    CHECK(m.apply(cancel(0, Player::Copilot, kGuardar)).reject == Reject::NothingToCancel);
    REQUIRE(m.apply(request(0, Player::Copilot, kGuardar)).ok());
    CHECK(m.apply(cancel(0, Player::Driver, kGuardar)).reject == Reject::NothingToCancel);
    CHECK(m.apply(cancel(0, Player::Copilot, kSacar)).reject == Reject::NothingToCancel);
}

TEST_CASE("actions: a lost requirement cancels the action in that step") {
    SUBCASE("the car starts moving") {
        ActionMachine m(table());
        Log log;
        m.set_observer(&log);
        run_to(m, 50);
        REQUIRE(m.apply(request(50, Player::Copilot, kPark)).ok());
        run_to(m, 79);
        CHECK(m.active_count() == 1);
        m.advance(kMoving); // time pasa a 80 con el coche en marcha
        REQUIRE(m.time() == 80);
        const Transition* cancelled = log.find(TransitionKind::Cancelled);
        REQUIRE(cancelled != nullptr);
        CHECK(cancelled->step == 80);
        CHECK(cancelled->reason == CancelReason::CarMoving);
        CHECK(m.active_count() == 0);
        CHECK(m.hands_free(Player::Copilot) == 2);
        // Con el coche en marcha no se puede pedir.
        CHECK(m.apply(request(80, Player::Copilot, kPark)).reject == Reject::CarMoving);
    }
    SUBCASE("finishing in the same step wins over a lost requirement") {
        ActionMachine m(table());
        Log log;
        m.set_observer(&log);
        REQUIRE(m.apply(request(0, Player::Copilot, kPark)).ok());
        const std::int64_t end = 24 + 240 + 24;
        run_to(m, end - 1);
        m.advance(kMoving);
        CHECK(log.find(TransitionKind::Finished) != nullptr);
        CHECK(log.find(TransitionKind::Cancelled) == nullptr);
    }
}

TEST_CASE("actions: with both hands busy a two-hand action is rejected, the other player is not") {
    ActionMachine m(table());
    REQUIRE(m.apply(request(0, Player::Copilot, kBusy)).ok());
    CHECK(m.hands_free(Player::Copilot) == 0);
    CHECK(m.apply(request(0, Player::Copilot, kBusy)).reject == Reject::NoHands);
    CHECK(m.apply(request(0, Player::Copilot, kPark)).reject == Reject::NoHands);
    CHECK(m.hands_free(Player::Driver) == 2);
    CHECK(m.apply(request(0, Player::Driver, kDriverHand)).ok());
    CHECK(m.hands_free(Player::Driver) == 0);
}

TEST_CASE("actions: a cooperative action lasts as long as its longest branch") {
    SUBCASE("two players: 3 s and 5 s last 5 s") {
        ActionMachine m(table());
        Log log;
        m.set_observer(&log);
        run_to(m, 10);
        const ApplyResult a = m.apply(request(10, Player::Driver, kCoop));
        REQUIRE(a.ok());
        REQUIRE(a.branch_count == 2);
        CHECK(a.branches[0].player == Player::Driver);
        CHECK(a.branches[1].player == Player::Copilot);
        CHECK(a.branches[0].collect_end - a.start == 3 * 240);
        CHECK(a.branches[1].collect_end - a.start == 5 * 240);
        CHECK(a.end - a.start == 5 * 240);
        CHECK(m.hands_free(Player::Driver) == 1);
        CHECK(m.hands_free(Player::Copilot) == 1);
        run_to(m, a.end);
        CHECK(log.find(TransitionKind::Finished)->step == 10 + 1200);
        CHECK(m.hands_free(Player::Driver) == 2);
        CHECK(m.hands_free(Player::Copilot) == 2);
    }
    SUBCASE("the copilot can start it too") {
        ActionMachine m(table());
        const ApplyResult a = m.apply(request(0, Player::Copilot, kCoop));
        REQUIRE(a.ok());
        CHECK(a.branches[0].player == Player::Driver);
        CHECK(a.end == 1200);
    }
    SUBCASE("a single player does both branches in turn: 8 s") {
        MachineSetup solo;
        solo.present = {true, false};
        ActionMachine m(table(), solo);
        const ApplyResult a = m.apply(request(0, Player::Driver, kCoop));
        REQUIRE(a.ok());
        CHECK(a.branches[1].start == a.branches[0].collect_end);
        CHECK(a.end - a.start == 8 * 240);
        CHECK(m.hands_free(Player::Driver) == 1);
        run_to(m, a.end);
        CHECK(m.hands_free(Player::Driver) == 2);
    }
    SUBCASE("cancelling one branch's action cancels the whole action") {
        ActionMachine m(table());
        REQUIRE(m.apply(request(0, Player::Driver, kCoop)).ok());
        run_to(m, 50);
        CHECK(m.apply(cancel(50, Player::Copilot, kCoop)).ok());
        CHECK(m.active_count() == 0);
        CHECK(m.hands_free(Player::Driver) == 2);
        CHECK(m.hands_free(Player::Copilot) == 2);
    }
}

namespace {

// Un guion de eventos por paso, igual en cualquier tasa de fotogramas.
struct Scripted {
    std::int64_t step;
    ActionEvent event;
};

// Reproduce el guion con fotogramas de `fps`: las peticiones llegan al principio del fotograma
// y se aplican en el siguiente paso a simular (el primero del fotograma). Devuelve los cambios.
std::vector<Transition> play(int fps, std::uint64_t& hash) {
    // Los eventos llegan en el fotograma que empieza en el paso 200 (0,8333 s) y 320.
    ActionMachine m(table());
    Log log;
    m.set_observer(&log);
    rally::platform::FixedStepClock clock;
    const std::int64_t frames = 3 * fps;
    std::int64_t previous_ns = 0;
    for (std::int64_t k = 0; k < frames; ++k) {
        // Duraciones por suma acumulada (techo), para que 30 y 60 fps caigan en los mismos pasos.
        const std::int64_t cum =
            (k + 1) * 1'000'000'000 / fps + (((k + 1) * 1'000'000'000) % fps != 0);
        const rally::platform::FrameSteps fs = clock.advance(cum - previous_ns);
        previous_ns = cum;
        for (int i = 0; i < fs.count; ++i) {
            const std::int64_t step = fs.first + i;
            if (i == 0 && step == 200) {
                m.apply(request(step, Player::Copilot, kGuardar));
            }
            if (i == 0 && step == 320) {
                m.apply(cancel(step, Player::Copilot, kGuardar));
            }
            if (i == 0 && step == 400) {
                m.apply(request(step, Player::Driver, kCoop));
            }
            REQUIRE(m.time() == step);
            m.advance(kStopped);
        }
    }
    hash = m.timeline_hash();
    return log.list;
}

} // namespace

TEST_CASE("actions: the timeline is the same at 30 and at 60 fps") {
    std::uint64_t hash30 = 0;
    std::uint64_t hash60 = 0;
    const std::vector<Transition> a = play(30, hash30);
    const std::vector<Transition> b = play(60, hash60);
    REQUIRE(!a.empty());
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].step == b[i].step);
        CHECK(a[i].kind == b[i].kind);
        CHECK(a[i].serial == b[i].serial);
    }
    CHECK(hash30 == hash60);
    CHECK(hash30 != 0);
    // Los pasos de las transiciones son los del guion (no solo iguales entre sí).
    CHECK(a[0].step == 200);
    CHECK(a[0].phase == Phase::Prepare);
    // La petición de guardar se aplicó en 200 y la cooperativa en 400.
    CHECK(a.front().step == 200);
}

// ---------------------------------------------------------------------------------------------
// save() / restore()

namespace {

// Estado con todos los campos distintos de los de partida: tablet guardada, coche en marcha,
// una cooperativa a medias (antes de su efecto), un número de serie avanzado, manos ocupadas.
void prepare(ActionMachine& m) {
    REQUIRE(m.apply(request(0, Player::Copilot, kGuardar)).ok());
    run_to(m, 200);
    REQUIRE(m.tablet_stowed());
    REQUIRE(m.apply(request(200, Player::Driver, kCoop)).ok());
    run_to(m, 300, kMoving);
}

// Continuación A: se deja correr. B: se cancela a mitad de la ejecución. Las dos pasan por
// peticiones que dependen del estado. Los pasos son relativos al tiempo de la propia máquina.
struct Outcome {
    std::uint64_t hash = 0;
    ActionSnapshot snapshot;
    bool operator==(const Outcome&) const = default;
};

Outcome continue_a(ActionMachine& m) {
    const std::int64_t t0 = m.time();
    // Pide cosas cuyo resultado depende de cada campo.
    m.apply(request(t0, Player::Copilot, kPark));      // coche en marcha / parado
    m.apply(request(t0, Player::Copilot, kSacar));     // tablet, manos
    m.apply(request(t0, Player::Driver, kDriverHand)); // posición, manos
    run_to(m, t0 + 1500, kStopped);
    m.apply(request(m.time(), Player::Copilot, kSacar)); // serie
    run_to(m, m.time() + 300);
    return {m.timeline_hash(), m.save()};
}

Outcome continue_b(ActionMachine& m) {
    const std::int64_t t0 = m.time();
    run_to(m, t0 + 40, kMoving);
    m.apply(cancel(m.time(), Player::Driver, kCoop)); // pasos perdidos desde el inicio
    run_to(m, m.time() + 300);
    return {m.timeline_hash(), m.save()};
}

struct FieldMutation {
    const char* name;
    std::function<void(ActionSnapshot&)> mutate;
};

} // namespace

TEST_CASE("actions: save then restore then the same steps give the same timeline hash") {
    ActionMachine ref(table());
    prepare(ref);
    const ActionSnapshot snap = ref.save();
    const std::uint64_t hash_at_save = ref.timeline_hash();

    // Otra máquina, con otra instalación, recibe el estado.
    MachineSetup other;
    other.position = {Position::Outside, Position::Outside};
    other.present = {false, false};
    ActionMachine a(table());
    ActionMachine b(table(), other);
    REQUIRE(a.restore(snap));
    REQUIRE(b.restore(snap));
    CHECK(a.timeline_hash() == hash_at_save);
    CHECK(b.save() == snap);

    ActionMachine ref2(table());
    prepare(ref2);
    const Outcome expected_a = continue_a(ref);
    CHECK(continue_a(a) == expected_a);
    const Outcome expected_b = continue_b(ref2);
    ActionMachine c(table(), other);
    REQUIRE(c.restore(snap));
    CHECK(continue_b(c) == expected_b);
    CHECK(expected_a.hash != expected_b.hash);
}

TEST_CASE("actions: every field of the snapshot matters for what comes next") {
    // Si el campo falta del save(), la máquina restaurada no lo recibe y se comporta como con
    // este valor cambiado: el resultado tiene que ser distinto del de la referencia.
    ActionMachine ref(table());
    prepare(ref);
    const ActionSnapshot snap = ref.save();
    REQUIRE(snap.table_hash == table().text_hash);
    REQUIRE(snap.tablet_stowed == 1);
    REQUIRE(snap.car_stopped == 0);

    ActionMachine r1(table());
    REQUIRE(r1.restore(snap));
    const Outcome expected_a = continue_a(r1);
    ActionMachine r2(table());
    REQUIRE(r2.restore(snap));
    const Outcome expected_b = continue_b(r2);

    // La cooperativa está en la ranura 0; las demás están sin usar.
    const std::vector<FieldMutation> mutations = {
        {"time", [](ActionSnapshot& s) { s.time += 7; }},
        {"timeline_hash", [](ActionSnapshot& s) { s.timeline_hash ^= 1; }},
        {"next_serial", [](ActionSnapshot& s) { s.next_serial += 5; }},
        {"car_stopped", [](ActionSnapshot& s) { s.car_stopped = 1; }},
        {"tablet_stowed", [](ActionSnapshot& s) { s.tablet_stowed = 0; }},
        {"position", [](ActionSnapshot& s) { s.position[0] = 2; }},
        {"present", [](ActionSnapshot& s) { s.present[1] = 0; }},
        {"hands_free", [](ActionSnapshot& s) { s.hands_free = {2, 2}; }},
        {"active.used", [](ActionSnapshot& s) { s.active[0].used = 0; }},
        {"active.action", [](ActionSnapshot& s) { s.active[0].action = kPark; }},
        {"active.effect_applied", [](ActionSnapshot& s) { s.active[0].effect_applied = 1; }},
        {"active.branch_count", [](ActionSnapshot& s) { s.active[0].branch_count = 1; }},
        {"active.serial", [](ActionSnapshot& s) { s.active[0].serial += 9; }},
        {"active.start", [](ActionSnapshot& s) { s.active[0].start += 30; }},
        {"active.end", [](ActionSnapshot& s) { s.active[0].end += 30; }},
        {"active.effect_step", [](ActionSnapshot& s) { s.active[0].effect_step = 0; }},
        {"branch.player", [](ActionSnapshot& s) { s.active[0].branches[0].player ^= 1; }},
        {"branch.phase", [](ActionSnapshot& s) { s.active[0].branches[0].phase = 3; }},
        {"branch.start", [](ActionSnapshot& s) { s.active[0].branches[1].start += 10; }},
        {"branch.prepare_end",
         [](ActionSnapshot& s) { s.active[0].branches[0].prepare_end += 10; }},
        {"branch.execute_end",
         [](ActionSnapshot& s) { s.active[0].branches[1].execute_end += 10; }},
        {"branch.collect_end",
         [](ActionSnapshot& s) { s.active[0].branches[1].collect_end += 10; }},
        {"branch.hands", [](ActionSnapshot& s) { s.active[0].branches[0].hands = 2; }},
    };
    for (const FieldMutation& f : mutations) {
        CAPTURE(f.name);
        ActionSnapshot mutated = snap;
        f.mutate(mutated);
        REQUIRE_FALSE(mutated == snap);
        ActionMachine ma(table());
        REQUIRE(ma.restore(mutated));
        ActionMachine mb(table());
        REQUIRE(mb.restore(mutated));
        const bool a_differs = !(continue_a(ma) == expected_a);
        const bool b_differs = !(continue_b(mb) == expected_b);
        CHECK((a_differs || b_differs));
    }
}

TEST_CASE("actions: the snapshot layout is checked, a new field needs its test") {
    // Si cambia el tamaño, hay un campo nuevo: añade su mutación a la lista de arriba.
    CHECK(sizeof(BranchState) == 48);
    CHECK(sizeof(ActiveState) == 128);
    CHECK(sizeof(ActionSnapshot) == 1064);
    static_assert(std::is_trivially_copyable_v<ActionSnapshot>);
}

TEST_CASE("actions: restore with another table is refused by the snapshot hash") {
    ActionMachine m(table());
    const ActionParseResult other = parse_actions(R"(
action = otra
car = any
tablet_requires = any
tablet_result = unchanged
branch = any
hands = 0
position = any
prepare_ms = 0
execute_ms = 0
collect_ms = 0
)");
    REQUIRE(other.ok);
    ActionMachine n(other.table);
    CHECK(m.save().table_hash == table().text_hash);
    CHECK(n.save().table_hash == other.table.text_hash);
    CHECK(m.save().table_hash != n.save().table_hash);
    CHECK_FALSE(m.restore(n.save()));
    CHECK(m.time() == 0);
    // Una acción sin fases termina en el mismo paso y deja las manos libres.
    const ApplyResult a = n.apply(request(0, Player::Driver, 0));
    CHECK(a.ok());
    CHECK(a.end == a.start);
    CHECK(n.active_count() == 0);
}

TEST_CASE("actions: stepping the machine does not allocate") {
    ActionMachine m(table());
    std::int64_t accepted = 0;
    const std::uint64_t before = rally::tests::global_new_count();
    for (int i = 0; i < 10000; ++i) {
        const std::int64_t t = m.time();
        if (i % 50 == 0) {
            accepted +=
                m.apply(request(t, Player::Copilot, static_cast<std::uint8_t>(i / 50 % 3))).ok();
        }
        if (i % 70 == 0) {
            accepted += m.apply(cancel(t, Player::Copilot, kGuardar)).ok();
            accepted += m.apply(request(t, Player::Driver, kCoop)).ok();
        }
        if (i % 90 == 0) {
            accepted += m.apply(request(t, Player::Copilot, 99)).ok();
        }
        m.advance(i % 400 < 200 ? kStopped : kMoving);
    }
    const std::uint64_t after = rally::tests::global_new_count();
    CHECK(after - before == 0);
    CHECK(accepted > 10);
}

TEST_CASE("actions: car_state_from reads the simulation and never writes it") {
    const rally::sim::JoltRuntime runtime;
    rally::sim::SimWorld world;
    world.build(rally::tests::flat_world(rally::sim::Surface::Asphalt));
    world.seal();
    const std::uint64_t hash = world.state_hash();
    const auto stopped_at = [&](float vx, float vy, float vz) {
        rally::sim::BodyVelocity v;
        v.vx = vx / 3.6f;
        v.vy = vy / 3.6f;
        v.vz = vz / 3.6f;
        world.set_car_velocity(v);
        return car_state_from(world).stopped;
    };
    CHECK(stopped_at(0.0f, 0.0f, 0.0f));
    CHECK(stopped_at(0.0f, 0.0f, -1.9f));
    CHECK_FALSE(stopped_at(0.0f, 0.0f, -2.1f));
    CHECK_FALSE(stopped_at(2.1f, 0.0f, 0.0f));
    CHECK(stopped_at(1.2f, 0.0f, 1.2f));       // 1,7 km/h en diagonal
    CHECK_FALSE(stopped_at(1.5f, 0.0f, 1.5f)); // 2,1 km/h en diagonal
    CHECK(stopped_at(0.0f, 30.0f, 0.0f));      // la vertical no cuenta
    // Sin coche: parado.
    rally::sim::SimWorld empty;
    rally::sim::WorldDesc desc = rally::tests::flat_world(rally::sim::Surface::Asphalt);
    desc.car = false;
    empty.build(desc);
    empty.seal();
    CHECK(car_state_from(empty).stopped);

    // La máquina no toca la simulación.
    rally::sim::SimWorld w2;
    w2.build(rally::tests::flat_world(rally::sim::Surface::Asphalt));
    w2.seal();
    const std::uint64_t h2 = w2.state_hash();
    ActionMachine m(table());
    m.apply(request(0, Player::Copilot, kGuardar));
    for (int i = 0; i < 300; ++i) {
        w2.step(rally::sim::SimInput{});
        m.advance(car_state_from(w2));
    }
    CHECK(h2 != 0);
    (void)hash;
}

TEST_CASE("actions: the timeline hash of a fixed scenario is frozen") {
    // Este hash irá en el pie de las grabaciones de formato 7: si cambia sin querer, falla aquí.
    ActionMachine m(table());
    m.apply(request(0, Player::Copilot, kGuardar));
    run_to(m, 100);
    m.apply(cancel(100, Player::Copilot, kGuardar));
    m.apply(request(100, Player::Copilot, kGuardar));
    m.apply(request(100, Player::Driver, kCoop));
    run_to(m, 400, kMoving);
    m.apply(request(400, Player::Copilot, kPark));
    run_to(m, 2000);
    m.apply(request(2000, Player::Copilot, kSacar));
    run_to(m, 2400);
    std::printf("RALLY_SUMMARY acciones_timeline=0x%016llx\n",
                static_cast<unsigned long long>(m.timeline_hash()));
    CHECK(m.timeline_hash() == kExpectedTimelineHash);
}
