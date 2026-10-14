#pragma once

#include "platform/keys.h"

#include <span>

namespace rally::platform {

// Las tablas de teclas de todos los componentes, en el orden en que se juntan (key_table()). Un
// componente nuevo con teclas añade aquí su tabla: es el único sitio que las nombra todas.
std::span<const KeyTable* const> key_tables();

} // namespace rally::platform
