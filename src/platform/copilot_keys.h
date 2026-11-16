#pragma once

#include "net/state_buffer.h"
#include "platform/keys.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace rally::platform {

// Teclas propias de la ventana del copiloto (rally.exe --copiloto, lote 3 §2.7). Su tabla entra en
// la de platform/key_tables (docs/teclas.md); su contexto, kContextCopilot, no está en
// kContextAll: en el juego Z no hace nada y su ayuda (H) no la enseña. Mirar con la cabeza no es
// una tecla del copiloto: es la fila Look de platform/demo_camera_keys, que vale en las dos
// ventanas.
enum class CopilotAction : std::uint8_t {
    SimulatedNet,
    MapOrientation, // la tablet (lote 6): orientación del mapa, A / B
    Pointer,        // Espacio mantenido: el ratón deja de mirar y sirve para pulsar en el mapa
    CursorNext,     // ↑ o la cruceta arriba: el cambio de dirección siguiente; mantenido, desliza
    CursorPrevious, // ↓ o la cruceta abajo: el anterior
    // Los diales (lote 7): ← y → mantenidas abren el principal por su lado; S, el secundario; D, la
    // distancia; 1–8 eligen (el 7 es la H con el principal abierto); Retroceso borra; F cambia la
    // forma de la distancia.
    DialLeft,
    DialRight,
    DialSecondary,
    DialDistance,
    Pick1,
    Pick2,
    Pick3,
    Pick4,
    Pick5,
    Pick6,
    Pick7,
    Pick8,
    Erase,
    DistanceForm,
    Count,
};

extern const KeyTable kCopilotKeys;

constexpr ActionRef action(CopilotAction a) {
    return {&kCopilotKeys, static_cast<std::uint8_t>(a)};
}

// Contexto con el que la ventana del copiloto lee las teclas: las propias y las del juego que
// valen en ella (Tab, F1, F3, H, F11 y Esc, que llevan kContextCopilot; las demás no se leen).
inline constexpr std::uint8_t kCopilotKeyContext = kContextCopilot;

// Las filas que enseña la ayuda (H) del copiloto: sus ocho filas de siempre (Tab y Mirar con texto
// propio y Z) y las tres de la tablet.
std::span<const KeyBinding> copilot_help_table();

// Red simulada (Z): no, media (100 ± 30 ms, 5 % de pérdida) y mala (250 ± 80 ms, 20 %).
enum class SimulatedNet : std::uint8_t { Off, Medium, Bad };
SimulatedNet next_simulated_net(SimulatedNet level);
std::string_view simulated_net_name(SimulatedNet level);
// Lo que se le pide a Copilot::set_impairment (todo en 0 para «no»).
net::Impairment simulated_net_params(SimulatedNet level);

} // namespace rally::platform
