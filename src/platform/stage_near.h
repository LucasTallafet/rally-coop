#pragma once

#include "platform/near_field.h"
#include "platform/stage_surface.h"
#include "sim/stage_track.h"
#include "sim/test_zone.h"

#include <cstdint>

namespace rally::platform {

// Celda de la rejilla cercana del tramo (tarea tramo-dibujo, lote 5): la altura del suelo del
// tramo (núcleo y terreno, StageSurface) y el microrrelieve h de la rueda (stage_relief_frame y
// stage_relief_height, con el marco de la proyección más cercana al punto). Sin reservas. Los tres
// objetos tienen que vivir mientras se use.
class StageNearSampler {
public:
    StageNearSampler(const sim::StageTrack& track, const StageSurface& surface,
                     const sim::TestZone& noise)
        : track_(&track), surface_(&surface), noise_(&noise) {}

    // La celda del mundo (x, z) (metros).
    NearTexel at(double x, double z) const;
    // La celda de la rejilla (gx, gz) de paso `spacing`.
    NearTexel cell(std::int64_t gx, std::int64_t gz, double spacing) const {
        return at(static_cast<double>(gx) * spacing, static_cast<double>(gz) * spacing);
    }
    // El microrrelieve que daría la rueda en (x, z) con la s de arranque `s_hint`.
    double relief(double x, double z, double s_hint) const;

private:
    const sim::StageTrack* track_;
    const StageSurface* surface_;
    const sim::TestZone* noise_;
};

} // namespace rally::platform
