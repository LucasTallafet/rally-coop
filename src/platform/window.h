#pragma once

namespace rally::platform {

// Ventana de raylib (backend SDL). Se abre al construir y se cierra al destruir.
// Esc cierra la ventana; F11 o Alt+Intro alternan la ventana y la pantalla completa sin
// bordes a la resolución nativa del monitor (pantalla completa de escritorio de SDL: no
// cambia el modo de pantalla y mantiene la sincronización vertical).
class Window {
public:
    Window(int width, int height, const char* title);
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool should_close() const;

    // Una vez por fotograma, con la entrada del fotograma ya leída.
    void handle_keys();

    void toggle_fullscreen();
};

} // namespace rally::platform
