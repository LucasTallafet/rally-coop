#pragma once

#include "platform/keys.h"

#include <cstdint>

namespace rally::platform {

// Pantalla de la demo (platform/demo_screen): ayuda, depuración, gráficas y contador. Su tabla
// entra en la de platform/key_tables (ayuda y docs/07 §8).
enum class ScreenAction : std::uint8_t {
    Help,
    DebugPanel,
    Perf,
    Graphs,
    Count,
};

extern const KeyTable kScreenKeys;

constexpr ActionRef action(ScreenAction a) { return {&kScreenKeys, static_cast<std::uint8_t>(a)}; }

} // namespace rally::platform
