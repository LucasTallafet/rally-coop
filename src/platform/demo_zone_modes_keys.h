#pragma once

#include "platform/keys.h"

#include <cstdint>

namespace rally::platform {

// Modos de la zona de la demo (platform/demo_zone_modes): superficies, modo a ciegas, salida,
// recinto y mundo lejos del origen. Su tabla entra en la de platform/key_tables (ayuda y docs/07
// §8).
enum class ZoneModeAction : std::uint8_t {
    NextSurface,
    NextTrial,
    BlindMode,
    RoadSurface,
    ZoneStart,
    ToggleArena,
    FarOrigin,
    Count,
};

extern const KeyTable kZoneModeKeys;

constexpr ActionRef action(ZoneModeAction a) {
    return {&kZoneModeKeys, static_cast<std::uint8_t>(a)};
}

} // namespace rally::platform
