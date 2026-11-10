#pragma once

#include <cstdint>
#include <string>

namespace rally::platform {

// rally.exe --copiloto [dirección[:puerto]]: la segunda instancia, que recibe el coche del juego
// y lo dibuja (lote 3). Por defecto, 127.0.0.1:29470 (net::kDefaultPort).
struct CopilotArgs {
    bool enabled = false;
    std::string address = "127.0.0.1";
    std::uint16_t port = 29470;
    // --ventana <ancho>x<alto> (lote 6): el tamaño de la ventana, de 640 × 360 a 3840 × 2160. Solo
    // un argumento: no se guarda en ajustes.txt ni en ningún otro ajuste.
    int width = 640;
    int height = 360;
};

inline constexpr int kCopilotMinWidth = 640;
inline constexpr int kCopilotMinHeight = 360;
inline constexpr int kCopilotMaxWidth = 3840;
inline constexpr int kCopilotMaxHeight = 2160;

// Busca --copiloto en la línea de órdenes. Sin él, `enabled` queda en falso y devuelve true. Con
// él, la dirección y el puerto son opcionales («127.0.0.1», «127.0.0.1:29471»); la dirección es una
// IP de cuatro números (sin nombres) y el puerto va de 1 a 65535. `--ventana 1280x720` (solo con
// --copiloto) fija el tamaño de la ventana. Si está mal escrito, devuelve false con el motivo en
// `error` (en español).
bool parse_copilot_args(int argc, char** argv, CopilotArgs& out, std::string& error);

} // namespace rally::platform
