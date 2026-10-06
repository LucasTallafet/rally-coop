#include "platform/surface_feel.h"

#include <algorithm>
#include <cmath>

namespace rally::platform {

float surface_roughness(sim::Surface surface, bool blind) {
    return blind ? kBlindRoughness : kSurfaceRoughness[static_cast<std::size_t>(surface)];
}

float mean_roughness(const std::array<std::int8_t, 4>& surfaces, bool blind) {
    float sum = 0.0f;
    int count = 0;
    for (const std::int8_t s : surfaces) {
        if (s < 0) {
            continue;
        }
        sum += surface_roughness(static_cast<sim::Surface>(s), blind);
        ++count;
    }
    return count > 0 ? sum / static_cast<float>(count) : 0.0f;
}

float bump_term(float suspension_speed) {
    return std::clamp((suspension_speed - kBumpSpeedStart) / (kBumpSpeedFull - kBumpSpeedStart),
                      0.0f, 1.0f);
}

Rumble continuous_rumble(const RumbleInput& input, bool blind) {
    const float rough =
        kRoughGain * input.roughness * std::min(input.speed / kRoughSpeedFull, 1.0f);
    const float bumps = blind ? 0.0f : kBumpGain * bump_term(input.suspension_speed);
    Rumble rumble;
    rumble.low = std::min(kLowMax, rough + bumps);
    rumble.high = kHighMax * std::clamp(input.rho_max - 1.0f, 0.0f, 1.0f);
    rumble.duration_ms = kRumbleSendMs;
    return rumble;
}

std::optional<Rumble> RumbleMixer::update(const Rumble& continuous, float frame_impact,
                                          std::int64_t now_ns, bool silent) {
    Rumble target;
    if (silent) {
        impact_until_ns_ = 0;
    } else {
        const Rumble hit = rumble_for_impact(frame_impact);
        if (hit.duration_ms > 0) {
            impact_ = hit;
            impact_until_ns_ = now_ns + static_cast<std::int64_t>(hit.duration_ms) * 1'000'000;
        }
        target.low = continuous.low;
        target.high = continuous.high;
        if (now_ns < impact_until_ns_) {
            target.low = std::max(target.low, impact_.low);
            target.high = std::max(target.high, impact_.high);
        }
    }
    const bool zero = target.low == 0.0f && target.high == 0.0f;
    const bool sent_zero = !any_sent_ || (sent_.low == 0.0f && sent_.high == 0.0f);
    if (zero && sent_zero) {
        return std::nullopt; // nada que mandar ni que parar
    }
    const bool changed = std::abs(target.low - sent_.low) > kRumbleSendDelta ||
                         std::abs(target.high - sent_.high) > kRumbleSendDelta ||
                         (zero && !sent_zero);
    const bool stale = now_ns - sent_ns_ >= kRumbleResendNs;
    if (!changed && !(stale && !zero)) {
        return std::nullopt;
    }
    // Un 0 se manda con duración 1 ms: SDL lo trata como «parar» igual que con más.
    target.duration_ms = zero ? 1 : kRumbleSendMs;
    sent_ = target;
    sent_ns_ = now_ns;
    any_sent_ = true;
    return target;
}

} // namespace rally::platform
