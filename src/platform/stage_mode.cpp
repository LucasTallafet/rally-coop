#include "platform/stage_mode.h"

#include "sim/stage_axis.h"

#include <utility>

namespace rally::platform {

WorldKind cycle_world_kind(WorldKind current, bool stage_available) {
    switch (current) {
    case WorldKind::Zone:
        return stage_available ? WorldKind::Stage : WorldKind::Zone;
    case WorldKind::Stage:
        return WorldKind::Arena;
    case WorldKind::Arena:
        return WorldKind::Zone;
    }
    return WorldKind::Zone;
}

bool StageSelection::has_irregularity() const { return has_irregularity_; }

namespace {

std::string played_of(const std::string& text, bool flat) {
    return flat ? sim::stage_text_without_irregularity(text) : text;
}

} // namespace

bool StageSelection::set_text(std::string new_text, std::string& error) {
    std::string new_played = played_of(new_text, flat);
    sim::StageTrack built;
    const sim::StageParseResult parsed = sim::StageTrack::build(new_played, built);
    if (!parsed.ok) {
        error = parsed.error;
        return false;
    }
    has_irregularity_ = sim::stage_text_without_irregularity(new_text) != new_text;
    text = std::move(new_text);
    played = std::move(new_played);
    track = std::move(built);
    name = track.axis().name();
    length = track.axis().length();
    return true;
}

bool StageSelection::set_flat(bool value, std::string& error) {
    std::string new_played = played_of(text, value);
    sim::StageTrack built;
    const sim::StageParseResult parsed = sim::StageTrack::build(new_played, built);
    if (!parsed.ok) {
        error = parsed.error;
        return false;
    }
    flat = value;
    played = std::move(new_played);
    track = std::move(built);
    name = track.axis().name();
    length = track.axis().length();
    return true;
}

IrregularityToggle toggle_irregularity(StageSelection& selection) {
    IrregularityToggle out;
    if (!selection.has_irregularity()) {
        out.message = "este tramo no tiene irregularidad que quitar";
        return out;
    }
    std::string error;
    if (!selection.set_flat(!selection.flat, error)) {
        out.message = "tramo: " + error;
        return out;
    }
    out.changed = true;
    out.message = selection.flat ? "tramo sin irregularidad (el coche vuelve a la salida)"
                                 : "tramo con irregularidad (el coche vuelve a la salida)";
    return out;
}

std::string irregularity_suffix(const StageSelection& selection) {
    if (!selection.has_irregularity()) {
        return {};
    }
    return selection.flat ? " · sin irregularidad" : " · con irregularidad";
}

namespace {

std::string base_name(const std::string& path) {
    const std::size_t at = path.find_last_of("/\\");
    return at == std::string::npos ? path : path.substr(at + 1);
}

} // namespace

WorldStep step_world_kind(WorldKind current, StageSelection& stage, const StageFile& file) {
    WorldStep out;
    out.kind = current;
    const WorldKind next = cycle_world_kind(current, true);
    if (next == WorldKind::Stage) {
        if (!file.error.empty()) {
            out.message = "tramo: no se puede leer " + file.path + ": " + file.error;
            return out;
        }
        if (file.path.empty()) {
            out.message = "tramo: no se encuentra data/stages/lazadas.stage (ni "
                          "lazadas_irregular.stage; se busca desde la carpeta del "
                          "exe hacia arriba; o --tramo <archivo>)";
            return out;
        }
        std::string error;
        if (!stage.set_text(file.text, error)) {
            out.message = "tramo: " + base_name(file.path) + ", " + error;
            return out;
        }
        stage.path = file.path;
        out.clear_blind = true;
    }
    out.kind = next;
    out.changed = true;
    return out;
}

sim::WorldDesc stage_world_desc(const StageSelection& selection, const sim::CarData& car,
                                const sim::DriverAids& aids) {
    sim::WorldDesc desc;
    desc.stage_text = selection.played_text();
    desc.car = true;
    desc.car_data = car;
    desc.car_spawn = sim::stage_spawn(selection.track, kGameStageSpawnS);
    desc.aids = aids;
    return desc;
}

} // namespace rally::platform
