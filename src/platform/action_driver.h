#pragma once

#include "game/actions.h"
#include "io/recording.h"
#include "sim/sim_world.h"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>

namespace rally::platform {

// El pegamento entre la máquina de acciones (src/game), la grabación (src/io) y las teclas de la
// plataforma (tarea 2.9, parte P). Sin raylib ni SDL: se prueba con la repetición real.
//
// Sigue el contrato de la grabación de formato 7 (docs/progreso/grabacion-v7.md):
//   - start() justo tras build() y seal(): máquina nueva con MachineSetup{} y
//     setup.car = car_state_from(mundo). Reconstruir el mundo empieza otra máquina y otra
//     grabación a la vez.
//   - submit() al principio del fotograma, antes de los pasos: el evento lleva el paso que se va
//     a simular (now() de la máquina, que es Recorder::steps()), se graba con append_event
//     (ensure_capacity antes, fuera del paso) y se aplica a la máquina con el mismo evento. Se
//     graban también los rechazados: la repetición los aplica igual y el hash de la línea de
//     tiempo los incluye.
//   - advance() tras cada SimWorld::step.
//   - timeline_hash() para Recorder::finish (0 sin acciones).
// La máquina guarda punteros a la tabla y al observador: el ActionDriver no se copia ni se mueve.
class ActionDriver {
public:
    // Transiciones sin sacar (drain) que caben; con más, se descartan las más viejas.
    static constexpr std::size_t kPendingTransitions = 32;

    ActionDriver() = default;
    ActionDriver(const ActionDriver&) = delete;
    ActionDriver& operator=(const ActionDriver&) = delete;

    // Carga el texto de data/game/actions.txt (aborta con la línea si no vale). Vacío: sin
    // acciones. Descarta la máquina: hay que llamar a start() de nuevo.
    void load(std::string_view text);
    bool enabled() const { return !table_.actions.empty(); }
    const std::string& text() const { return text_; }
    const game::ActionTable& table() const { return table_; }

    // Máquina nueva para el mundo recién construido; vacía las transiciones pendientes.
    void start(const sim::SimWorld& world);

    // Aplica y graba un evento en el paso actual. Sin acciones o sin máquina: Reject::UnknownAction
    // y no graba nada. `recorder` nulo: no graba (--golden).
    game::ApplyResult submit(io::Recorder* recorder, game::Player player, game::EventKind kind,
                             std::uint8_t action);
    // Tras cada paso simulado, con el mundo ya observado. No reserva memoria.
    void advance(const sim::SimWorld& world);

    std::int64_t now() const { return machine_ ? machine_->now() : 0; }
    std::uint64_t timeline_hash() const { return machine_ ? machine_->timeline_hash() : 0; }
    bool tablet_stowed() const { return machine_ && machine_->tablet_stowed(); }
    // Progreso de la acción activa del copiloto (la única que hay en la 1.0 hasta la tablet).
    game::ActionProgress copilot_progress() const;

    // Saca hasta out.size() transiciones pendientes, de la más vieja a la más nueva.
    std::size_t drain(std::span<game::Transition> out);
    // Transiciones descartadas por no haberse sacado a tiempo (desde el último start).
    std::uint64_t dropped() const { return sink_.dropped; }

private:
    class Sink final : public game::ActionObserver {
    public:
        void on_transition(const game::Transition& transition) override;
        void clear();
        std::array<game::Transition, kPendingTransitions> ring{};
        std::size_t first = 0;
        std::size_t count = 0;
        std::uint64_t dropped = 0;
    };

    std::string text_;
    game::ActionTable table_;
    std::optional<game::ActionMachine> machine_;
    Sink sink_;
};

// El anillo de progreso: fracción de la acción entera ya transcurrida, en [0, 1].
struct RingState {
    bool visible = false;
    float fraction = 0.0f;
    std::uint8_t action = 0;
    game::Phase phase = game::Phase::Waiting;
};
RingState ring_state(const game::ActionProgress& progress);

// Textos para la pantalla (español, sin acentos raros fuera de la fuente por defecto).
// Motivo de un rechazo; `def` puede ser nulo (acción desconocida).
std::string reject_text(game::Reject reject, const game::ActionDef* def);
// «preparar» / «ejecutar» / «recoger» (vacío fuera de esas fases).
std::string_view phase_name(game::Phase phase);
// Pasos como segundos con coma: 192 -> «0,80 s».
std::string duration_text(std::int64_t steps);
// Mensaje de un cambio de la máquina; vacío si no hay que decir nada (cambios de fase y fin).
std::string transition_text(const game::Transition& transition, const game::ActionTable& table);

} // namespace rally::platform
