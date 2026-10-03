#pragma once

#include <cstdint>

namespace rally::sim {

// Superficie del suelo. El valor es el índice del material en el suelo de Jolt y va en las
// grabaciones: no se reordena.
enum class Surface : std::uint8_t { Asphalt, Gravel, Dirt, Snow, Sand };

inline constexpr int kSurfaceCount = 5;

// Nombre en español para el overlay y rally_replay ("asfalto", "grava"...).
const char* surface_name(Surface surface);
// Nombre en inglés para el JSON de la zona ("asphalt", "gravel"...).
const char* surface_key(Surface surface);

} // namespace rally::sim
