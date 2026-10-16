#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rally::sim {
class SimWorld;
}

namespace rally::game {

// Máquina de acciones (01 §8). Toda acción tiene fases de preparar, ejecutar y recoger, cada una
// con su duración en datos (data/game/actions.txt). La duración es la regla; hasta que haya
// animaciones la llena un anillo de progreso.
//
// Es estado del juego, no de la simulación: no entra en el SaveState de Jolt ni en
// SimWorld::state_hash, lee la simulación (CarState) y nunca la escribe. Cuenta pasos, nunca
// lee el reloj.
//
// El tiempo de la máquina, now(), es el número de paso «al principio del paso»: el siguiente
// que se va a simular. En cada paso s:
//   1. se aplican los eventos con step == s, en el orden en que llegaron (apply);
//   2. se simula el paso s (SimWorld::step);
//   3. advance(CarState) con el estado del coche ya observado: now() pasa a s + 1.
// Origen: now() = 0 al construir la máquina, que es el paso 0 de la grabación. El paso de un
// evento sale de now(), no de FrameSteps::first (que cuenta desde que arranca el programa y no
// vuelve a 0 al reconstruir el mundo). Reconstruir el mundo (teclas 1/2/3) empieza una grabación
// nueva y, con ella, una máquina nueva. apply() comprueba los requisitos con el último CarState
// recibido en advance (el del principio del paso); antes del primero, con el de MachineSetup.
// Todos los pasos de la línea de tiempo (inicio, cambios de fase, fin, cancelaciones) son
// valores de now(): «cancelada en N» es que, al llegar now() a N, ya no se cumplía.

inline constexpr int kStepsPerSecond = 240;
inline constexpr int kMaxActions = 16; // acciones de una tabla
inline constexpr int kMaxBranches = 2; // ramas de una acción (cooperativa: 2)
inline constexpr int kMaxActive = 8;   // acciones activas a la vez
inline constexpr int kPlayers = 2;
inline constexpr std::int64_t kMaxPhaseMs = 600'000; // 10 min por fase

enum class Player : std::uint8_t { Driver = 0, Copilot = 1 };
enum class Role : std::uint8_t { Driver = 0, Copilot = 1, Any = 2 };
enum class Position : std::uint8_t { DriverSeat = 0, CopilotSeat = 1, Outside = 2 };
// Posición que pide una rama: un asiento o cualquiera.
enum class PositionReq : std::uint8_t { DriverSeat = 0, CopilotSeat = 1, Any = 2 };
enum class CarReq : std::uint8_t { Any = 0, Stopped = 1 };
enum class TabletReq : std::uint8_t { Any = 0, Stowed = 1, Out = 2 };
enum class TabletResult : std::uint8_t { Unchanged = 0, Stowed = 1, Out = 2 };
enum class Phase : std::uint8_t { Waiting = 0, Prepare = 1, Execute = 2, Collect = 3, Done = 4 };

// Pasos de una duración en milisegundos: (ms × 240 + 500) / 1000, en enteros. 1 ms son 0 pasos
// y una fase de 0 pasos se salta.
constexpr std::int64_t ms_to_steps(std::int64_t ms) { return (ms * kStepsPerSecond + 500) / 1000; }

struct BranchDef {
    Role role = Role::Any;
    std::uint8_t hands = 0; // 0..2
    PositionReq position = PositionReq::Any;
    std::int32_t ms[3] = {0, 0, 0}; // preparar, ejecutar, recoger
    std::int32_t steps[3] = {0, 0, 0};
};

struct ActionDef {
    std::string name;
    CarReq car = CarReq::Any;
    TabletReq tablet_requires = TabletReq::Any;
    TabletResult tablet_result = TabletResult::Unchanged;
    std::uint8_t branch_count = 0;
    std::array<BranchDef, kMaxBranches> branches{};
};

// Tabla de acciones cargada de texto. La acción es su índice (el que viaja en la grabación y en
// la red).
struct ActionTable {
    std::vector<ActionDef> actions;
    std::uint64_t text_hash = 0; // FNV-1a del texto: una máquina solo restaura con su tabla

    // Índice de una acción por nombre, o -1.
    int find(std::string_view name) const;
};

struct ActionParseResult {
    bool ok = false;
    int line = 0;        // línea (desde 1) del primer error, 0 si ok
    std::string message; // motivo, sin la línea
    ActionTable table;
};

// Carga el texto de las acciones (data/game/actions.txt, o la cabecera de una grabación). Formato
// `clave = valor`, `#` comentarios; `action = nombre` abre una acción (car, tablet_requires,
// tablet_result) y `branch = driver|copilot|any` abre una rama (hands, position, prepare_ms,
// execute_ms, collect_ms). Todas las claves son obligatorias. Una clave desconocida, repetida,
// o que falta, o una duración negativa, devuelven el error con el número de línea. Reserva
// memoria: se llama al cargar, nunca dentro del paso.
ActionParseResult parse_actions(std::string_view text);

// Lo mismo, y aborta con RALLY_CHECK («línea N: motivo») si el texto no es válido.
ActionTable parse_actions_or_die(std::string_view text, const char* origin);

// Lo que la máquina lee del coche en cada paso.
struct CarState {
    bool stopped = true; // velocidad horizontal < 2 km/h
    friend bool operator==(const CarState&, const CarState&) = default;
};
CarState car_state_from(const sim::SimWorld& world);

enum class EventKind : std::uint8_t { Request = 0, Cancel = 1 };

// Un evento con el paso en que se aplica (el siguiente que se va a simular).
struct ActionEvent {
    std::uint64_t step = 0; // el mismo tipo que Recorder::steps()
    Player player = Player::Driver;
    EventKind kind = EventKind::Request;
    std::uint8_t action = 0; // índice en la tabla
    friend bool operator==(const ActionEvent&, const ActionEvent&) = default;
};

enum class Reject : std::uint8_t {
    Ok = 0,
    UnknownAction,   // índice fuera de la tabla
    WrongPlayer,     // ausente, o sin rama que le corresponda
    TabletState,     // la tablet no está como pide la acción
    CarMoving,       // pide el coche parado
    WrongPosition,   // no está en el asiento que pide
    NoHands,         // menos manos libres de las que pide
    NoSlot,          // demasiadas acciones activas
    NothingToCancel, // (cancelación) no hay una acción suya de ese tipo
};

enum class CancelReason : std::uint8_t { Player = 0, CarMoving };

// Pasos absolutos de una rama: cuándo acaba cada fase.
struct BranchPlan {
    Player player = Player::Driver;
    std::int64_t start = 0;
    std::int64_t prepare_end = 0;
    std::int64_t execute_end = 0;
    std::int64_t collect_end = 0; // fin de la rama
};

// Resultado de aplicar un evento. En una petición aceptada lleva el paso de inicio y el de fin
// de cada fase: es lo que el host confirmará en la red.
struct ApplyResult {
    Reject reject = Reject::Ok;
    std::uint32_t serial = 0; // identificador de la instancia (0 si rechazada)
    std::int64_t start = 0;
    std::int64_t end = 0; // primer paso en que ya no está activa
    std::uint8_t branch_count = 0;
    std::array<BranchPlan, kMaxBranches> branches{};
    std::int64_t lost_steps = 0; // (cancelación) pasos avanzados que se pierden
    bool ok() const { return reject == Reject::Ok; }
};

enum class TransitionKind : std::uint8_t {
    PhaseStart = 0, // una rama entra en una fase (Prepare, Execute o Collect)
    Effect,         // se aplica el efecto sobre la tablet
    Finished,
    Cancelled,
};

struct Transition {
    std::int64_t step = 0;
    TransitionKind kind = TransitionKind::PhaseStart;
    std::uint32_t serial = 0;
    std::uint8_t action = 0;
    std::uint8_t branch = 0;
    Player player = Player::Driver;
    Phase phase = Phase::Waiting;               // PhaseStart
    CancelReason reason = CancelReason::Player; // Cancelled
    std::int64_t lost_steps = 0;                // Cancelled
};

// Lo que ve la plataforma: mensajes y anillo. No es estado de la máquina. on_transition se llama
// dentro del bucle de pasos: no puede reservar memoria.
class ActionObserver {
public:
    virtual void on_transition(const Transition& transition) = 0;

protected:
    ~ActionObserver() = default;
};

// Progreso de la primera acción activa de un jugador, para el anillo.
struct ActionProgress {
    bool active = false;
    std::uint8_t action = 0;
    Phase phase = Phase::Waiting;
    std::int64_t elapsed = 0; // pasos desde el inicio
    std::int64_t total = 0;   // pasos de inicio a fin
};

// Instalación inicial de la máquina: dónde está cada jugador y quién juega. En la 1.0 nadie
// cambia de sitio durante la partida, y el formato 7 del .rrec no registra ni las posiciones ni
// quién está presente: la plataforma y io::replay tienen que construir la máquina igual. El valor
// por defecto es el de la 1.0 (los dos jugadores, cada uno en su asiento, coche quieto).
struct MachineSetup {
    CarState car;
    std::array<Position, kPlayers> position = {Position::DriverSeat, Position::CopilotSeat};
    std::array<bool, kPlayers> present = {true, true};
};

// Estado completo de la máquina (el save()). POD: viaja con el estado del juego en el cambio de
// host. Cada campo nuevo tiene su test en tests/game/test_actions.cpp.
struct BranchState {
    std::uint8_t player = 0;
    std::uint8_t phase = 0; // Phase
    std::int64_t start = 0;
    std::int64_t prepare_end = 0;
    std::int64_t execute_end = 0;
    std::int64_t collect_end = 0;
    std::uint8_t hands = 0;
    friend bool operator==(const BranchState&, const BranchState&) = default;
};

struct ActiveState {
    std::uint8_t used = 0;
    std::uint8_t action = 0;
    std::uint8_t effect_applied = 0;
    std::uint8_t branch_count = 0;
    std::uint32_t serial = 0;
    std::int64_t start = 0;
    std::int64_t end = 0;
    std::int64_t effect_step = 0;
    std::array<BranchState, kMaxBranches> branches{};
    friend bool operator==(const ActiveState&, const ActiveState&) = default;
};

struct ActionSnapshot {
    std::int64_t time = 0;
    std::uint64_t table_hash = 0;
    std::uint64_t timeline_hash = 0;
    std::uint32_t next_serial = 1;
    std::uint8_t car_stopped = 1;
    std::uint8_t tablet_stowed = 0;
    std::array<std::uint8_t, kPlayers> position = {};
    std::array<std::uint8_t, kPlayers> present = {};
    std::array<std::uint8_t, kPlayers> hands_free = {};
    std::array<ActiveState, kMaxActive> active = {};
    friend bool operator==(const ActionSnapshot&, const ActionSnapshot&) = default;
};

class ActionMachine {
public:
    // La tabla tiene que sobrevivir a la máquina.
    explicit ActionMachine(const ActionTable& table, const MachineSetup& setup = {});

    // Aplica un evento en el paso actual (event.step == now(), si no, RALLY_CHECK). No reserva.
    ApplyResult apply(const ActionEvent& event);

    // Avanza un paso, con el estado del coche en el paso al que se llega. Comprueba los
    // requisitos de las acciones activas. No reserva.
    void advance(const CarState& car);

    std::int64_t now() const { return state_.time; }
    bool tablet_stowed() const { return state_.tablet_stowed != 0; }
    std::uint8_t hands_free(Player player) const;
    int active_count() const;
    ActionProgress progress(Player player) const;
    // FNV-1a de 64 bits (desplazamiento y primo estándar) sobre una secuencia de enteros de 64 bits
    // en little-endian: por cada evento aplicado (1, paso, jugador, tipo, acción, motivo del
    // rechazo, serie) y por cada cambio (2, paso, tipo, serie, acción, rama, jugador, fase, motivo,
    // pasos perdidos). Incluye el motivo y los pasos perdidos de cada cancelación.
    std::uint64_t timeline_hash() const { return state_.timeline_hash; }

    ActionSnapshot save() const { return state_; }
    // Restaura un save() hecho con la misma tabla. false, sin tocar nada, si es de otra tabla:
    // el llamador decide (RALLY_CHECK).
    [[nodiscard]] bool restore(const ActionSnapshot& snapshot);

    void set_observer(ActionObserver* observer) { observer_ = observer; }

private:
    ApplyResult request(const ActionEvent& event);
    ApplyResult cancel(const ActionEvent& event);
    void settle(ActiveState& active, bool initial);
    void cancel_active(ActiveState& active, CancelReason reason);
    void release(ActiveState& active);
    std::int64_t lost_steps_of(const ActiveState& active) const;
    void emit(const Transition& transition);
    void hash_value(std::int64_t value);

    const ActionTable* table_;
    ActionSnapshot state_;
    ActionObserver* observer_ = nullptr;
};

} // namespace rally::game
