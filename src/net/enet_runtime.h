#pragma once

// Interno de src/net (no es público): arranca ENet si no lo está. Sin incluir enet.h aquí.

namespace rally::net {

void ensure_enet_initialized();

} // namespace rally::net
