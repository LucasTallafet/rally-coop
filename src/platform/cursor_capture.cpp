#include "platform/cursor_capture.h"

#include <raylib.h>

namespace rally::platform {

CursorCapture::~CursorCapture() {
    if (captured_) {
        EnableCursor();
    }
}

void CursorCapture::update(bool want) {
    changed_ = want != captured_;
    if (want && !captured_) {
        DisableCursor();
        captured_ = true;
        skip_delta_ = true;
    } else if (!want && captured_) {
        EnableCursor();
        captured_ = false;
    }
}

bool CursorCapture::take_delta(float& dx, float& dy) {
    if (!captured_) {
        return false;
    }
    const Vector2 delta = GetMouseDelta();
    if (skip_delta_) {
        skip_delta_ = false;
        return false;
    }
    dx = delta.x;
    dy = delta.y;
    return true;
}

} // namespace rally::platform
