#pragma once

#include "platform/keys.h"

#include <cstdint>

namespace rally::platform {

// Cámara de la demo (platform/demo_camera): vistas, cabeza, asiento y render en float del mundo. Su
// tabla entra en la de platform/key_tables (ayuda y docs/teclas.md).
enum class CameraAction : std::uint8_t {
    Camera,
    HeadMotion,
    SeatUp,
    SeatDown,
    SeatForward,
    SeatBack,
    SeatRight,
    SeatLeft,
    WorldFloat,
    Count,
};

extern const KeyTable kCameraKeys;

constexpr ActionRef action(CameraAction a) { return {&kCameraKeys, static_cast<std::uint8_t>(a)}; }

} // namespace rally::platform
