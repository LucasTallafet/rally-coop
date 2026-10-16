#pragma once

#include "platform/keys.h"

#include <cstdint>

namespace rally::platform {

// Medidas de la demo (platform/demo_measure): capturas, registro, bosque, ritmo, pausa e
// interpolación. Su tabla entra en la de platform/key_tables (ayuda y docs/teclas.md).
enum class MeasureAction : std::uint8_t {
    Capture,
    FrameLog,
    Forest,
    FrameRate,
    Vsync,
    Interpolation,
    Snap,
    Pause,
    Count,
};

extern const KeyTable kMeasureKeys;

constexpr ActionRef action(MeasureAction a) {
    return {&kMeasureKeys, static_cast<std::uint8_t>(a)};
}

} // namespace rally::platform
