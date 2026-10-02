#pragma once

#include "sim/sim_input.h"

#include <cstdint>

namespace rally::sim {

// Velocidades de un limitador, en unidades de eje (kAxisMax = a fondo) por paso de 240 Hz.
// rise = 0: sin limitador (el objetivo se aplica tal cual, también al cambiar de sentido).
struct AxisRates {
    std::int32_t rise = 0; // alejándose del centro
    std::int32_t fall = 0; // volviendo al centro (también al cambiar de sentido, hasta el 0)
};

// Velocidad que recorre de 0 a kAxisMax en exactamente `steps` pasos.
constexpr std::int32_t rate_for_steps(int steps) { return (kAxisMax + steps - 1) / steps; }

// Velocidades provisionales hasta la fase 2 (docs/05-decisiones.md, «Entrada»).
// Dirección con teclado: al tope en 0,25 s mientras se mantiene y de vuelta al centro en
// 0,12 s al soltar. Con stick, en 1/30 s: casi inmediata, solo quita el salto de un paso.
inline constexpr int kSteerDigitalRiseSteps = 60;
inline constexpr int kSteerDigitalFallSteps = 29;
inline constexpr int kSteerAnalogSteps = 8;
// Acelerador digital (W/S o gatillos de 0 o 1): sube en 0,20 s y baja en 0,10 s.
inline constexpr int kThrottleDigitalRiseSteps = 48;
inline constexpr int kThrottleDigitalFallSteps = 24;

inline constexpr AxisRates kSteerDigitalRates{rate_for_steps(kSteerDigitalRiseSteps),
                                              rate_for_steps(kSteerDigitalFallSteps)};
inline constexpr AxisRates kSteerAnalogRates{rate_for_steps(kSteerAnalogSteps),
                                             rate_for_steps(kSteerAnalogSteps)};
inline constexpr AxisRates kThrottleDigitalRates{rate_for_steps(kThrottleDigitalRiseSteps),
                                                 rate_for_steps(kThrottleDigitalFallSteps)};
// Acelerador analógico: sin limitador (de un extremo al otro en un paso).
inline constexpr AxisRates kThrottleAnalogRates{0, 0};

// Un paso del limitador: `applied` se acerca a `target` como mucho rise o fall. Hacia el
// centro usa fall y se para en 0 si el objetivo está al otro lado; desde ahí sigue con rise
// en el paso siguiente.
std::int32_t follow_target(std::int32_t applied, std::int32_t target, AxisRates rates);

// Valor aplicado de un eje y modo (digital o analógico) del último objetivo distinto de 0.
// Con objetivo 0 se usa ese modo: al soltar la tecla, la dirección vuelve al centro a la
// velocidad del teclado aunque haya un mando conectado con el stick quieto.
struct SmoothedAxis {
    std::int32_t applied = 0; // en [-kAxisMax, kAxisMax]
    bool digital = false;

    void step(std::int16_t target, bool target_digital, AxisRates digital_rates,
              AxisRates analog_rates);
};

// Dirección y acelerador que la simulación aplica de verdad, siguiendo a SimInput por paso.
// Por paso y no por fotograma: la respuesta es la misma a 30 y a 60 fps y queda en la
// grabación de entradas. Es estado de la simulación que no está en el SaveState de Jolt:
// en el cambio de host (fase 5) viaja con el estado del juego, como previous_buttons.
struct ControlState {
    SmoothedAxis steer;
    SmoothedAxis throttle;

    void step(const SimInput& input);
};

} // namespace rally::sim
