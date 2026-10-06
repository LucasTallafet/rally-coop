#include "platform/slip.h"

#include <cmath>

namespace rally::platform {

float normalized_slip(float kappa, float alpha, const sim::TireSurface& tire) {
    const float rho_x = kappa / tire.slip_ratio_peak;
    const float rho_y = std::tan(alpha) / tire.tan_alpha_peak;
    return std::hypot(rho_x, rho_y);
}

} // namespace rally::platform
