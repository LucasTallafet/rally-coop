#include "platform/overlay.h"

#include "platform/ui_text.h"

#include <chrono>
#include <cstdio>
#include <utility>

namespace rally::platform {

namespace {

constexpr Color kOverflow{255, 70, 60, 255};

// Reloj de los mensajes (el de la plataforma; la simulación no lo ve).
std::int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
constexpr Color kMessageError{255, 110, 90, 255};

// Las capas de una línea suelta no llevan fondo ni relleno (como en
// tests/platform/test_screen_layout).
bool padded(Layer layer) {
    return layer != Layer::Indicator && layer != Layer::HelpHint && layer != Layer::Messages &&
           layer != Layer::ActionRing;
}

// Puntos de código de la fuente cuyo ancho en la tabla no es el de MeasureText (y el primero).
struct GlyphCheck {
    int mismatches = 0;
    char32_t first = 0;
};

void append_utf8(char32_t c, std::string& out) {
    if (c < 0x80) {
        out += static_cast<char>(c);
    } else {
        out += static_cast<char>(0xC0 | (c >> 6));
        out += static_cast<char>(0x80 | (c & 0x3F));
    }
}

GlyphCheck check_glyphs() {
    GlyphCheck check;
    std::string text;
    for (char32_t c = kDefaultFontFirst; c <= kDefaultFontLast; ++c) {
        text.clear();
        append_utf8(c, text);
        // A 10 y a 20 px: el ancho del glifo y la escala.
        for (const int size : {10, 20}) {
            if (MeasureText(text.c_str(), size) != text_width(text, size)) {
                if (check.mismatches++ == 0) {
                    check.first = c;
                }
                break;
            }
        }
    }
    // La separación entre glifos, con un texto de varios.
    constexpr const char* kSample = "Cámara y asiento · F11 o Alt+Intro";
    for (const int size : {10, 20, 30}) {
        if (MeasureText(kSample, size) != text_width(kSample, size)) {
            ++check.mismatches;
        }
    }
    return check;
}

} // namespace

Overlay::Overlay() {
    text_buffer_.reserve(512);
    const GlyphCheck check = check_glyphs();
    if (check.mismatches > 0) {
        char line[160];
        std::snprintf(line, sizeof(line),
                      "la tabla de glifos (ui_text) no coincide con MeasureText de raylib en %d "
                      "medidas (primera: U+%04X): el diseño de la pantalla no es fiable",
                      check.mismatches, static_cast<unsigned>(check.first));
        set_message(Message::Glyphs, line, kMessageError);
    }
}

void Overlay::begin(int screen_width, int screen_height) {
    if (screen_width != layout_.width || screen_height != layout_.height || kind_ != layout_.kind) {
        layout_ = layout_screen(screen_width, screen_height, kind_);
    }
    overflow_ = {};
    debug_rect_set_ = false;
}

int Overlay::content_x(Layer layer) const {
    return rect(layer).x + (padded(layer) ? layout_.m.padding : 0);
}

int Overlay::content_y(Layer layer) const {
    return rect(layer).y + (padded(layer) ? layout_.m.padding : 0);
}

void Overlay::panel(Layer layer, Color color) const {
    const UiRect& r = rect(layer);
    DrawRectangle(r.x, r.y, r.w, r.h, color);
}

void Overlay::text(Layer layer, int x, int y, std::string_view text, Color color, bool big) {
    const UiMetrics& m = layout_.m;
    const int size = big ? m.big_font : m.font;
    text_buffer_.assign(text);
    const int width = MeasureText(text_buffer_.c_str(), size);
    const UiRect& r = rect(layer);
    const int pad = padded(layer) ? m.padding : 0;
    if (x < r.x + pad || y < r.y + pad || x + width > r.right() - pad ||
        y + size > r.bottom() - pad) {
        report_overflow(layer, text);
    }
    DrawText(text_buffer_.c_str(), x, y, size, color);
}

void Overlay::text_right(Layer layer, int right, int y, std::string_view text, Color color) {
    text_buffer_.assign(text);
    const int width = MeasureText(text_buffer_.c_str(), layout_.m.font);
    this->text(layer, right - width, y, text, color);
}

void Overlay::text_fit(Layer layer, int x, int y, std::string_view text, Color color) {
    const UiRect& r = rect(layer);
    const int pad = padded(layer) ? layout_.m.padding : 0;
    bool ellipsis = false;
    const std::size_t bytes = fit_text(text, layout_.m.font, r.right() - pad - x, ellipsis);
    if (!ellipsis) {
        this->text(layer, x, y, text, color);
        return;
    }
    std::string cut(text.substr(0, bytes));
    cut += "...";
    this->text(layer, x, y, cut, color);
}

void Overlay::set_message(Message slot, std::string text, Color color, std::int64_t duration_ns) {
    Entry& e = messages_[static_cast<std::size_t>(slot)];
    e.text = std::move(text);
    e.color = color;
    e.expires_ns = duration_ns > 0 ? now_ns() + duration_ns : 0;
    e.active = true;
}

void Overlay::clear_message(Message slot) { messages_[static_cast<std::size_t>(slot)] = Entry{}; }

void Overlay::report_overflow(Layer layer, std::string_view text) {
    const auto index = static_cast<std::size_t>(layer);
    overflow_[index] = true;
    if (reported_[index]) {
        return;
    }
    reported_[index] = true;
    set_message(Message::Overflow,
                "texto fuera de su sitio en la capa " + std::string(layer_name(layer)) + ": " +
                    std::string(text),
                kOverflow);
}

void Overlay::draw_messages() {
    const UiMetrics& m = layout_.m;
    const UiRect& r = rect(Layer::Messages);
    const std::int64_t now = now_ns();
    int line = 0;
    for (Entry& e : messages_) {
        if (!e.active) {
            continue;
        }
        if (e.expires_ns != 0 && now >= e.expires_ns) {
            e = Entry{};
            continue;
        }
        if (line < kMessageLines) {
            text_fit(Layer::Messages, r.x, r.y + line * m.line, e.text, e.color);
            ++line;
        }
    }
    for (int i = 0; i < kLayerCount; ++i) {
        if (overflow_[static_cast<std::size_t>(i)]) {
            const UiRect& o = rect(static_cast<Layer>(i));
            DrawRectangleLines(o.x, o.y, o.w, o.h, kOverflow);
        }
    }
}

} // namespace rally::platform
