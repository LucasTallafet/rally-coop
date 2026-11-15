#include "platform/notes_host.h"

#include <utility>

namespace rally::platform {

void NotesSession::start_stage(double stage_length, std::vector<game::DirectionChange> changes,
                               std::string stage_name, std::string log_path) {
    close();
    queue_.reset(stage_length);
    changes_ = std::move(changes);
    stage_name_ = std::move(stage_name);
    log_path_ = std::move(log_path);
    stage_ = true;
}

void NotesSession::start_without_stage() {
    close();
    queue_.reset(0.0);
    changes_.clear();
    stage_ = false;
}

void NotesSession::open_log() {
    std::string error;
    if (!log_.open(log_path_, io::notes_log_header(stage_name_), error)) {
        error_ = "registro de notas: " + error;
    }
}

int NotesSession::pump(net::Host& host, std::uint64_t step, double car_s) {
    int applied = 0;
    game::NoteOrder order;
    while (host.take_note_order(order)) {
        if (!stage_) {
            continue;
        }
        const game::NoteApplied result = queue_.apply(order, car_s);
        if (!log_.is_open() && error_.empty()) {
            open_log();
        }
        if (log_.is_open()) {
            log_.append(step, car_s, order, result, changes_);
        }
        ++applied;
    }
    host.publish_notes(queue_);
    return applied;
}

std::string NotesSession::take_error() { return std::exchange(error_, std::string()); }

void NotesSession::close() {
    if (log_.is_open()) {
        std::string error;
        if (!log_.close(error)) {
            error_ = "registro de notas: " + error;
        }
    }
}

} // namespace rally::platform
