#include "platform/mouse_steer.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace rally::platform {

std::array<char, kWheelBarChars + 1> wheel_bar(float steer) {
    std::array<char, kWheelBarChars + 1> bar{};
    for (int i = 0; i < kWheelBarChars; ++i) {
        bar[static_cast<std::size_t>(i)] = '-';
    }
    constexpr int kLast = kWheelBarChars - 1;
    bar[kLast / 2] = '+';
    const float t = (std::clamp(steer, -1.0f, 1.0f) + 1.0f) * 0.5f;
    bar[static_cast<std::size_t>(std::lround(t * kLast))] = 'O';
    return bar;
}

void MouseSteer::move(float dx_px, int full_lock_px) {
    full_lock_px_ = static_cast<float>(std::max(full_lock_px, 1));
    position_px_ = std::clamp(position_px_ + dx_px, -full_lock_px_, full_lock_px_);
}

float MouseSteer::steer() const { return std::clamp(position_px_ / full_lock_px_, -1.0f, 1.0f); }

} // namespace rally::platform
