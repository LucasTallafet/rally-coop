#pragma once

#include "platform/keys.h"

#include <cstdint>

namespace rally::platform {

// Conductor en el coche de la demo (platform/demo_driver_feel): volumen. Su tabla entra en la de
// platform/key_tables (ayuda y docs/teclas.md).
enum class DriverFeelAction : std::uint8_t {
    Volume,
    Count,
};

extern const KeyTable kDriverFeelKeys;

constexpr ActionRef action(DriverFeelAction a) {
    return {&kDriverFeelKeys, static_cast<std::uint8_t>(a)};
}

} // namespace rally::platform
