#pragma once

#include "platform/keys.h"

#include <cstdint>

namespace rally::platform {

// Mundo y grabación de la demo (platform/demo_world): coche, reinicio y ajustes que cambian la
// simulación o la entrada. Su tabla entra en la de platform/key_tables (ayuda y docs/teclas.md).
enum class WorldAction : std::uint8_t {
    SteerAssist,
    Gearbox,
    SteerDevice,
    SensitivityDown,
    SensitivityUp,
    Abs,
    TractionControl,
    Suspension,
    ReloadCar,
    NextCar,
    ResetWorld,
    Count,
};

extern const KeyTable kWorldKeys;

constexpr ActionRef action(WorldAction a) { return {&kWorldKeys, static_cast<std::uint8_t>(a)}; }

} // namespace rally::platform
