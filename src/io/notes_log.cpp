#include "io/notes_log.h"

#include <charconv>
#include <filesystem>
#include <system_error>

namespace rally::io {

namespace {

// Tercera copia de estas tres líneas (recording.cpp y telemetry_csv.cpp tienen la suya): se
// acepta para no tocar esos archivos.
std::filesystem::path to_path(const std::string& utf8) {
    return std::filesystem::path(
        std::u8string(reinterpret_cast<const char8_t*>(utf8.data()), utf8.size()));
}

void append_real(double value, std::string& out) {
    char text[64];
    const std::to_chars_result r =
        std::to_chars(text, text + sizeof(text), value, std::chars_format::fixed, 3);
    const char* begin = text;
    if (text[0] == '-') {
        bool zero = true;
        for (const char* p = text + 1; p != r.ptr; ++p) {
            if (*p != '0' && *p != '.') {
                zero = false;
                break;
            }
        }
        if (zero) {
            ++begin; // un valor que redondea a 0 se escribe sin signo
        }
    }
    out.append(begin, static_cast<std::size_t>(r.ptr - begin));
}

void append_uint(std::uint64_t value, std::string& out) {
    char text[32];
    const std::to_chars_result r = std::to_chars(text, text + sizeof(text), value);
    out.append(text, r.ptr);
}

const char* order_text(game::NoteOrderKind kind) {
    switch (kind) {
    case game::NoteOrderKind::Corner:
        return "curva";
    case game::NoteOrderKind::Hazard:
        return "peligro";
    case game::NoteOrderKind::Mark:
        return "modificador";
    case game::NoteOrderKind::Distance:
        return "distancia";
    case game::NoteOrderKind::EraseLast:
        return "borrar";
    case game::NoteOrderKind::ClearPending:
        return "vaciar";
    }
    return "";
}

const char* result_text(game::NoteResult result) {
    switch (result) {
    case game::NoteResult::Applied:
        return "ok";
    case game::NoteResult::NoNote:
        return "sin nota";
    case game::NoteResult::Full:
        return "cola llena";
    case game::NoteResult::Invalid:
        return "no valida";
    }
    return "";
}

const char* source_text(game::DistanceSource source) {
    switch (source) {
    case game::DistanceSource::None:
        return "";
    case game::DistanceSource::Wheel:
        return "rueda";
    case game::DistanceSource::Counter:
        return "contador";
    }
    return "";
}

} // namespace

std::string notes_log_header(std::string_view stage_name) {
    std::string out = "\xEF\xBB\xBF# rally notes\n# format=";
    out += std::to_string(kNotesLogFormatVersion);
    out += "\n# stage=";
    out += stage_name;
    out += "\nstep,order,result,note_id,anchor_s,note,distance,distance_source,real_distance_m,"
           "car_s,erased\n";
    return out;
}

void append_notes_log_row(std::uint64_t step, double car_s, const game::NoteOrder& order,
                          const game::NoteApplied& applied,
                          const std::vector<game::DirectionChange>& changes, std::string& out) {
    // La nota de la fila: la afectada; si una curva o un peligro se rechazó, la que habría sido.
    game::Note note;
    bool has_note = applied.has_note;
    if (has_note) {
        note = applied.note;
    } else if (applied.result != game::NoteResult::Applied &&
               (order.kind == game::NoteOrderKind::Corner ||
                order.kind == game::NoteOrderKind::Hazard) &&
               (order.s - order.s) == 0.0) {
        has_note = true;
        note.s = order.s;
        if (order.kind == game::NoteOrderKind::Corner) {
            note.grade = order.grade;
            note.side = order.side;
        } else {
            note.marks = game::mark_bit(order.mark);
        }
    }
    const game::NoteDistance& distance =
        order.kind == game::NoteOrderKind::Distance ? order.distance : note.distance;

    append_uint(step, out);
    out += ',';
    out += order_text(order.kind);
    out += ',';
    out += result_text(applied.result);
    out += ',';
    if (applied.has_note) {
        append_uint(note.id, out);
    }
    out += ',';
    double real = 0.0;
    if (has_note) {
        append_real(note.s, out);
    }
    out += ',';
    if (has_note) {
        out += game::note_call_text(note);
    }
    out += ',';
    out += game::distance_text(distance);
    out += ',';
    out += source_text(distance.source);
    out += ',';
    if (has_note && game::real_distance_after(changes, note.s, real)) {
        append_real(real, out);
    }
    out += ',';
    append_real(car_s, out);
    out += ',';
    append_uint(static_cast<std::uint64_t>(applied.erased), out);
    out += '\n';
}

bool NotesLog::open(const std::string& path, std::string_view header, std::string& error) {
    path_ = path;
    const std::filesystem::path target = to_path(path);
    std::error_code ec;
    if (target.has_parent_path()) {
        std::filesystem::create_directories(target.parent_path(), ec);
    }
    file_.open(target, std::ios::binary | std::ios::trunc);
    if (!file_) {
        error = "no se puede crear " + path;
        return false;
    }
    file_.write(header.data(), static_cast<std::streamsize>(header.size()));
    file_.flush();
    failed_ = !file_;
    rows_ = 0;
    return true;
}

void NotesLog::append(std::uint64_t step, double car_s, const game::NoteOrder& order,
                      const game::NoteApplied& applied,
                      const std::vector<game::DirectionChange>& changes) {
    if (!file_.is_open()) {
        return;
    }
    row_.clear();
    append_notes_log_row(step, car_s, order, applied, changes, row_);
    file_.write(row_.data(), static_cast<std::streamsize>(row_.size()));
    file_.flush();
    failed_ = failed_ || !file_;
    ++rows_;
}

bool NotesLog::close(std::string& error) {
    if (!file_.is_open()) {
        return true;
    }
    file_.close();
    if (failed_ || file_.fail()) {
        error = "no se pudo escribir " + path_;
        return false;
    }
    return true;
}

} // namespace rally::io
