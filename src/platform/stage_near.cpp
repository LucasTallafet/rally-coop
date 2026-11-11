#include "platform/stage_near.h"

#include "platform/stage_look.h"

namespace rally::platform {

double StageNearSampler::relief(double x, double z, double s_hint) const {
    const sim::StageReliefFrame f = sim::stage_relief_frame(*track_, s_hint, x, z);
    return sim::stage_relief_height(f, *noise_, f.s, f.t, x, z);
}

NearTexel StageNearSampler::at(double x, double z) const {
    NearTexel texel;
    const StageHit hit = surface_->query(x, z);
    if (!hit.valid) {
        texel.valid = 0.0f;
        return texel;
    }
    texel.height = static_cast<float>(hit.y);
    // Del núcleo, el índice de color del aspecto (muro, cuneta y talud tienen el suyo, >= 5).
    texel.surface = static_cast<float>(hit.core ? stage_color_index(hit.surface, hit.look)
                                                : static_cast<int>(hit.surface));
    texel.micro = static_cast<float>(relief(x, z, track_->acquire(x, z).s));
    return texel;
}

} // namespace rally::platform
