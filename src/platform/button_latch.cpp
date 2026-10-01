#include "platform/button_latch.h"

namespace rally::platform {

std::uint16_t ButtonLatch::apply(std::uint16_t frame_buttons, int step_count) {
    pending_ = static_cast<std::uint16_t>(pending_ | frame_buttons);
    const std::uint16_t buttons = pending_;
    if (step_count > 0) {
        pending_ = 0;
    }
    return buttons;
}

} // namespace rally::platform
