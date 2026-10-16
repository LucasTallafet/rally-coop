#pragma once

#include "platform/keys.h"

#include <cstdint>

namespace rally::platform {

// Conducir: teclado y mando del coche (platform/input, platform/gamepad). Su tabla entra en la de
// platform/key_tables (ayuda y docs/teclas.md).
enum class DriveAction : std::uint8_t {
    Throttle,
    Brake,
    SteerLeft,
    SteerRight,
    Handbrake,
    ShiftDown,
    ShiftUp,
    Reset,
    RecenterWheel,
    Count,
};

extern const KeyTable kDriveKeys;

constexpr ActionRef action(DriveAction a) { return {&kDriveKeys, static_cast<std::uint8_t>(a)}; }

} // namespace rally::platform
