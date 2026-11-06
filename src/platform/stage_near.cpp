#include "platform/stage_near.h"

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
    texel.surface = static_cast<float>(hit.surface);
    texel.micro = static_cast<float>(relief(x, z, track_->acquire(x, z).s));
    return texel;
}

} // namespace rally::platform
