#include "platform/pointer_mode.h"

namespace rally::platform {

PointerFrame pointer_frame(bool held, bool mouse_wanted) {
    PointerFrame frame;
    frame.frozen = held;
    frame.clicks = held;
    frame.capture = mouse_wanted && !held;
    return frame;
}

} // namespace rally::platform
