#pragma once

#include <cstdint>
#include <type_traits>

namespace rally::sim {

inline constexpr std::int16_t kAxisMax = 32767;

// Bits de SimInput::buttons. Los de mantener (freno de mano) actúan mientras están; los de
// pulsación (marchas, recolocar) actúan en el paso en que pasan de 0 a 1
// (ControlState::pressed).
inline constexpr std::uint16_t kButtonHandbrake = 1u << 0;
inline constexpr std::uint16_t kButtonShiftUp = 1u << 1;
inline constexpr std::uint16_t kButtonShiftDown = 1u << 2;
// Recolocar: vuelve a la salida, parado y mirando al frente.
inline constexpr std::uint16_t kButtonReset = 1u << 3;
// La dirección viene de un control digital (teclado): la simulación la sube despacio y la
// devuelve al centro con las velocidades del teclado. Sin el bit, del stick (casi inmediata).
inline constexpr std::uint16_t kButtonSteerDigital = 1u << 4;
// El acelerador o el freno vienen de un control digital (teclado o gatillos que solo dan 0
// o 1): la simulación los sube y los baja con un limitador. Sin el bit, tal cual.
inline constexpr std::uint16_t kButtonThrottleDigital = 1u << 5;
inline constexpr std::uint16_t kButtonBrakeDigital = 1u << 6;
// Reservado: era el salto de la caja de la demo (tareas 2.1–2.3). La simulación lo ignora. No
// se reutiliza ni se renumera: las grabaciones de los formatos 1 y 2 lo llevan y se siguen
// leyendo igual.
inline constexpr std::uint16_t kButtonReserved7 = 1u << 7;

// Entrada de un paso de simulación. Solo enteros, tamaño fijo y sin relleno: la grabación
// por paso (io/recording.h) la guarda campo a campo y la repite exacta. La traducción desde
// el teclado o el mando va en src/platform; aquí nunca entra un float del dispositivo.
struct SimInput {
    // Objetivos: la simulación los sigue con los limitadores de sim/input_smoothing.h.
    std::int16_t steer = 0; // giro a la derecha (+) o a la izquierda (-), en [-kAxisMax, kAxisMax]
    std::int16_t throttle = 0; // acelerador, en [0, kAxisMax]
    std::int16_t brake = 0;    // freno, en [0, kAxisMax]
    std::uint16_t buttons = 0; // kButton*

    friend bool operator==(const SimInput&, const SimInput&) = default;
};

static_assert(sizeof(SimInput) == 8);
static_assert(std::has_unique_object_representations_v<SimInput>,
              "SimInput no puede tener relleno");

} // namespace rally::sim
