#pragma once

// Arranque de ENet: lo hacen Host y Copilot al crearse, una sola vez por proceso (un objeto
// estático cierra ENet al salir). Un solo hilo: ENet no se toca desde otro.

#include <cstdint>

namespace rally::net {

// Reservas de memoria de ENet desde que arrancó. ENet reserva con malloc, no con operator new:
// el contador de new de los tests no las ve. Solo informativo.
std::uint64_t enet_allocation_count();

} // namespace rally::net
