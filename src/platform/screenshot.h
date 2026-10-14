#pragma once

#include <string>

namespace rally::platform {

// Guarda el fotograma en curso como captura_NNN.png junto al exe, con el primer número libre
// (no pisa capturas de otras ejecuciones). Se llama entre BeginDrawing y EndDrawing, después
// de dibujar todo: vacía el lote de raylib y lee el búfer trasero antes de presentarlo, así
// que la captura es la imagen que se va a mostrar, sin lo que pueda pasar al presentarla
// (tearing).
void save_screenshot();
// Lo mismo en `path` (UTF-8, .png), pisándolo si existe (--golden).
void save_screenshot_to(const std::string& path);

} // namespace rally::platform
