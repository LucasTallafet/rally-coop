#pragma once

#include <cstdint>
#include <type_traits>

namespace rally::sim {

inline constexpr std::int16_t kAxisMax = 32767;

// Bits de SimInput::buttons.
inline constexpr std::uint16_t kButtonJump = 1u << 0;
// La dirección viene de un control digital (teclado): la simulación la sube despacio y la
// devuelve al centro con las velocidades del teclado. Sin el bit, del stick (casi inmediata).
inline constexpr std::uint16_t kButtonSteerDigital = 1u << 1;
// El acelerador viene de un control digital (W/S o gatillos que solo dan 0 o 1): la
// simulación lo sube y lo baja con un limitador. Sin el bit, se aplica tal cual.
inline constexpr std::uint16_t kButtonThrottleDigital = 1u << 2;

// Entrada de un paso de simulación. Solo enteros, tamaño fijo y sin relleno: la grabación
// por paso (fase 2) la guarda byte a byte y la reproduce exacta. La traducción desde el
// teclado o el mando va en src/platform; aquí nunca entra un float del dispositivo.
struct SimInput {
    // Objetivos: la simulación los sigue con los limitadores de sim/input_smoothing.h.
    std::int16_t throttle = 0; // empuje hacia delante (+) o atrás (-), en [-kAxisMax, kAxisMax]
    std::int16_t steer = 0; // giro a la derecha (+) o a la izquierda (-), en [-kAxisMax, kAxisMax]
    std::uint16_t buttons = 0; // kButton*
};

static_assert(sizeof(SimInput) == 6);
static_assert(std::has_unique_object_representations_v<SimInput>,
              "SimInput no puede tener relleno");

} // namespace rally::sim
