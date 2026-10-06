#pragma once

#include "sim/car_data.h"
#include "sim/drivetrain.h"
#include "sim/input_smoothing.h"

#include <array>
#include <cstdint>

namespace rally::sim {

// Ayuda de dirección (tarea 2.5b; docs/05-decisiones.md, «Dirección»): limita el ángulo de las
// ruedas delanteras al de máximo agarre de la superficie respecto a su velocidad. Solo recorta
// lo que pide el jugador: el aplicado tiene el signo de la entrada, nunca es mayor que ella y
// nunca cruza el 0. Sin estado: se calcula al principio de cada paso con el estado del coche.
//
// Ventana de cada lado del giro, sobre la rueda delantera exterior (la de la izquierda para
// girar a la derecha y al revés; si va sin carga, la interior):
//   borde = δ_vel + α_pico (derecha) o δ_vel − α_pico (izquierda), con δ_vel el ángulo de la
//   velocidad del cuerpo en esa rueda (ejes del coche, + derecha) y α_pico el de su superficie.
// El borde, pasado a unidades de eje con la inversa de Ackermann y acotado a [0, kAxisMax], es
// el tope del objetivo hacia ese lado. Entre kAssistStartSpeed y kAssistFullSpeed el tope se
// mezcla con el giro completo; por debajo, giro completo.
//
// Contravolante: la trasera desliza (|δ_vel,tras| > α_pico,tras, con alguna trasera con carga)
// y la entrada va hacia el lado al que se va la trasera (signo de δ_vel,tras). Ese lado no
// recorta. En todos los demás casos (recta, giro con agarre, cambio de lado en una chicane)
// recortan los dos lados.
//
// Sin recorte: ayuda desactivada, por debajo de kAssistStartSpeed, marcha atrás puesta o
// velocidad hacia atrás, y las dos delanteras sin carga (en el aire).

inline constexpr float kAssistStartSpeed = 15.0f / 3.6f; // m/s: desde aquí, mezcla
inline constexpr float kAssistFullSpeed = 30.0f / 3.6f;  // m/s: desde aquí, la ventana entera

// Ayudas del jugador que cambian la simulación. Van en la descripción del mundo y en la
// grabación; cambiar una reconstruye el mundo. La 2.8 añade ABS y control de tracción.
struct DriverAids {
    bool steer_assist = false;
    GearboxMode gearbox = GearboxMode::Automatic; // tarea 2.6

    friend bool operator==(const DriverAids&, const DriverAids&) = default;
};

// Rueda delantera que decide un lado del giro.
struct SteerSide {
    bool loaded = false;    // con carga (si no, ese lado no tiene ventana)
    bool outer = true;      // la exterior del giro; false: la interior (la exterior en el aire)
    float vel_angle = 0.0f; // rad: δ_vel de esa rueda, + derecha
    float tan_alpha_peak = 0.0f; // de la superficie que pisa
};

// Estado del coche al principio del paso que usa la ayuda.
struct SteerAssistInput {
    float speed = 0.0f;               // m/s: |v| horizontal del centro del eje delantero
    float forward_speed = 0.0f;       // m/s: su componente hacia delante
    bool reverse = false;             // marcha atrás puesta
    std::array<SteerSide, 2> side{};  // 0: giro a la derecha (FL); 1: a la izquierda (FR)
    bool rear_loaded = false;         // alguna trasera con carga
    float rear_vel_angle = 0.0f;      // rad: δ_vel del centro del eje trasero, + derecha
    float rear_tan_alpha_peak = 0.0f; // el mayor de las traseras con carga
};

inline constexpr int kSteerRight = 0;
inline constexpr int kSteerLeft = 1;

// Contexto de la dirección del paso: velocidad para las velocidades de giro y topes.
SteerContext steer_context(const SteerAssistInput& in, const CarData& car, bool assist);

// Ángulo de la rueda (rad, ≥ 0) con la dirección en `axis` ∈ [0, 1]: la interior gira
// axis·max_steer; la exterior, la mezcla de ese ángulo con el de Ackermann puro.
float wheel_angle_for_axis(const CarData& car, float axis, bool outer);
// Su inversa: la dirección en [0, 1] que da `angle` en esa rueda (acotada a [0, 1]).
float axis_for_wheel_angle(const CarData& car, float angle, bool outer);

} // namespace rally::sim
