#pragma once

#include <cstdint>
#include <type_traits>

namespace rally::sim {

inline constexpr std::int16_t kAxisMax = 32767;

// Bits de SimInput::buttons.
inline constexpr std::uint16_t kButtonJump = 1u << 0;

// Entrada de un paso de simulación. Solo enteros, tamaño fijo y sin relleno: la grabación
// por paso (fase 2) la guarda byte a byte y la reproduce exacta. La traducción desde el
// teclado o el mando va en src/platform; aquí nunca entra un float del dispositivo.
struct SimInput {
    std::int16_t move_x = 0;   // empuje hacia +x del mundo, en [-kAxisMax, kAxisMax]
    std::int16_t move_z = 0;   // empuje hacia +z del mundo, en [-kAxisMax, kAxisMax]
    std::uint16_t buttons = 0; // kButton*
};

static_assert(sizeof(SimInput) == 6);
static_assert(std::has_unique_object_representations_v<SimInput>,
              "SimInput no puede tener relleno");

} // namespace rally::sim
