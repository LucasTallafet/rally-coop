#include "platform/window.h"

#include "platform/keys_raylib.h"
#include "platform/window_keys.h"

#include <raylib.h>

namespace rally::platform {

Window::Window(int width, int height, const char* title) {
    InitWindow(width, height, title);
    SetExitKey(raylib_key(chords_for(action(WindowAction::Exit))[0].key));
}

Window::~Window() { CloseWindow(); }

bool Window::should_close() const { return WindowShouldClose(); }

void Window::handle_keys() {
    if (action_pressed(action(WindowAction::Fullscreen))) {
        toggle_fullscreen();
    }
}

void Window::toggle_fullscreen() { ToggleBorderlessWindowed(); }

} // namespace rally::platform
