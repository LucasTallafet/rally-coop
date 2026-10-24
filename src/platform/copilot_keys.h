#pragma once

#include "net/state_buffer.h"
#include "platform/keys.h"

#include <cstdint>
#include <span>
#include <string_view>

namespace rally::platform {

// Teclas propias de la ventana del copiloto (rally.exe --copiloto, lote 3 §2.7). Su tabla entra en
// la de platform/key_tables (docs/teclas.md); su contexto, kContextCopilot, no está en
// kContextAll: en el juego Z y B no hacen nada y su ayuda (H) no la enseña.
enum class CopilotAction : std::uint8_t {
    SimulatedNet,
    LookAtDash,
    Count,
};

extern const KeyTable kCopilotKeys;

constexpr ActionRef action(CopilotAction a) {
    return {&kCopilotKeys, static_cast<std::uint8_t>(a)};
}

// Contexto con el que la ventana del copiloto lee las teclas: las propias y las del juego que
// valen en ella (Tab, F1, F3, H, F11 y Esc; las demás no se leen).
inline constexpr std::uint8_t kCopilotKeyContext = kContextZone | kContextCopilot;

// Las filas que enseña la ayuda (H) del copiloto: sus ocho teclas (Tab con texto propio), Z y B.
std::span<const KeyBinding> copilot_help_table();

// Red simulada (Z): no, media (100 ± 30 ms, 5 % de pérdida) y mala (250 ± 80 ms, 20 %).
enum class SimulatedNet : std::uint8_t { Off, Medium, Bad };
SimulatedNet next_simulated_net(SimulatedNet level);
std::string_view simulated_net_name(SimulatedNet level);
// Lo que se le pide a Copilot::set_impairment (todo en 0 para «no»).
net::Impairment simulated_net_params(SimulatedNet level);

} // namespace rally::platform
