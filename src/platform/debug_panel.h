#pragma once

#include "platform/overlay.h"
#include "platform/screen_layout.h"

#include <raylib.h>

#include <string>
#include <utility>
#include <vector>

namespace rally::platform {

// Líneas del panel de depuración (F3) de un fotograma, agrupadas en bloques que no se parten entre
// páginas. El panel (DebugPanel) mide cada línea, reparte los bloques en páginas y se ajusta a
// lo que se dibuja de verdad.
class DebugText {
public:
    struct Line {
        std::string text;
        Color color;
        bool bars = false; // fila de las barras de los ejes: la dibuja quien llama
    };
    // Empieza un bloque nuevo.
    void block() { starts_.push_back(static_cast<int>(lines_.size())); }
    void add(std::string text, Color color) { lines_.push_back({std::move(text), color, false}); }
    void add_bars() { lines_.push_back({{}, WHITE, true}); }
    const std::vector<Line>& lines() const { return lines_; }
    int block_count() const { return static_cast<int>(starts_.size()); }
    // Índice de la primera línea del bloque `i` y cuántas tiene.
    int block_first(int i) const { return starts_[static_cast<std::size_t>(i)]; }
    int block_size(int i) const {
        const int end =
            i + 1 < block_count() ? block_first(i + 1) : static_cast<int>(lines_.size());
        return end - block_first(i);
    }

private:
    std::vector<Line> lines_;
    std::vector<int> starts_;
};

// Dónde quedó la fila de las barras de los ejes (y = -1 si no está en la página que se ve).
struct DebugDrawn {
    int bars_x = 0;
    int bars_y = -1;
};

// Estado de F3: la página que se ve (-1 oculto), cuántas páginas tuvo el último fotograma y el
// ancho retenido (histéresis de presentación).
class DebugPanel {
public:
    bool visible() const { return page_ >= 0; }
    // F3: oculto, página 1, ..., última, oculto.
    void press() { page_ = page_ < 0 ? 0 : (page_ + 1 < pages_ ? page_ + 1 : -1); }
    void show_first() { page_ = 0; }
    // Dibuja la página (recortada si hay menos páginas que antes) con su fondo.
    DebugDrawn draw(Overlay& overlay, const DebugText& text, double now_s);

private:
    int page_ = -1;
    int pages_ = 1;
    WidthHold hold_;
};

} // namespace rally::platform
