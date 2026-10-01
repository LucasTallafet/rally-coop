#pragma once

// Comprobación de invariantes de la simulación, activa también en Release. Sin excepciones:
// si falla, escribe el mensaje en stderr, avisa al manejador (en el juego, un cuadro de
// mensaje) y aborta.

namespace rally::sim {

using CheckHandler = void (*)(const char* message);

// Lo instala la plataforma al arrancar. Sin manejador, solo se escribe en stderr.
void set_check_handler(CheckHandler handler);

[[noreturn]] void check_failed(const char* message, const char* file, int line);

} // namespace rally::sim

#define RALLY_CHECK(condition, message)                                                            \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            ::rally::sim::check_failed(message, __FILE__, __LINE__);                               \
        }                                                                                          \
    } while (false)
