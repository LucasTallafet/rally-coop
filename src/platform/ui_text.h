#pragma once

#include <cstddef>
#include <string_view>

namespace rally::platform {

// Medida del texto de la fuente por defecto de raylib sin raylib, para que el diseño de la
// pantalla (platform/screen_layout) se pruebe sin ventana. Reproduce MeasureText de raylib 6.0
// (rtext.c): la fuente es un mapa de bits de 10 px con 224 glifos (puntos de código 32..255);
// el ancho a un tamaño es la suma de los anchos de los glifos × tamaño / 10 más, entre cada dos
// glifos, (tamaño / 10, entero) de separación. Un punto de código que no está se dibuja como
// '?'. La plataforma compara la tabla con MeasureText al arrancar (platform/overlay).
inline constexpr int kDefaultFontBase = 10;
inline constexpr char32_t kDefaultFontFirst = 32;
inline constexpr char32_t kDefaultFontLast = 255;

// Ancho del glifo a 10 px (el de '?' si no está en la fuente).
int glyph_width(char32_t codepoint);
// Ancho en px como MeasureText(text, font_size). Con '\n', el de la línea más larga.
int text_width(std::string_view utf8, int font_size);
// Todos los puntos de código del texto están en la fuente (32..255) y el UTF-8 es válido.
bool in_default_font(std::string_view utf8);

// Punto de código que empieza en utf8[at] y su longitud en bytes, como GetCodepointNext de
// raylib: una secuencia no válida da '?' y avanza un byte.
char32_t next_codepoint(std::string_view utf8, std::size_t at, std::size_t& bytes);

// Bytes del principio del texto que se dibujan en max_width a font_size. Si cabe entero, todos
// y `ellipsis` falso; si no, los que caben con "..." detrás y `ellipsis` verdadero. Nunca corta
// un punto de código por la mitad.
std::size_t fit_text(std::string_view utf8, int font_size, int max_width, bool& ellipsis);

} // namespace rally::platform
