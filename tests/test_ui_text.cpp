#include "platform/ui_text.h"

#include <doctest/doctest.h>

#include <string>
#include <string_view>

using rally::platform::fit_text;
using rally::platform::glyph_width;
using rally::platform::in_default_font;
using rally::platform::text_width;

// La tabla de anchos sale de raylib: la garantía de que coincide con MeasureText es la
// comprobación del arranque (overlay.cpp). Aquí, la fórmula de MeasureText sobre esa tabla.
TEST_CASE("ui_text: width follows the MeasureText formula") {
    // H 6, o 5, l 2, a 5 → 18 a 10 px; a 20, 36 más 3 separaciones de 2.
    CHECK(text_width("Hola", 20) == 42);
    CHECK(text_width("Hola", 10) == 21);
    CHECK(text_width("", 20) == 0);
    CHECK(text_width("W", 30) == 21); // un glifo: sin separación
    // Por debajo de 10 px, raylib dibuja a 10.
    CHECK(text_width("Hola", 6) == 21);
}

TEST_CASE("ui_text: two-byte UTF-8 counts as one glyph") {
    // 'ñ' (U+00F1) está en la fuente: un glifo de 5, no dos.
    CHECK(glyph_width(U'ñ') == 5);
    CHECK(text_width("ñ", 10) == 5);
    CHECK(text_width("añ", 10) == 5 + 5 + 1);
}

TEST_CASE("ui_text: codepoints outside the font are drawn as question marks") {
    CHECK(glyph_width(U'–') == glyph_width(U'?')); // «–»
    CHECK(text_width("–", 20) == text_width("?", 20));
    CHECK_FALSE(in_default_font("R – N"));
    CHECK_FALSE(in_default_font("↑"));
    CHECK(in_default_font("Cámara y asiento · ± «Prueba»"));
    CHECK_FALSE(in_default_font("\xC3")); // UTF-8 cortado
}

TEST_CASE("ui_text: newline measures the widest line") {
    CHECK(text_width("Hola\nH", 20) == text_width("Hola", 20));
}

TEST_CASE("ui_text: fit_text keeps whole codepoints and adds an ellipsis only when needed") {
    bool ellipsis = true;
    CHECK(fit_text("Hola", 20, 42, ellipsis) == 4);
    CHECK_FALSE(ellipsis);
    // "Ho" + "..." a 20: (6 + 5 + 3 × 1) × 2 + 4 × 2 = 36.
    CHECK(fit_text("Hola", 20, 36, ellipsis) == 2);
    CHECK(ellipsis);
    // "ñ" ocupa 2 bytes: se corta antes o después, nunca en medio.
    const std::string_view text = "ññññññ";
    const std::size_t bytes = fit_text(text, 20, 50, ellipsis);
    CHECK(ellipsis);
    CHECK(bytes % 2 == 0);
    CHECK(text_width(std::string(text.substr(0, bytes)) + "...", 20) <= 50);
}
