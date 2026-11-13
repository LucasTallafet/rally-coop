#include "net/notes_wire.h"

#include "net/wire.h"

namespace rally::net {

namespace {

using game::DistanceKind;
using game::DistanceSource;
using game::Note;
using game::NoteGrade;
using game::NoteMark;
using game::NoteOrder;
using game::NoteOrderKind;
using game::NoteSide;

double quantized(double s) { return from_mm(to_mm(s)); }

void write_note(Out& o, const Note& n) {
    o.u32(n.id);
    o.i32(to_mm(n.s));
    o.u8(static_cast<std::uint8_t>(n.grade));
    o.u8(static_cast<std::uint8_t>(n.side));
    o.u8(n.marks);
    o.u8(static_cast<std::uint8_t>(n.distance.kind));
    o.u16(n.distance.metres);
    o.u8(static_cast<std::uint8_t>(n.distance.source));
}

// Campos tal como vienen; el llamador comprueba rangos.
struct RawNote {
    std::uint32_t id = 0;
    std::int32_t s_mm = 0;
    std::uint8_t grade = 0, side = 0, marks = 0, kind = 0, source = 0;
    std::uint16_t metres = 0;
};

bool read_raw(In& r, RawNote& n) {
    return r.u32(n.id) && r.i32(n.s_mm) && r.u8(n.grade) && r.u8(n.side) && r.u8(n.marks) &&
           r.u8(n.kind) && r.u16(n.metres) && r.u8(n.source);
}

bool raw_distance(const RawNote& n, game::NoteDistance& d) {
    if (n.kind > static_cast<std::uint8_t>(DistanceKind::Over300) ||
        n.source > static_cast<std::uint8_t>(DistanceSource::Counter)) {
        return false;
    }
    d.kind = static_cast<DistanceKind>(n.kind);
    d.source = static_cast<DistanceSource>(n.source);
    d.metres = n.metres;
    return game::valid(d);
}

bool is_zero(const RawNote& n) {
    return n.id == 0 && n.s_mm == 0 && n.grade == 0 && n.side == 0 && n.marks == 0 && n.kind == 0 &&
           n.source == 0 && n.metres == 0;
}

} // namespace

NotesState notes_state_from(const game::NoteQueue& queue, std::uint8_t generation) {
    NotesState s;
    s.generation = generation;
    s.revision = queue.revision();
    s.last_result = queue.last_result();
    s.count = queue.size();
    for (int i = 0; i < s.count; ++i) {
        s.anchors[static_cast<std::size_t>(i)] = quantized(queue.at(i).s);
    }
    if (queue.has_last()) {
        s.has_last = true;
        s.last = queue.last();
        s.last.s = quantized(s.last.s);
    }
    return s;
}

int notes_ahead(const NotesState& state, double car_s) {
    int n = 0;
    for (int i = 0; i < state.count; ++i) {
        if (state.anchors[static_cast<std::size_t>(i)] >= car_s) {
            ++n;
        }
    }
    return n;
}

bool notes_has_anchor_near(const NotesState& state, double s, double tolerance) {
    for (int i = 0; i < state.count; ++i) {
        const double d = state.anchors[static_cast<std::size_t>(i)] - s;
        if (d <= tolerance && -d <= tolerance) {
            return true;
        }
    }
    return false;
}

std::string encode_note_order(const NoteOrder& order, std::uint8_t generation) {
    Out o;
    o.u8(kMsgNoteOrder);
    o.u8(generation);
    o.u8(static_cast<std::uint8_t>(order.kind));
    const bool has_s = order.kind == NoteOrderKind::Corner || order.kind == NoteOrderKind::Hazard;
    o.i32(has_s ? to_mm(order.s) : 0);
    o.u8(order.kind == NoteOrderKind::Corner ? static_cast<std::uint8_t>(order.grade) : 0);
    o.u8(order.kind == NoteOrderKind::Corner ? static_cast<std::uint8_t>(order.side) : 0);
    const bool has_mark = order.kind == NoteOrderKind::Hazard || order.kind == NoteOrderKind::Mark;
    o.u8(has_mark ? static_cast<std::uint8_t>(order.mark) : 0);
    const bool has_dist = order.kind == NoteOrderKind::Distance;
    o.u8(has_dist ? static_cast<std::uint8_t>(order.distance.kind) : 0);
    o.u16(has_dist ? order.distance.metres : 0);
    o.u8(has_dist ? static_cast<std::uint8_t>(order.distance.source) : 0);
    return o.take();
}

bool decode_note_order(std::span<const std::uint8_t> in, NoteOrder& order,
                       std::uint8_t& generation) {
    if (in.size() != kNoteOrderBytes) {
        return false;
    }
    In r(in);
    std::uint8_t type = 0, gen = 0, kind = 0, grade = 0, side = 0, mark = 0, dkind = 0, dsrc = 0;
    std::int32_t s_mm = 0;
    std::uint16_t metres = 0;
    if (!r.u8(type) || type != kMsgNoteOrder || !r.u8(gen) || !r.u8(kind) || !r.i32(s_mm) ||
        !r.u8(grade) || !r.u8(side) || !r.u8(mark) || !r.u8(dkind) || !r.u16(metres) ||
        !r.u8(dsrc) || !r.at_end()) {
        return false;
    }
    if (kind < static_cast<std::uint8_t>(NoteOrderKind::Corner) ||
        kind > static_cast<std::uint8_t>(NoteOrderKind::ClearPending) ||
        grade > static_cast<std::uint8_t>(NoteGrade::Hairpin) ||
        side > static_cast<std::uint8_t>(NoteSide::Right) || mark >= game::kNoteMarkCount) {
        return false;
    }
    NoteOrder o;
    o.kind = static_cast<NoteOrderKind>(kind);
    const bool dist_zero = dkind == 0 && metres == 0 && dsrc == 0;
    switch (o.kind) {
    case NoteOrderKind::Corner:
        if (mark != 0 || !dist_zero) {
            return false;
        }
        o.s = from_mm(s_mm);
        o.grade = static_cast<NoteGrade>(grade);
        o.side = static_cast<NoteSide>(side);
        break;
    case NoteOrderKind::Hazard:
        if (grade != 0 || side != 0 || !dist_zero) {
            return false;
        }
        o.s = from_mm(s_mm);
        o.mark = static_cast<NoteMark>(mark);
        break;
    case NoteOrderKind::Mark:
        if (s_mm != 0 || grade != 0 || side != 0 || !dist_zero) {
            return false;
        }
        o.mark = static_cast<NoteMark>(mark);
        break;
    case NoteOrderKind::Distance: {
        if (s_mm != 0 || grade != 0 || side != 0 || mark != 0) {
            return false;
        }
        RawNote raw;
        raw.kind = dkind;
        raw.metres = metres;
        raw.source = dsrc;
        if (!raw_distance(raw, o.distance)) {
            return false;
        }
        break;
    }
    case NoteOrderKind::EraseLast:
    case NoteOrderKind::ClearPending:
        if (s_mm != 0 || grade != 0 || side != 0 || mark != 0 || !dist_zero) {
            return false;
        }
        break;
    }
    if (!game::valid(o)) {
        return false;
    }
    order = o;
    generation = gen;
    return true;
}

std::string encode_notes_state(const NotesState& state) {
    Out o;
    o.u8(kMsgNotesState);
    o.u8(state.generation);
    o.u32(state.revision);
    o.u8(static_cast<std::uint8_t>(state.last_result));
    o.u8(state.has_last ? 1 : 0);
    write_note(o, state.has_last ? state.last : Note{});
    o.u16(static_cast<std::uint16_t>(state.count));
    for (int i = 0; i < state.count; ++i) {
        o.i32(to_mm(state.anchors[static_cast<std::size_t>(i)]));
    }
    return o.take();
}

bool decode_notes_state(std::span<const std::uint8_t> in, NotesState& out) {
    In r(in);
    NotesState s;
    std::uint8_t type = 0, result = 0, has_last = 0;
    RawNote last;
    std::uint16_t n = 0;
    if (!r.u8(type) || type != kMsgNotesState || !r.u8(s.generation) || !r.u32(s.revision) ||
        !r.u8(result) || !r.u8(has_last) || !read_raw(r, last) || !r.u16(n)) {
        return false;
    }
    if (result > static_cast<std::uint8_t>(game::NoteResult::Invalid) || has_last > 1 ||
        n > game::kMaxNotes || (n == 0) != (has_last == 0) ||
        in.size() != 25 + 4 * static_cast<std::size_t>(n)) {
        return false;
    }
    s.last_result = static_cast<game::NoteResult>(result);
    s.count = n;
    if (has_last == 1) {
        if (last.grade > static_cast<std::uint8_t>(NoteGrade::Hairpin) ||
            last.side > static_cast<std::uint8_t>(NoteSide::Right) ||
            (last.marks & ~((1u << game::kNoteMarkCount) - 1u)) != 0 || last.id == 0) {
            return false;
        }
        s.has_last = true;
        s.last.id = last.id;
        s.last.s = from_mm(last.s_mm);
        s.last.grade = static_cast<NoteGrade>(last.grade);
        s.last.side = static_cast<NoteSide>(last.side);
        s.last.marks = last.marks;
        if (!raw_distance(last, s.last.distance)) {
            return false;
        }
    } else if (!is_zero(last)) {
        return false;
    }
    for (int i = 0; i < n; ++i) {
        std::int32_t mm = 0;
        if (!r.i32(mm)) {
            return false;
        }
        s.anchors[static_cast<std::size_t>(i)] = from_mm(mm);
    }
    if (!r.at_end()) {
        return false;
    }
    out = s;
    return true;
}

} // namespace rally::net
