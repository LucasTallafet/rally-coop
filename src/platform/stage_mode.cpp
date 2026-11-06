#include "platform/stage_mode.h"

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

bool StageSelection::set_text(std::string new_text, std::string& error) {
    sim::StageTrack built;
    const sim::StageParseResult parsed = sim::StageTrack::build(new_text, built);
    if (!parsed.ok) {
        error = parsed.error;
        return false;
    }
    text = std::move(new_text);
    track = std::move(built);
    name = track.axis().name();
    length = track.axis().length();
    return true;
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
            out.message =
                "tramo: no se encuentra data/stages/lazadas.stage (se busca desde la carpeta del "
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
    desc.stage_text = selection.text;
    desc.car = true;
    desc.car_data = car;
    desc.car_spawn = sim::stage_spawn(selection.track, kGameStageSpawnS);
    desc.aids = aids;
    return desc;
}

} // namespace rally::platform
