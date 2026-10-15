#include "game/actions.h"

#include "sim/check.h"
#include "sim/drivetrain.h"
#include "sim/sim_world.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <string>
#include <system_error>

namespace rally::game {

namespace {

// FNV-1a de 64 bits, propio: la máquina tiene que poder guardar y restaurar su acumulador
// (sim::Fnv1a64 no tiene setter). Los enteros entran en little-endian, byte a byte.
constexpr std::uint64_t kFnvOffset = 0xcbf2'9ce4'8422'2325ull;
constexpr std::uint64_t kFnvPrime = 0x0000'0100'0000'01b3ull;

std::uint64_t fnv_byte(std::uint64_t hash, std::uint8_t byte) {
    hash ^= byte;
    return hash * kFnvPrime;
}

std::uint64_t fnv_value(std::uint64_t hash, std::int64_t value) {
    const auto bits = static_cast<std::uint64_t>(value);
    for (int i = 0; i < 8; ++i) {
        hash = fnv_byte(hash, static_cast<std::uint8_t>(bits >> (8 * i)));
    }
    return hash;
}

std::uint64_t fnv_text(std::string_view text) {
    std::uint64_t hash = kFnvOffset;
    for (const char c : text) {
        hash = fnv_byte(hash, static_cast<std::uint8_t>(c));
    }
    return hash;
}

// ---------------------------------------------------------------------------------------------
// Carga del texto

std::string_view trim(std::string_view text) {
    constexpr std::string_view kBlank = " \t\r";
    const std::size_t begin = text.find_first_not_of(kBlank);
    if (begin == std::string_view::npos) {
        return {};
    }
    const std::size_t end = text.find_last_not_of(kBlank);
    return text.substr(begin, end - begin + 1);
}

// Bits de las claves ya vistas.
enum ActionKey : unsigned { kKeyCar = 1, kKeyTabletRequires = 2, kKeyTabletResult = 4 };
enum BranchKey : unsigned {
    kKeyHands = 1,
    kKeyPosition = 2,
    kKeyPrepare = 4,
    kKeyExecute = 8,
    kKeyCollect = 16
};
constexpr unsigned kAllActionKeys = kKeyCar | kKeyTabletRequires | kKeyTabletResult;
constexpr unsigned kAllBranchKeys =
    kKeyHands | kKeyPosition | kKeyPrepare | kKeyExecute | kKeyCollect;

struct Parser {
    ActionParseResult result;
    int action_line = 0; // línea del action abierto
    int branch_line = 0; // línea del branch abierto
    unsigned action_seen = 0;
    unsigned branch_seen = 0;
    bool in_action = false;
    bool in_branch = false;

    bool fail(int line, std::string message) {
        result.ok = false;
        result.line = line;
        result.message = std::move(message);
        return false;
    }

    ActionDef& action() { return result.table.actions.back(); }
    BranchDef& branch() { return action().branches[action().branch_count - 1]; }

    bool close_branch() {
        if (!in_branch) {
            return true;
        }
        in_branch = false;
        if (branch_seen != kAllBranchKeys) {
            static constexpr const char* kNames[] = {"hands", "position", "prepare_ms",
                                                     "execute_ms", "collect_ms"};
            for (unsigned i = 0; i < 5; ++i) {
                if ((branch_seen & (1u << i)) == 0) {
                    return fail(branch_line,
                                std::string("a la rama le falta la clave '") + kNames[i] + "'");
                }
            }
        }
        BranchDef& b = branch();
        for (int i = 0; i < 3; ++i) {
            b.steps[i] = static_cast<std::int32_t>(ms_to_steps(b.ms[i]));
        }
        return true;
    }

    bool close_action() {
        if (!close_branch()) {
            return false;
        }
        if (!in_action) {
            return true;
        }
        in_action = false;
        if (action_seen != kAllActionKeys) {
            static constexpr const char* kNames[] = {"car", "tablet_requires", "tablet_result"};
            for (unsigned i = 0; i < 3; ++i) {
                if ((action_seen & (1u << i)) == 0) {
                    return fail(action_line,
                                std::string("a la acción le falta la clave '") + kNames[i] + "'");
                }
            }
        }
        if (action().branch_count == 0) {
            return fail(action_line, "la acción no tiene ninguna rama (branch)");
        }
        return true;
    }

    bool parse_int(std::string_view value, std::int64_t& out, int line, const char* key) {
        const char* first = value.data();
        const char* last = first + value.size();
        const auto [ptr, ec] = std::from_chars(first, last, out);
        if (ec != std::errc{} || ptr != last || value.empty()) {
            return fail(line, std::string("valor no numérico en '") + key + "'");
        }
        return true;
    }

    bool parse_ms(std::string_view value, std::int32_t& out, int line, const char* key) {
        std::int64_t ms = 0;
        if (!parse_int(value, ms, line, key)) {
            return false;
        }
        if (ms < 0) {
            return fail(line, std::string("duración negativa en '") + key + "'");
        }
        if (ms > kMaxPhaseMs) {
            return fail(line, std::string("duración demasiado larga en '") + key + "'");
        }
        out = static_cast<std::int32_t>(ms);
        return true;
    }

    bool open_action(std::string_view name, int line) {
        if (!close_action()) {
            return false;
        }
        if (name.empty() || name.find_first_of(" \t") != std::string_view::npos) {
            return fail(line, "nombre de acción vacío o con espacios");
        }
        if (result.table.find(name) >= 0) {
            return fail(line, "acción repetida: " + std::string(name));
        }
        if (static_cast<int>(result.table.actions.size()) >= kMaxActions) {
            return fail(line, "demasiadas acciones");
        }
        result.table.actions.emplace_back();
        action().name = std::string(name);
        in_action = true;
        action_line = line;
        action_seen = 0;
        return true;
    }

    bool open_branch(std::string_view value, int line) {
        if (!in_action) {
            return fail(line, "'branch' antes de 'action'");
        }
        if (!close_branch()) {
            return false;
        }
        if (action().branch_count >= kMaxBranches) {
            return fail(line, "demasiadas ramas");
        }
        Role role = Role::Any;
        if (value == "driver") {
            role = Role::Driver;
        } else if (value == "copilot") {
            role = Role::Copilot;
        } else if (value != "any") {
            return fail(line, "rol desconocido (driver, copilot o any)");
        }
        ++action().branch_count;
        branch() = BranchDef{};
        branch().role = role;
        in_branch = true;
        branch_line = line;
        branch_seen = 0;
        return true;
    }

    bool set_once(unsigned& seen, unsigned bit, int line, const char* key) {
        if ((seen & bit) != 0) {
            return fail(line, std::string("clave repetida: ") + key);
        }
        seen |= bit;
        return true;
    }

    bool action_key(std::string_view key, std::string_view value, int line) {
        if (key == "car") {
            if (!set_once(action_seen, kKeyCar, line, "car")) {
                return false;
            }
            if (value == "any") {
                action().car = CarReq::Any;
            } else if (value == "stopped") {
                action().car = CarReq::Stopped;
            } else {
                return fail(line, "valor desconocido en 'car' (any o stopped)");
            }
            return true;
        }
        if (key == "tablet_requires") {
            if (!set_once(action_seen, kKeyTabletRequires, line, "tablet_requires")) {
                return false;
            }
            if (value == "any") {
                action().tablet_requires = TabletReq::Any;
            } else if (value == "stowed") {
                action().tablet_requires = TabletReq::Stowed;
            } else if (value == "out") {
                action().tablet_requires = TabletReq::Out;
            } else {
                return fail(line, "valor desconocido en 'tablet_requires' (any, stowed u out)");
            }
            return true;
        }
        if (key == "tablet_result") {
            if (!set_once(action_seen, kKeyTabletResult, line, "tablet_result")) {
                return false;
            }
            if (value == "unchanged") {
                action().tablet_result = TabletResult::Unchanged;
            } else if (value == "stowed") {
                action().tablet_result = TabletResult::Stowed;
            } else if (value == "out") {
                action().tablet_result = TabletResult::Out;
            } else {
                return fail(line, "valor desconocido en 'tablet_result' (unchanged, stowed u out)");
            }
            return true;
        }
        return fail(line, "clave desconocida: " + std::string(key));
    }

    bool branch_key(std::string_view key, std::string_view value, int line) {
        if (key == "hands") {
            if (!set_once(branch_seen, kKeyHands, line, "hands")) {
                return false;
            }
            std::int64_t hands = 0;
            if (!parse_int(value, hands, line, "hands")) {
                return false;
            }
            if (hands < 0 || hands > 2) {
                return fail(line, "'hands' tiene que estar entre 0 y 2");
            }
            branch().hands = static_cast<std::uint8_t>(hands);
            return true;
        }
        if (key == "position") {
            if (!set_once(branch_seen, kKeyPosition, line, "position")) {
                return false;
            }
            if (value == "driver_seat") {
                branch().position = PositionReq::DriverSeat;
            } else if (value == "copilot_seat") {
                branch().position = PositionReq::CopilotSeat;
            } else if (value == "any") {
                branch().position = PositionReq::Any;
            } else {
                return fail(line,
                            "valor desconocido en 'position' (driver_seat, copilot_seat o any)");
            }
            return true;
        }
        if (key == "prepare_ms") {
            return set_once(branch_seen, kKeyPrepare, line, "prepare_ms") &&
                   parse_ms(value, branch().ms[0], line, "prepare_ms");
        }
        if (key == "execute_ms") {
            return set_once(branch_seen, kKeyExecute, line, "execute_ms") &&
                   parse_ms(value, branch().ms[1], line, "execute_ms");
        }
        if (key == "collect_ms") {
            return set_once(branch_seen, kKeyCollect, line, "collect_ms") &&
                   parse_ms(value, branch().ms[2], line, "collect_ms");
        }
        return fail(line, "clave desconocida: " + std::string(key));
    }

    bool line_of(std::string_view raw, int line) {
        std::string_view text = raw;
        const std::size_t hash = text.find('#');
        if (hash != std::string_view::npos) {
            text = text.substr(0, hash);
        }
        text = trim(text);
        if (text.empty()) {
            return true;
        }
        const std::size_t eq = text.find('=');
        if (eq == std::string_view::npos) {
            return fail(line, "falta '=' (se espera clave = valor)");
        }
        const std::string_view key = trim(text.substr(0, eq));
        const std::string_view value = trim(text.substr(eq + 1));
        if (key == "action") {
            return open_action(value, line);
        }
        if (key == "branch") {
            return open_branch(value, line);
        }
        const bool is_branch_key = key == "hands" || key == "position" || key == "prepare_ms" ||
                                   key == "execute_ms" || key == "collect_ms";
        if (is_branch_key) {
            if (!in_branch) {
                return fail(line, "clave de rama fuera de una rama (falta 'branch')");
            }
            return branch_key(key, value, line);
        }
        if (!in_action) {
            if (key == "car" || key == "tablet_requires" || key == "tablet_result") {
                return fail(line, "clave de acción antes de 'action'");
            }
            return fail(line, "clave desconocida: " + std::string(key));
        }
        if (action().branch_count > 0) {
            if (key == "car" || key == "tablet_requires" || key == "tablet_result") {
                return fail(line, "las claves de la acción van antes de sus ramas");
            }
        }
        return action_key(key, value, line);
    }
};

// ¿Puede hacer esta rama un jugador?
bool role_allows(Role role, Player player) {
    return role == Role::Any ||
           static_cast<std::uint8_t>(role) == static_cast<std::uint8_t>(player);
}

bool position_allows(PositionReq req, Position position) {
    switch (req) {
    case PositionReq::DriverSeat:
        return position == Position::DriverSeat;
    case PositionReq::CopilotSeat:
        return position == Position::CopilotSeat;
    case PositionReq::Any:
        return true;
    }
    return false;
}

Phase phase_at(const BranchState& b, std::int64_t t) {
    if (t < b.start) {
        return Phase::Waiting;
    }
    if (t < b.prepare_end) {
        return Phase::Prepare;
    }
    if (t < b.execute_end) {
        return Phase::Execute;
    }
    if (t < b.collect_end) {
        return Phase::Collect;
    }
    return Phase::Done;
}

} // namespace

int ActionTable::find(std::string_view name) const {
    for (std::size_t i = 0; i < actions.size(); ++i) {
        if (actions[i].name == name) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

ActionParseResult parse_actions(std::string_view text) {
    Parser parser;
    parser.result.table.text_hash = fnv_text(text);
    int line = 0;
    std::size_t pos = 0;
    bool ok = true;
    while (ok && pos <= text.size()) {
        ++line;
        std::size_t end = text.find('\n', pos);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        ok = parser.line_of(text.substr(pos, end - pos), line);
        pos = end + 1;
    }
    if (ok) {
        // Un error al cerrar (clave que falta) se atribuye a la línea de su action o branch.
        ok = parser.close_action();
    }
    if (ok && parser.result.table.actions.empty()) {
        ok = parser.fail(line, "el archivo no define ninguna acción");
    }
    if (ok) {
        parser.result.ok = true;
        parser.result.line = 0;
    }
    return std::move(parser.result);
}

ActionTable parse_actions_or_die(std::string_view text, const char* origin) {
    ActionParseResult parsed = parse_actions(text);
    if (!parsed.ok) {
        char message[256];
        std::snprintf(message, sizeof message, "%s: línea %d: %s", origin, parsed.line,
                      parsed.message.c_str());
        RALLY_CHECK(false, message);
    }
    return std::move(parsed.table);
}

CarState car_state_from(const sim::SimWorld& world) {
    CarState car;
    if (!world.has_car()) {
        return car;
    }
    const sim::BodyVelocity v = world.car_velocity();
    const float horizontal_sq = v.vx * v.vx + v.vz * v.vz;
    car.stopped = horizontal_sq < sim::kGearStopSpeed * sim::kGearStopSpeed;
    return car;
}

// ---------------------------------------------------------------------------------------------
// Máquina

ActionMachine::ActionMachine(const ActionTable& table, const MachineSetup& setup) : table_(&table) {
    state_.table_hash = table.text_hash;
    state_.car_stopped = setup.car.stopped ? 1 : 0;
    state_.timeline_hash = kFnvOffset;
    for (int p = 0; p < kPlayers; ++p) {
        state_.position[static_cast<std::size_t>(p)] =
            static_cast<std::uint8_t>(setup.position[static_cast<std::size_t>(p)]);
        state_.present[static_cast<std::size_t>(p)] =
            setup.present[static_cast<std::size_t>(p)] ? 1 : 0;
        state_.hands_free[static_cast<std::size_t>(p)] = 2;
    }
}

void ActionMachine::hash_value(std::int64_t value) {
    state_.timeline_hash = fnv_value(state_.timeline_hash, value);
}

void ActionMachine::emit(const Transition& transition) {
    hash_value(2);
    hash_value(transition.step);
    hash_value(static_cast<std::int64_t>(transition.kind));
    hash_value(transition.serial);
    hash_value(transition.action);
    hash_value(transition.branch);
    hash_value(static_cast<std::int64_t>(transition.player));
    hash_value(static_cast<std::int64_t>(transition.phase));
    hash_value(static_cast<std::int64_t>(transition.reason));
    hash_value(transition.lost_steps);
    if (observer_ != nullptr) {
        observer_->on_transition(transition);
    }
}

std::uint8_t ActionMachine::hands_free(Player player) const {
    return state_.hands_free[static_cast<std::size_t>(player)];
}

int ActionMachine::active_count() const {
    int count = 0;
    for (const ActiveState& a : state_.active) {
        count += a.used != 0 ? 1 : 0;
    }
    return count;
}

ActionProgress ActionMachine::progress(Player player) const {
    ActionProgress progress;
    for (const ActiveState& a : state_.active) {
        if (a.used == 0) {
            continue;
        }
        for (int i = 0; i < a.branch_count; ++i) {
            const BranchState& b = a.branches[static_cast<std::size_t>(i)];
            if (b.player == static_cast<std::uint8_t>(player)) {
                progress.active = true;
                progress.action = a.action;
                progress.phase = static_cast<Phase>(b.phase);
                progress.elapsed = state_.time - a.start;
                progress.total = a.end - a.start;
                return progress;
            }
        }
    }
    return progress;
}

bool ActionMachine::restore(const ActionSnapshot& snapshot) {
    if (snapshot.table_hash != table_->text_hash) {
        return false;
    }
    state_ = snapshot;
    return true;
}

ApplyResult ActionMachine::apply(const ActionEvent& event) {
    RALLY_CHECK(static_cast<std::int64_t>(event.step) == state_.time,
                "ActionMachine::apply: el evento no es del paso que se va a simular");
    ApplyResult result = event.kind == EventKind::Request ? request(event) : cancel(event);
    return result;
}

ApplyResult ActionMachine::request(const ActionEvent& event) {
    ApplyResult result;
    const auto reject = [&](Reject reason) {
        result = ApplyResult{};
        result.reject = reason;
        hash_value(1);
        hash_value(static_cast<std::int64_t>(event.step));
        hash_value(static_cast<std::int64_t>(event.player));
        hash_value(static_cast<std::int64_t>(event.kind));
        hash_value(event.action);
        hash_value(static_cast<std::int64_t>(reason));
        hash_value(0);
        return result;
    };

    if (event.action >= table_->actions.size()) {
        return reject(Reject::UnknownAction);
    }
    const ActionDef& def = table_->actions[event.action];
    const auto requester = static_cast<std::size_t>(event.player);
    if (state_.present[requester] == 0) {
        return reject(Reject::WrongPlayer);
    }

    // Quién hace cada rama.
    std::array<std::uint8_t, kMaxBranches> who = {};
    const bool parallel = def.branch_count == 2 && state_.present[0] != 0 && state_.present[1] != 0;
    if (def.branch_count == 1 || !parallel) {
        // Una sola rama, o una cooperativa con un solo jugador (que hace todas las ramas, una
        // tras otra, sea cual sea su rol): solo la rama única comprueba el rol.
        for (int i = 0; i < def.branch_count; ++i) {
            if (def.branch_count == 1 &&
                !role_allows(def.branches[static_cast<std::size_t>(i)].role, event.player)) {
                return reject(Reject::WrongPlayer);
            }
            who[static_cast<std::size_t>(i)] = static_cast<std::uint8_t>(event.player);
        }
    } else {
        // Las ramas con rol fijo van a su jugador; las libres, al pedidor y luego al otro.
        const auto other = static_cast<std::uint8_t>(1 - requester);
        bool taken[kPlayers] = {false, false};
        for (int i = 0; i < 2; ++i) {
            const Role role = def.branches[static_cast<std::size_t>(i)].role;
            if (role != Role::Any) {
                const auto p = static_cast<std::uint8_t>(role);
                if (taken[p]) {
                    return reject(Reject::WrongPlayer);
                }
                taken[p] = true;
                who[static_cast<std::size_t>(i)] = p;
            }
        }
        for (int i = 0; i < 2; ++i) {
            if (def.branches[static_cast<std::size_t>(i)].role != Role::Any) {
                continue;
            }
            const auto p = !taken[requester] ? static_cast<std::uint8_t>(requester) : other;
            if (taken[p]) {
                return reject(Reject::WrongPlayer);
            }
            taken[p] = true;
            who[static_cast<std::size_t>(i)] = p;
        }
        if (!taken[requester]) {
            return reject(Reject::WrongPlayer); // el pedidor no participa
        }
    }

    // Requisitos.
    const bool stowed = state_.tablet_stowed != 0;
    if ((def.tablet_requires == TabletReq::Stowed && !stowed) ||
        (def.tablet_requires == TabletReq::Out && stowed)) {
        return reject(Reject::TabletState);
    }
    if (def.car == CarReq::Stopped && state_.car_stopped == 0) {
        return reject(Reject::CarMoving);
    }
    for (int i = 0; i < def.branch_count; ++i) {
        const auto p = who[static_cast<std::size_t>(i)];
        if (!position_allows(def.branches[static_cast<std::size_t>(i)].position,
                             static_cast<Position>(state_.position[p]))) {
            return reject(Reject::WrongPosition);
        }
    }

    // Manos: en paralelo, cada una las suyas; en serie, el jugador retiene el máximo.
    std::array<std::uint8_t, kMaxBranches> hold = {};
    for (int i = 0; i < def.branch_count; ++i) {
        hold[static_cast<std::size_t>(i)] = def.branches[static_cast<std::size_t>(i)].hands;
    }
    if (!parallel && def.branch_count == 2) {
        hold[0] = std::max(hold[0], hold[1]);
        hold[1] = 0;
    }
    std::array<int, kPlayers> need = {0, 0};
    for (int i = 0; i < def.branch_count; ++i) {
        need[who[static_cast<std::size_t>(i)]] += hold[static_cast<std::size_t>(i)];
    }
    for (int p = 0; p < kPlayers; ++p) {
        if (need[static_cast<std::size_t>(p)] > state_.hands_free[static_cast<std::size_t>(p)]) {
            return reject(Reject::NoHands);
        }
    }

    ActiveState* slot = nullptr;
    for (ActiveState& a : state_.active) {
        if (a.used == 0) {
            slot = &a;
            break;
        }
    }
    if (slot == nullptr) {
        return reject(Reject::NoSlot);
    }

    // Aceptada: se fijan las fechas de todas las fases.
    ActiveState active;
    active.used = 1;
    active.action = event.action;
    active.branch_count = def.branch_count;
    active.serial = state_.next_serial++;
    active.start = state_.time;
    std::int64_t cursor = state_.time;
    std::int64_t end = state_.time;
    std::int64_t effect_step = state_.time;
    for (int i = 0; i < def.branch_count; ++i) {
        const BranchDef& bd = def.branches[static_cast<std::size_t>(i)];
        BranchState& b = active.branches[static_cast<std::size_t>(i)];
        b.player = who[static_cast<std::size_t>(i)];
        b.phase = static_cast<std::uint8_t>(Phase::Waiting);
        b.start = parallel || i == 0 ? state_.time : cursor;
        b.prepare_end = b.start + bd.steps[0];
        b.execute_end = b.prepare_end + bd.steps[1];
        b.collect_end = b.execute_end + bd.steps[2];
        b.hands = hold[static_cast<std::size_t>(i)];
        cursor = b.collect_end;
        end = std::max(end, b.collect_end);
        effect_step = std::max(effect_step, b.execute_end);
        state_.hands_free[b.player] =
            static_cast<std::uint8_t>(state_.hands_free[b.player] - b.hands);
    }
    active.end = end;
    active.effect_step = effect_step;
    *slot = active;

    hash_value(1);
    hash_value(static_cast<std::int64_t>(event.step));
    hash_value(static_cast<std::int64_t>(event.player));
    hash_value(static_cast<std::int64_t>(event.kind));
    hash_value(event.action);
    hash_value(static_cast<std::int64_t>(Reject::Ok));
    hash_value(active.serial);

    result.serial = active.serial;
    result.start = active.start;
    result.end = active.end;
    result.branch_count = active.branch_count;
    for (int i = 0; i < active.branch_count; ++i) {
        const BranchState& b = active.branches[static_cast<std::size_t>(i)];
        result.branches[static_cast<std::size_t>(i)] = {
            static_cast<Player>(b.player), b.start, b.prepare_end, b.execute_end, b.collect_end};
    }
    settle(*slot, true);
    return result;
}

ApplyResult ActionMachine::cancel(const ActionEvent& event) {
    ApplyResult result;
    hash_value(1);
    hash_value(static_cast<std::int64_t>(event.step));
    hash_value(static_cast<std::int64_t>(event.player));
    hash_value(static_cast<std::int64_t>(event.kind));
    hash_value(event.action);
    // Si hay varias instancias suyas de esa acción, la de menor serie.
    ActiveState* found = nullptr;
    for (ActiveState& a : state_.active) {
        if (a.used == 0 || a.action != event.action) {
            continue;
        }
        bool mine = false;
        for (int i = 0; i < a.branch_count; ++i) {
            mine = mine || a.branches[static_cast<std::size_t>(i)].player ==
                               static_cast<std::uint8_t>(event.player);
        }
        if (mine && (found == nullptr || a.serial < found->serial)) {
            found = &a;
        }
    }
    if (found != nullptr) {
        hash_value(static_cast<std::int64_t>(Reject::Ok));
        hash_value(found->serial);
        result.serial = found->serial;
        result.start = found->start;
        result.end = found->end;
        result.lost_steps = lost_steps_of(*found);
        cancel_active(*found, CancelReason::Player);
        return result;
    }
    result.reject = Reject::NothingToCancel;
    hash_value(static_cast<std::int64_t>(Reject::NothingToCancel));
    hash_value(0);
    return result;
}

void ActionMachine::release(ActiveState& active) {
    for (int i = 0; i < active.branch_count; ++i) {
        const BranchState& b = active.branches[static_cast<std::size_t>(i)];
        state_.hands_free[b.player] =
            static_cast<std::uint8_t>(state_.hands_free[b.player] + b.hands);
    }
    active.used = 0;
}

// Pasos avanzados que se pierden si se cancela ahora: 0 mientras todas las ramas preparan o
// esperan, y después de aplicado el efecto; si no, los transcurridos desde el inicio.
std::int64_t ActionMachine::lost_steps_of(const ActiveState& active) const {
    for (int i = 0; i < active.branch_count; ++i) {
        const auto phase = static_cast<Phase>(active.branches[static_cast<std::size_t>(i)].phase);
        if (phase != Phase::Waiting && phase != Phase::Prepare) {
            return active.effect_applied != 0 ? 0 : state_.time - active.start;
        }
    }
    return 0;
}

void ActionMachine::cancel_active(ActiveState& active, CancelReason reason) {
    Transition t;
    t.step = state_.time;
    t.kind = TransitionKind::Cancelled;
    t.serial = active.serial;
    t.action = active.action;
    t.reason = reason;
    t.player = static_cast<Player>(active.branches[0].player);
    t.lost_steps = lost_steps_of(active);
    emit(t);
    release(active);
}

void ActionMachine::settle(ActiveState& active, bool /*initial*/) {
    const std::int64_t t = state_.time;
    const ActionDef& def = table_->actions[active.action];
    for (int i = 0; i < active.branch_count; ++i) {
        BranchState& b = active.branches[static_cast<std::size_t>(i)];
        const Phase now = phase_at(b, t);
        if (static_cast<std::uint8_t>(now) == b.phase) {
            continue;
        }
        b.phase = static_cast<std::uint8_t>(now);
        if (now == Phase::Prepare || now == Phase::Execute || now == Phase::Collect) {
            Transition tr;
            tr.step = t;
            tr.kind = TransitionKind::PhaseStart;
            tr.serial = active.serial;
            tr.action = active.action;
            tr.branch = static_cast<std::uint8_t>(i);
            tr.player = static_cast<Player>(b.player);
            tr.phase = now;
            emit(tr);
        }
    }
    if (active.effect_applied == 0 && t >= active.effect_step) {
        active.effect_applied = 1;
        if (def.tablet_result == TabletResult::Stowed) {
            state_.tablet_stowed = 1;
        } else if (def.tablet_result == TabletResult::Out) {
            state_.tablet_stowed = 0;
        }
        Transition tr;
        tr.step = t;
        tr.kind = TransitionKind::Effect;
        tr.serial = active.serial;
        tr.action = active.action;
        tr.player = static_cast<Player>(active.branches[0].player);
        emit(tr);
    }
    if (t >= active.end) {
        Transition tr;
        tr.step = t;
        tr.kind = TransitionKind::Finished;
        tr.serial = active.serial;
        tr.action = active.action;
        tr.player = static_cast<Player>(active.branches[0].player);
        emit(tr);
        release(active);
    }
}

void ActionMachine::advance(const CarState& car) {
    ++state_.time;
    state_.car_stopped = car.stopped ? 1 : 0;
    for (ActiveState& a : state_.active) {
        if (a.used != 0) {
            settle(a, false);
        }
    }
    // Requisitos que dejan de cumplirse: la acción se cancela sola en este paso.
    for (ActiveState& a : state_.active) {
        if (a.used == 0) {
            continue;
        }
        const ActionDef& def = table_->actions[a.action];
        if (def.car == CarReq::Stopped && !car.stopped) {
            cancel_active(a, CancelReason::CarMoving);
            continue;
        }
        // La posición solo se comprueba al pedir: moverse (salir del coche) es [post-1.0].
    }
}

} // namespace rally::game
