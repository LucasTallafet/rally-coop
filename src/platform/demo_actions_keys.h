#pragma once

#include "platform/keys.h"

#include <cstdint>

namespace rally::platform {

// Teclas de depuración que hacen de copiloto hasta que haya tablet (tarea 2.9, fase 3): guardar la
// tablet, sacarla y cancelar la acción en curso. Su tabla entra en la de platform/key_tables
// (ayuda y docs/teclas.md); las trata platform/demo_world con ActionDriver.
enum class ActionsAction : std::uint8_t {
    StowTablet,
    TakeTablet,
    CancelAction,
    Count,
};

extern const KeyTable kActionsKeys;

constexpr ActionRef action(ActionsAction a) {
    return {&kActionsKeys, static_cast<std::uint8_t>(a)};
}

} // namespace rally::platform
