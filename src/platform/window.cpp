#include "platform/window.h"

#include <raylib.h>

namespace rally::platform {

Window::Window(int width, int height, const char* title) {
    InitWindow(width, height, title);
    SetExitKey(KEY_ESCAPE);
}

Window::~Window() { CloseWindow(); }

bool Window::should_close() const { return WindowShouldClose(); }

} // namespace rally::platform
