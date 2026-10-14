#pragma once

#include "platform/keys.h"

#include <cstdint>

namespace rally::platform {

// Ventana: pantalla completa y salir (platform/window). Su tabla entra en la de
// platform/key_tables (ayuda y docs/07 §8).
enum class WindowAction : std::uint8_t {
    Fullscreen,
    Exit,
    Count,
};

extern const KeyTable kWindowKeys;

constexpr ActionRef action(WindowAction a) { return {&kWindowKeys, static_cast<std::uint8_t>(a)}; }

} // namespace rally::platform
