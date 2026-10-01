#pragma once

namespace rally::platform {

// Ventana de raylib (backend SDL). Se abre al construir y se cierra al destruir.
// Esc cierra la ventana.
class Window {
public:
    Window(int width, int height, const char* title);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool should_close() const;
};

} // namespace rally::platform
