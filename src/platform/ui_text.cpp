#include "platform/ui_text.h"

#include <algorithm>
#include <array>

namespace rally::platform {

namespace {

// charsWidth[224] de LoadFontDefault en raylib 6.0 (src/rtext.c): ancho a 10 px de los puntos
// de código 32..255. Si raylib cambia la fuente, el aviso del arranque lo dice.
constexpr std::array<unsigned char, 224> kGlyphWidths = {
    3, 1, 4, 6, 5, 7, 6, 2, 3, 3, 5, 5, 2, 4, 1, 7, 5, 2, 5, 5, 5, 5, 5, 5, 5, 5, 1, 1, 3, 4, 3, 6,
    7, 6, 6, 6, 6, 6, 6, 6, 6, 3, 5, 6, 5, 7, 6, 6, 6, 6, 6, 6, 7, 6, 7, 7, 6, 6, 6, 2, 7, 2, 3, 5,
    2, 5, 5, 5, 5, 5, 4, 5, 5, 1, 2, 5, 2, 5, 5, 5, 5, 5, 5, 5, 4, 5, 5, 5, 5, 5, 5, 3, 1, 3, 4, 4,
    1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1,
    1, 1, 5, 5, 5, 7, 1, 5, 3, 7, 3, 5, 4, 1, 7, 4, 3, 5, 3, 3, 2, 5, 6, 1, 2, 2, 3, 5, 6, 6, 6, 6,
    6, 6, 6, 6, 6, 6, 7, 6, 6, 6, 6, 6, 3, 3, 3, 3, 7, 6, 6, 6, 6, 6, 6, 5, 6, 6, 6, 6, 6, 6, 4, 6,
    5, 5, 5, 5, 5, 5, 9, 5, 5, 5, 5, 5, 2, 2, 3, 3, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 5, 3, 5};

bool continuation(unsigned char byte) { return (byte & 0xC0) == 0x80; }

unsigned char byte_at(std::string_view text, std::size_t i) {
    // Como raylib, que lee hasta el '\0': fuera del texto cuenta como un byte nulo.
    return i < text.size() ? static_cast<unsigned char>(text[i]) : 0;
}

// Suma de anchos a 10 px y número de glifos de la línea más ancha, como MeasureTextEx.
struct Measure {
    int width = 0;
    int glyphs = 0;
};

int width_from(int glyph_sum, int glyphs, int font_size) {
    if (glyphs == 0) {
        return 0;
    }
    const int size = std::max(font_size, kDefaultFontBase);
    const int spacing = size / kDefaultFontBase;
    // En float, como raylib: tamaño / base y luego (int) del total.
    const float scale = static_cast<float>(size) / static_cast<float>(kDefaultFontBase);
    return static_cast<int>(static_cast<float>(glyph_sum) * scale +
                            static_cast<float>((glyphs - 1) * spacing));
}

} // namespace

char32_t next_codepoint(std::string_view utf8, std::size_t at, std::size_t& bytes) {
    bytes = 1;
    const unsigned char b0 = byte_at(utf8, at);
    if ((b0 & 0xF8) == 0xF0) {
        if (!continuation(byte_at(utf8, at + 1)) || !continuation(byte_at(utf8, at + 2)) ||
            !continuation(byte_at(utf8, at + 3))) {
            return U'?';
        }
        bytes = 4;
        return (static_cast<char32_t>(b0 & 0x07) << 18) |
               (static_cast<char32_t>(byte_at(utf8, at + 1) & 0x3F) << 12) |
               (static_cast<char32_t>(byte_at(utf8, at + 2) & 0x3F) << 6) |
               static_cast<char32_t>(byte_at(utf8, at + 3) & 0x3F);
    }
    if ((b0 & 0xF0) == 0xE0) {
        if (!continuation(byte_at(utf8, at + 1)) || !continuation(byte_at(utf8, at + 2))) {
            return U'?';
        }
        bytes = 3;
        return (static_cast<char32_t>(b0 & 0x0F) << 12) |
               (static_cast<char32_t>(byte_at(utf8, at + 1) & 0x3F) << 6) |
               static_cast<char32_t>(byte_at(utf8, at + 2) & 0x3F);
    }
    if ((b0 & 0xE0) == 0xC0) {
        if (!continuation(byte_at(utf8, at + 1))) {
            return U'?';
        }
        bytes = 2;
        return (static_cast<char32_t>(b0 & 0x1F) << 6) |
               static_cast<char32_t>(byte_at(utf8, at + 1) & 0x3F);
    }
    if ((b0 & 0x80) == 0) {
        return b0;
    }
    return U'?';
}

int glyph_width(char32_t codepoint) {
    if (codepoint < kDefaultFontFirst || codepoint > kDefaultFontLast) {
        codepoint = U'?';
    }
    return kGlyphWidths[codepoint - kDefaultFontFirst];
}

int text_width(std::string_view utf8, int font_size) {
    Measure widest;
    Measure line;
    for (std::size_t i = 0; i < utf8.size();) {
        std::size_t bytes = 1;
        const char32_t c = next_codepoint(utf8, i, bytes);
        i += bytes;
        if (c == U'\n') {
            widest.width = std::max(widest.width, line.width);
            widest.glyphs = std::max(widest.glyphs, line.glyphs);
            line = Measure{};
            continue;
        }
        line.width += glyph_width(c);
        ++line.glyphs;
    }
    widest.width = std::max(widest.width, line.width);
    widest.glyphs = std::max(widest.glyphs, line.glyphs);
    return width_from(widest.width, widest.glyphs, font_size);
}

bool in_default_font(std::string_view utf8) {
    for (std::size_t i = 0; i < utf8.size();) {
        std::size_t bytes = 1;
        const char32_t c = next_codepoint(utf8, i, bytes);
        if (c == U'?' && utf8[i] != '?') {
            return false; // UTF-8 no válido
        }
        if (c < kDefaultFontFirst || c > kDefaultFontLast) {
            return false;
        }
        i += bytes;
    }
    return true;
}

std::size_t fit_text(std::string_view utf8, int font_size, int max_width, bool& ellipsis) {
    ellipsis = false;
    if (text_width(utf8, font_size) <= max_width) {
        return utf8.size();
    }
    ellipsis = true;
    // Glifos que caben con "..." detrás: se suman de uno en uno (texto de una línea).
    constexpr std::string_view kDots = "...";
    const int dots = 3 * glyph_width(U'.');
    int sum = 0;
    int glyphs = 0;
    std::size_t fitted = 0;
    for (std::size_t i = 0; i < utf8.size();) {
        std::size_t bytes = 1;
        const char32_t c = next_codepoint(utf8, i, bytes);
        if (width_from(sum + glyph_width(c) + dots, glyphs + 1 + static_cast<int>(kDots.size()),
                       font_size) > max_width) {
            break;
        }
        sum += glyph_width(c);
        ++glyphs;
        i += bytes;
        fitted = i;
    }
    return fitted;
}

} // namespace rally::platform
