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

// Velocidades de la dirección (tarea 2.5b; docs/05-decisiones.md, «Entrada»; se ajustan en la
// 2.10): los pasos hasta el tope crecen con la velocidad del coche, de parado a
// kSteerRateFullSpeed, interpolando en medio (steer_rates). Son el ritmo de unas manos: nunca
// giran las ruedas por su cuenta. Teclado: al tope en 0,25 s parado y en 0,6 s a 100 km/h o más
// mientras se mantiene; de vuelta al centro en 0,12 s y en 0,3 s al soltar. Stick y ratón: de 0
// al tope en 0,10 s parado y en 0,35 s a 100 km/h o más (unos 300 °/s y 86 °/s en las ruedas),
// igual al girar que al volver.
inline constexpr int kSteerDigitalRiseSteps = 60;
inline constexpr int kSteerDigitalFallSteps = 29;
inline constexpr int kSteerDigitalRiseStepsFast = 144;
inline constexpr int kSteerDigitalFallStepsFast = 72;
inline constexpr int kSteerAnalogSteps = 24;
inline constexpr int kSteerAnalogStepsFast = 84;
inline constexpr float kSteerRateFullSpeed = 100.0f / 3.6f; // m/s
// Acelerador digital (W/S o gatillos de 0 o 1): sube en 0,20 s y baja en 0,10 s.
inline constexpr int kThrottleDigitalRiseSteps = 48;
inline constexpr int kThrottleDigitalFallSteps = 24;

inline constexpr AxisRates kThrottleDigitalRates{rate_for_steps(kThrottleDigitalRiseSteps),
                                                 rate_for_steps(kThrottleDigitalFallSteps)};
// Acelerador analógico: sin limitador (de un extremo al otro en un paso).
inline constexpr AxisRates kThrottleAnalogRates{0, 0};
// Freno: las mismas velocidades que el acelerador, digital y analógico.
inline constexpr AxisRates kBrakeDigitalRates = kThrottleDigitalRates;
inline constexpr AxisRates kBrakeAnalogRates = kThrottleAnalogRates;
// Freno de mano (botón): aplicado a fondo en 0,05 s y suelto en lo mismo. Provisional.
inline constexpr int kHandbrakeSteps = 12;
inline constexpr AxisRates kHandbrakeRates{rate_for_steps(kHandbrakeSteps),
                                           rate_for_steps(kHandbrakeSteps)};

// Un paso del limitador: `applied` se acerca a `target` como mucho rise o fall. Hacia el
// centro usa fall y se para en 0 si el objetivo está al otro lado; desde ahí sigue con rise
// en el paso siguiente.
std::int32_t follow_target(std::int32_t applied, std::int32_t target, AxisRates rates);

// Velocidades de la dirección a `speed` (m/s), acotada a [0, kSteerRateFullSpeed]: los pasos
// se interpolan en línea recta entre los de parado y los de kSteerRateFullSpeed, se redondean al
// entero más cercano (los medios hacia arriba) y de ellos sale la velocidad (rate_for_steps).
AxisRates steer_rates(float speed, bool digital);

// Contexto de la dirección de un paso (sim/steer_assist.h lo calcula con el estado del coche al
// principio del paso; no es estado): velocidad de referencia para las velocidades de giro y
// topes del objetivo hacia cada lado, en unidades de eje (kAxisMax: sin recorte).
struct SteerContext {
    float speed = 0.0f;                // m/s: |v| horizontal del centro del eje delantero
    bool active = false;               // la ayuda calcula su ventana en este paso
    bool rear_sliding = false;         // la trasera pasa de su pico (contravolante hacia su lado)
    std::int32_t right_max = kAxisMax; // tope del objetivo hacia la derecha, en [0, kAxisMax]
    std::int32_t left_max = kAxisMax;  // tope hacia la izquierda (magnitud), en [0, kAxisMax]
};

// Recorte de la ayuda: signo(target)·min(|target|, tope de ese lado). Nunca cruza el 0 ni da
// más de lo que se pide.
std::int32_t clip_steer(std::int32_t target, const SteerContext& ctx);

// Valor aplicado de un eje y modo (digital o analógico) del último objetivo distinto de 0.
// Con objetivo 0 se usa ese modo: al soltar la tecla, la dirección vuelve al centro a la
// velocidad del teclado aunque haya un mando conectado con el stick quieto.
struct SmoothedAxis {
    std::int32_t applied = 0; // en [-kAxisMax, kAxisMax]
    bool digital = false;

    void step(std::int32_t target, bool target_digital, AxisRates digital_rates,
              AxisRates analog_rates);
};

// Mandos que la simulación aplica de verdad, siguiendo a SimInput por paso, y pulsaciones
// del paso. Por paso y no por fotograma: la respuesta es la misma a 30 y a 60 fps y queda
// en la grabación de entradas. Es todo el estado de la simulación que no está en el
// SaveState de Jolt: en el cambio de host (fase 5) viaja con el estado del juego.
struct ControlState {
    SmoothedAxis steer;
    SmoothedAxis throttle;
    SmoothedAxis brake;
    std::int32_t handbrake = 0;         // freno de mano aplicado, en [0, kAxisMax]
    std::uint16_t previous_buttons = 0; // botones del paso anterior
    std::uint16_t pressed = 0;          // botones que pasan de 0 a 1 en este paso

    // Una vez por paso, antes de usar los valores. La dirección: objetivo → recorte de la ayuda
    // (clip_steer) → limitador con las velocidades de `steer.speed`. Es el único limitador de
    // la dirección: si la ventana se mueve, las ruedas la siguen al ritmo de las manos.
    // Devuelve el objetivo de la dirección ya recortado (telemetría).
    std::int32_t step(const SimInput& input, const SteerContext& steer_context = {});
    // Recolocar: los ejes aplicados vuelven a 0. Los botones del paso anterior y el modo
    // digital de cada eje se quedan como están.
    void zero_axes();
};

} // namespace rally::sim
