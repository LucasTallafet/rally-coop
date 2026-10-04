#include "platform/window.h"

#include "platform/keys_raylib.h"

#include <raylib.h>

namespace rally::platform {

Window::Window(int width, int height, const char* title) {
    InitWindow(width, height, title);
    SetExitKey(raylib_key(chords_for(Action::Exit)[0].key));
}

Window::~Window() { CloseWindow(); }

bool Window::should_close() const { return WindowShouldClose(); }

void Window::handle_keys() {
    if (action_pressed(Action::Fullscreen)) {
        toggle_fullscreen();
    }
}

void Window::toggle_fullscreen() { ToggleBorderlessWindowed(); }

} // namespace rally::platform
