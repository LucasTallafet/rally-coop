#include "game/notes.h"

#include <cstddef>

namespace rally::game {

namespace {

constexpr std::uint16_t kWheelMetres[] = {30, 50, 80, 100, 150, 200};

bool finite(double v) { return (v - v) == 0.0; } // falso con NaN e infinitos

} // namespace

NoteDistance wheel_distance(int index) {
    NoteDistance d;
    d.source = DistanceSource::Wheel;
    if (index <= 0) {
        d.kind = DistanceKind::Link;
    } else if (index >= 7) {
        d.kind = DistanceKind::Over300;
    } else {
        d.kind = DistanceKind::Metres;
        d.metres = kWheelMetres[index - 1];
    }
    return d;
}

NoteDistance counter_distance(int metres) {
    NoteDistance d;
    d.kind = DistanceKind::Metres;
    d.source = DistanceSource::Counter;
    d.metres = static_cast<std::uint16_t>(
        metres < 0 ? 0 : (metres > kMaxNoteDistanceM ? kMaxNoteDistanceM : metres));
    return d;
}

NoteOrder corner_order(double s, NoteGrade grade, NoteSide side) {
    NoteOrder o;
    o.kind = NoteOrderKind::Corner;
    o.s = s;
    o.grade = grade;
    o.side = side;
    return o;
}

NoteOrder hazard_order(double s, NoteMark hazard) {
    NoteOrder o;
    o.kind = NoteOrderKind::Hazard;
    o.s = s;
    o.mark = hazard;
    return o;
}

NoteOrder mark_order(NoteMark mark) {
    NoteOrder o;
    o.kind = NoteOrderKind::Mark;
    o.mark = mark;
    return o;
}

NoteOrder distance_order(const NoteDistance& distance) {
    NoteOrder o;
    o.kind = NoteOrderKind::Distance;
    o.distance = distance;
    return o;
}

NoteOrder erase_last_order() {
    NoteOrder o;
    o.kind = NoteOrderKind::EraseLast;
    return o;
}

NoteOrder clear_pending_order() {
    NoteOrder o;
    o.kind = NoteOrderKind::ClearPending;
    return o;
}

bool valid(const NoteDistance& d) {
    if (static_cast<unsigned>(d.kind) > static_cast<unsigned>(DistanceKind::Over300) ||
        static_cast<unsigned>(d.source) > static_cast<unsigned>(DistanceSource::Counter)) {
        return false;
    }
    if ((d.kind == DistanceKind::None) != (d.source == DistanceSource::None)) {
        return false;
    }
    if (d.kind != DistanceKind::Metres) {
        return d.metres == 0;
    }
    return d.metres <= kMaxNoteDistanceM;
}

bool valid(const NoteOrder& o) {
    switch (o.kind) {
    case NoteOrderKind::Corner:
        return finite(o.s) && static_cast<unsigned>(o.grade) >= 1 &&
               static_cast<unsigned>(o.grade) <= static_cast<unsigned>(NoteGrade::Hairpin) &&
               (o.side == NoteSide::Left || o.side == NoteSide::Right);
    case NoteOrderKind::Hazard:
        return finite(o.s) && static_cast<unsigned>(o.mark) < kNoteMarkCount && is_hazard(o.mark);
    case NoteOrderKind::Mark:
        return static_cast<unsigned>(o.mark) < kNoteMarkCount;
    case NoteOrderKind::Distance:
        return valid(o.distance);
    case NoteOrderKind::EraseLast:
    case NoteOrderKind::ClearPending:
        return true;
    }
    return false;
}

void NoteQueue::reset(double stage_length) {
    count_ = 0;
    length_ = finite(stage_length) && stage_length > 0.0 ? stage_length : 0.0;
    next_id_ = 1;
    revision_ = 0;
    erased_total_ = 0;
    last_result_ = NoteResult::Applied;
}

void NoteQueue::push_erased(const Note& note) {
    erased_[erased_total_ % kMaxNotes] = note;
    ++erased_total_;
}

NoteApplied NoteQueue::apply(const NoteOrder& order, double car_s) {
    ++revision_;
    NoteApplied out;
    if (!valid(order)) {
        out.result = NoteResult::Invalid;
        last_result_ = out.result;
        return out;
    }
    switch (order.kind) {
    case NoteOrderKind::Corner:
    case NoteOrderKind::Hazard: {
        if (count_ >= kMaxNotes) {
            out.result = NoteResult::Full;
            break;
        }
        Note n;
        n.id = next_id_++;
        n.s = order.s < 0.0 ? 0.0 : (order.s > length_ ? length_ : order.s);
        if (order.kind == NoteOrderKind::Corner) {
            n.grade = order.grade;
            n.side = order.side;
        } else {
            n.marks = mark_bit(order.mark);
        }
        notes_[static_cast<std::size_t>(count_++)] = n;
        out.has_note = true;
        out.note = n;
        break;
    }
    case NoteOrderKind::Mark:
    case NoteOrderKind::Distance: {
        if (count_ == 0) {
            out.result = NoteResult::NoNote;
            break;
        }
        Note& n = notes_[static_cast<std::size_t>(count_ - 1)];
        if (order.kind == NoteOrderKind::Mark) {
            n.marks = static_cast<std::uint8_t>(n.marks | mark_bit(order.mark));
        } else {
            n.distance = order.distance;
        }
        out.has_note = true;
        out.note = n;
        break;
    }
    case NoteOrderKind::EraseLast: {
        if (count_ == 0) {
            out.result = NoteResult::NoNote;
            break;
        }
        out.has_note = true;
        out.note = notes_[static_cast<std::size_t>(--count_)];
        push_erased(out.note);
        out.erased = 1;
        break;
    }
    case NoteOrderKind::ClearPending: {
        int kept = 0;
        for (int i = 0; i < count_; ++i) {
            const Note& n = notes_[static_cast<std::size_t>(i)];
            if (n.s >= car_s) {
                push_erased(n);
                ++out.erased;
            } else {
                notes_[static_cast<std::size_t>(kept++)] = n;
            }
        }
        count_ = kept;
        break;
    }
    }
    last_result_ = out.result;
    return out;
}

NotesView NoteQueue::view(double car_s) const {
    NotesView v;
    for (int i = 0; i < count_; ++i) {
        const Note& n = notes_[static_cast<std::size_t>(i)];
        if (n.s < car_s) {
            continue; // pasada: una comparación, no un campo
        }
        ++v.ahead;
        if (!v.has_next || n.s < v.next.s) {
            v.after = v.next;
            v.has_after = v.has_next;
            v.next = n;
            v.has_next = true;
        } else if (!v.has_after || n.s < v.after.s) {
            v.after = n;
            v.has_after = true;
        }
    }
    return v;
}

std::string distance_text(const NoteDistance& d) {
    switch (d.kind) {
    case DistanceKind::None:
        return {};
    case DistanceKind::Link:
        return "y";
    case DistanceKind::Metres:
        return std::to_string(d.metres);
    case DistanceKind::Over300:
        return "300+";
    }
    return {};
}

std::string note_call_text(const Note& n) {
    static const char* const kMarkText[kNoteMarkCount] = {
        "se cierra", "se abre", "no cortes", "larga", "cresta", "salto", "cuidado"};
    std::string out;
    auto add = [&out](const std::string& part) {
        if (!out.empty()) {
            out += ' ';
        }
        out += part;
    };
    if (n.grade == NoteGrade::Hairpin) {
        add("H");
    } else if (n.grade != NoteGrade::None) {
        add(std::to_string(static_cast<unsigned>(n.grade)));
    }
    if (n.side == NoteSide::Left) {
        add("izquierda");
    } else if (n.side == NoteSide::Right) {
        add("derecha");
    }
    for (int m = 0; m < kNoteMarkCount; ++m) {
        if ((n.marks & (1u << m)) != 0) {
            add(kMarkText[m]);
        }
    }
    return out;
}

std::string note_text(const Note& n) {
    std::string out = note_call_text(n);
    const std::string d = distance_text(n.distance);
    if (!d.empty()) {
        if (!out.empty()) {
            out += ' ';
        }
        out += d;
    }
    return out;
}

bool real_distance_after(const std::vector<DirectionChange>& changes, double anchor_s,
                         double& metres) {
    if (changes.empty()) {
        return false;
    }
    std::size_t best = 0;
    double best_dist = 0.0;
    for (std::size_t i = 0; i < changes.size(); ++i) {
        const DirectionChange& c = changes[i];
        const double dist = anchor_s < c.s_start ? c.s_start - anchor_s
                                                 : (anchor_s > c.s_end ? anchor_s - c.s_end : 0.0);
        // A igual distancia gana el de s_start mayor (los cambios van ordenados por s_start).
        if (i == 0 || dist <= best_dist) {
            best = i;
            best_dist = dist;
        }
    }
    if (best + 1 >= changes.size()) {
        return false;
    }
    metres = changes[best + 1].s_start - changes[best].s_end;
    return true;
}

} // namespace rally::game
