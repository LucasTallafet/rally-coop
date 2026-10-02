#pragma once

#include "sim/sim_input.h"

#include <cstdint>

namespace rally::platform {

// Forma de la entrada del conductor antes de la simulación: zona muerta y curva del stick,
// gatillos, mezcla de teclado y mando, mando activo y vibración. Nada depende del tiempo
// del fotograma: el suavizado por paso está en sim/input_smoothing.h.
// Sin raylib ni SDL: lo usan platform/input y platform/gamepad, y se prueba sin ventana.
// Constantes provisionales hasta la fase 2 (docs/05-decisiones.md, «Entrada»).

// Dirección: solo el eje X del stick. Un stick gastado que deriva en vertical no deja pasar
// giro. Por debajo de la zona interior da 0; desde la exterior, a fondo (algunos sticks no
// llegan a 1); entre las dos se reescala a [0, 1] y se aplica la curva |x|^exponente.
inline constexpr float kSteerInnerDeadzone = 0.10f;
inline constexpr float kSteerOuterDeadzone = 0.95f;
inline constexpr float kSteerCurveExponent = 1.5f;
// Gatillos en [0, 1]: por debajo, 0; el resto se reescala a [0, 1].
inline constexpr float kTriggerDeadzone = 0.05f;
// Un gatillo es analógico en cuanto da un valor entre estos dos (Switch Pro: solo 0 o 1).
inline constexpr float kTriggerAnalogLow = 0.05f;
inline constexpr float kTriggerAnalogHigh = 0.95f;

// Eje X del stick en [-1, 1] → dirección en [-1, 1].
float shape_steer(float x);
// Gatillo crudo → [0, 1]. Recorta lo negativo: raylib da -1 hasta el primer movimiento.
float shape_trigger(float value);
// [-1, 1] → eje de SimInput, redondeado y recortado.
std::int16_t to_axis(float value);

// Lectura cruda de un mando conectado en un fotograma, ya pasada a float.
struct PadReading {
    std::uint32_t id = 0;       // identificador estable mientras sigue conectado (nunca 0)
    float left_x = 0.0f;        // [-1, 1], derecha positiva
    float left_y = 0.0f;        // [-1, 1]; ni gira ni activa el mando (deriva de sticks gastados)
    float left_trigger = 0.0f;  // [0, 1] (puede llegar < 0 de raylib; se recorta)
    float right_trigger = 0.0f; // [0, 1]
    bool jump = false;          // botón de salto (por posición: el de abajo de la cara derecha)
    bool any_button = false;    // cualquier botón pulsado
};

// Estado del mando activo ya con forma, listo para mezclar con el teclado.
struct PadState {
    bool active = false;
    float steer = 0.0f;    // [-1, 1]
    float throttle = 0.0f; // [-1, 1]: gatillo derecho − gatillo izquierdo
    bool throttle_digital = false;
    bool jump = false;
};

// Mando activo = el último que ha movido un eje por encima de su zona muerta o pulsado un
// botón; si se desconecta, no hay mando activo (sus ejes pasan a 0 en ese mismo fotograma)
// hasta que se use otro. Recuerda también, por mando conectado, si sus gatillos son
// analógicos: digitales hasta que den un valor intermedio; al desconectar se olvida.
class PadTracker {
public:
    static constexpr int kMaxPads = 8;

    // Mandos conectados en este fotograma (como mucho kMaxPads; el resto no se mira).
    // Devuelve el estado del mando activo.
    PadState update(const PadReading* pads, int count);

    std::uint32_t active_id() const { return active_; } // 0 si ninguno
    bool trigger_digital(std::uint32_t id) const;

private:
    struct Known {
        std::uint32_t id = 0;
        bool analog_trigger = false;
    };
    Known known_[kMaxPads];
    int known_count_ = 0;
    std::uint32_t active_ = 0;
};

// Teclado del conductor en un fotograma.
struct KeyboardState {
    bool left = false;
    bool right = false;
    bool forward = false;
    bool back = false;
    bool jump = false;
};

// Mezcla teclado y mando activo: en cada eje gana el de mayor magnitud (empate: el mando).
// Si gana el teclado, marca el bit digital del eje; en el acelerador también si gana un
// gatillo digital. El salto es el OR de los dos. Los botones de pulsación (salto) salen
// sin retener: platform/input les aplica ButtonLatch.
sim::SimInput combine_inputs(const KeyboardState& keys, const PadState& pad);

// Vibración por impacto: por encima del umbral, fuerza proporcional desde kRumbleMinStrength
// hasta 1 en kImpactFull, durante kRumbleMs.
inline constexpr float kImpactThreshold = 1.0f; // m/s en un paso (sim::SimWorld::last_impact)
inline constexpr float kImpactFull = 6.0f;
inline constexpr float kRumbleMinStrength = 0.3f;
inline constexpr int kRumbleMs = 150;

struct Rumble {
    float low = 0.0f;    // motor de baja frecuencia, [0, 1]
    float high = 0.0f;   // motor de alta frecuencia, [0, 1]
    int duration_ms = 0; // 0: no vibrar
};

// Pico de impacto de los pasos de un fotograma → vibración (duración 0 si no llega).
Rumble rumble_for_impact(float peak_impact);

} // namespace rally::platform
