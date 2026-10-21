#include "platform/action_driver.h"

#include <algorithm>
#include <string>

namespace rally::platform {

void ActionDriver::Sink::on_transition(const game::Transition& transition) {
    // Sin reservas: un anillo fijo. Con más pendientes de las que caben, se pierde la más vieja.
    if (count == ring.size()) {
        first = (first + 1) % ring.size();
        --count;
        ++dropped;
    }
    ring[(first + count) % ring.size()] = transition;
    ++count;
}

void ActionDriver::Sink::clear() {
    first = 0;
    count = 0;
    dropped = 0;
}

void ActionDriver::load(std::string_view text) {
    machine_.reset();
    sink_.clear();
    text_ = std::string(text);
    table_ = text.empty() ? game::ActionTable{}
                          : game::parse_actions_or_die(text, "data/game/actions.txt");
}

void ActionDriver::start(const sim::SimWorld& world) {
    machine_.reset();
    sink_.clear();
    if (!enabled()) {
        return;
    }
    game::MachineSetup setup;
    setup.car = game::car_state_from(world);
    machine_.emplace(table_, setup);
    machine_->set_observer(&sink_);
}

game::ApplyResult ActionDriver::submit(io::Recorder* recorder, game::Player player,
                                       game::EventKind kind, std::uint8_t action) {
    if (!machine_) {
        game::ApplyResult none;
        none.reject = game::Reject::UnknownAction;
        return none;
    }
    const game::ActionEvent event{static_cast<std::uint64_t>(machine_->now()), player, kind,
                                  action};
    if (recorder != nullptr) {
        recorder->ensure_capacity(1);
        recorder->append_event(event);
    }
    return machine_->apply(event);
}

void ActionDriver::advance(const sim::SimWorld& world) {
    if (machine_) {
        machine_->advance(game::car_state_from(world));
    }
}

game::ActionProgress ActionDriver::copilot_progress() const {
    return machine_ ? machine_->progress(game::Player::Copilot) : game::ActionProgress{};
}

std::size_t ActionDriver::drain(std::span<game::Transition> out) {
    std::size_t n = 0;
    while (n < out.size() && sink_.count > 0) {
        out[n++] = sink_.ring[sink_.first];
        sink_.first = (sink_.first + 1) % sink_.ring.size();
        --sink_.count;
    }
    return n;
}

RingState ring_state(const game::ActionProgress& progress) {
    RingState ring;
    if (!progress.active || progress.total <= 0) {
        return ring;
    }
    ring.visible = true;
    ring.fraction = std::clamp(
        static_cast<float>(progress.elapsed) / static_cast<float>(progress.total), 0.0f, 1.0f);
    ring.action = progress.action;
    ring.phase = progress.phase;
    return ring;
}

std::string reject_text(game::Reject reject, const game::ActionDef* def) {
    switch (reject) {
    case game::Reject::Ok:
        return {};
    case game::Reject::UnknownAction:
        return "acción desconocida";
    case game::Reject::WrongPlayer:
        return "ese jugador no puede hacer esta acción";
    case game::Reject::TabletState:
        if (def != nullptr && def->tablet_requires == game::TabletReq::Stowed) {
            return "la tablet no está guardada";
        }
        if (def != nullptr && def->tablet_requires == game::TabletReq::Out) {
            return "la tablet ya está guardada";
        }
        return "la tablet no está como pide la acción";
    case game::Reject::CarMoving:
        return "el coche tiene que estar parado";
    case game::Reject::WrongPosition:
        return "no está en su asiento";
    case game::Reject::NoHands:
        return "las manos están ocupadas";
    case game::Reject::NoSlot:
        return "demasiadas acciones a la vez";
    case game::Reject::NothingToCancel:
        return "no hay nada que cancelar";
    }
    return {};
}

std::string_view phase_name(game::Phase phase) {
    switch (phase) {
    case game::Phase::Prepare:
        return "preparar";
    case game::Phase::Execute:
        return "ejecutar";
    case game::Phase::Collect:
        return "recoger";
    case game::Phase::Waiting:
    case game::Phase::Done:
        break;
    }
    return {};
}

std::string duration_text(std::int64_t steps) {
    const std::int64_t hundredths =
        (steps * 100 + game::kStepsPerSecond / 2) / game::kStepsPerSecond;
    std::string out = std::to_string(hundredths / 100) + ",";
    out += static_cast<char>('0' + (hundredths % 100) / 10);
    out += static_cast<char>('0' + hundredths % 10);
    return out + " s";
}

std::string transition_text(const game::Transition& transition, const game::ActionTable& table) {
    switch (transition.kind) {
    case game::TransitionKind::Effect: {
        if (transition.action >= table.actions.size()) {
            return {};
        }
        const game::TabletResult result = table.actions[transition.action].tablet_result;
        return result == game::TabletResult::Stowed ? "tablet guardada"
               : result == game::TabletResult::Out  ? "tablet fuera"
                                                    : std::string{};
    }
    case game::TransitionKind::Cancelled: {
        if (transition.reason == game::CancelReason::CarMoving) {
            return "acción cancelada: el coche se ha movido";
        }
        if (transition.lost_steps > 0) {
            return "acción cancelada: se pierden " + duration_text(transition.lost_steps);
        }
        return "acción cancelada (sin coste)";
    }
    case game::TransitionKind::PhaseStart:
    case game::TransitionKind::Finished:
        break;
    }
    return {};
}

} // namespace rally::platform
