#pragma once

#include "platform/screen_layout.h"

#include <raylib.h>

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace rally::platform {

// Texto en pantalla por capas con sitio fijo (platform/screen_layout). Recalcula el diseño al
// cambiar el tamaño de la ventana. Cada texto se dibuja en su capa; si se sale de su rectángulo
// (en cualquier compilación, también en Release), la capa se marca en rojo y la de mensajes lo
// dice: no debería pasar nunca, porque el diseño se prueba con el peor caso.
// Al construir compara la tabla de glifos de platform/ui_text con MeasureText de raylib (los 224
// glifos); si alguno no coincide, también lo dice en los mensajes.
class Overlay {
public:
    // Mensajes, de más a menos importante: se ven los kMessageLines primeros activos.
    enum class Message : std::uint8_t {
        Pause,
        Glyphs,
        Overflow,
        Car,
        Recording,
        Settings,
        Action, // acciones del copiloto (teclas 7, 8 y 9)
        Count,
    };

    // Necesita la ventana abierta.
    Overlay();

    // Al empezar el dibujo del texto: diseño para el tamaño actual de la ventana.
    void begin(int screen_width, int screen_height);
    const ScreenLayout& layout() const { return layout_; }
    const UiMetrics& metrics() const { return layout_.m; }
    const UiRect& rect(Layer layer) const { return layout_.rect(layer); }
    // Esquina de arriba a la izquierda del contenido (dentro del relleno).
    int content_x(Layer layer) const;
    int content_y(Layer layer) const;

    // Fondo de un panel en todo su rectángulo.
    void panel(Layer layer, Color color) const;
    // Texto en coordenadas de pantalla dentro de la capa, a la fuente normal o a la grande.
    void text(Layer layer, int x, int y, std::string_view text, Color color, bool big = false);
    // Texto con su borde derecho en `right`.
    void text_right(Layer layer, int right, int y, std::string_view text, Color color);
    // Texto recortado con "..." para que quepa hasta el borde derecho del contenido.
    void text_fit(Layer layer, int x, int y, std::string_view text, Color color);

    // Mensaje que dura duration_ns desde ahora (0: hasta que se quite o se cambie).
    void set_message(Message slot, std::string text, Color color, std::int64_t duration_ns = 0);
    void clear_message(Message slot);
    // Los mensajes activos, en su capa. Después de todo lo demás: también marca en rojo las
    // capas con texto fuera de su sitio.
    void draw_messages();

private:
    struct Entry {
        std::string text;
        Color color{};
        std::int64_t expires_ns = 0; // 0: no caduca
        bool active = false;
    };
    void report_overflow(Layer layer, std::string_view text);

    ScreenLayout layout_;
    std::array<Entry, static_cast<std::size_t>(Message::Count)> messages_{};
    std::array<bool, kLayerCount> overflow_{}; // en este fotograma
    std::array<bool, kLayerCount> reported_{}; // ya avisada (el aviso se queda)
    std::string text_buffer_;                  // texto con '\0' para DrawText
};

} // namespace rally::platform
