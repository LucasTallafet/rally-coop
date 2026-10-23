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
};

// Busca --copiloto en la línea de órdenes. Sin él, `enabled` queda en falso y devuelve true. Con
// él, la dirección y el puerto son opcionales («127.0.0.1», «127.0.0.1:29471»); la dirección es una
// IP de cuatro números (sin nombres) y el puerto va de 1 a 65535. Si está mal escrito, devuelve
// false con el motivo en `error` (en español).
bool parse_copilot_args(int argc, char** argv, CopilotArgs& out, std::string& error);

} // namespace rally::platform
