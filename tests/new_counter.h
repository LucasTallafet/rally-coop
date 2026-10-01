#pragma once

#include <cstdint>

namespace rally::tests {

// Número de llamadas al operator new global (cualquier forma) desde que arrancó rally_tests.
// new_counter.cpp sustituye los operadores globales solo en este ejecutable. Jolt no entra
// en esta cuenta: reserva con malloc a través de su propio asignador (JPH::Allocate).
std::uint64_t global_new_count();

} // namespace rally::tests
