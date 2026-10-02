#include "platform/window.h"

#include <raylib.h>

namespace rally::platform {

Window::Window(int width, int height, const char* title) {
    InitWindow(width, height, title);
    SetExitKey(KEY_ESCAPE);
}

Window::~Window() { CloseWindow(); }

bool Window::should_close() const { return WindowShouldClose(); }

void Window::handle_keys() {
    const bool alt = IsKeyDown(KEY_LEFT_ALT) || IsKeyDown(KEY_RIGHT_ALT);
    if (IsKeyPressed(KEY_F11) || (alt && IsKeyPressed(KEY_ENTER))) {
        toggle_fullscreen();
    }
}

void Window::toggle_fullscreen() { ToggleBorderlessWindowed(); }

} // namespace rally::platform
