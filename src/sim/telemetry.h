#pragma once

#include "sim/sim_input.h"

#include <array>
#include <cstdint>

namespace rally::sim {

// Telemetría de una rueda tras un paso (sim/car_controller.h, WheelOutput). Ejes del contacto:
// longitudinal hacia delante y lateral hacia la derecha.
struct WheelTelemetry {
    std::uint8_t contact = 0;  // 1 con la rueda en el suelo
    std::int8_t surface = -1;  // superficie del material del contacto (Surface), -1 en el aire
    float load = 0.0f;         // N: carga normal (suspensión del paso anterior)
    float compression = 0.0f;  // m desde la extensión máxima de la suspensión
    float kappa = 0.0f;        // deslizamiento relajado κ_r = u / σ_x (σ de la superficie)
    float alpha = 0.0f;        // rad: α_r = atan(w / σ_y), < 0 yendo hacia su derecha
    float slip_x = 0.0f;       // m/s: velocidad de deslizamiento ω·r − v_x
    float slip_y = 0.0f;       // m/s: velocidad de deslizamiento −v_y
    float fx = 0.0f;           // N: fuerza longitudinal media de los subpasos (adelante +)
    float fy = 0.0f;           // N: fuerza lateral media de los subpasos (derecha +)
    float fx_pure = 0.0f;      // N: la longitudinal que daría κ_r solo (sin la combinada)
    float fy_pure = 0.0f;      // N: la lateral que daría α_r solo (sin la combinada)
    float mu = 0.0f;           // μ efectivo: pico con la sensibilidad a la carga, 0 en el aire
    float omega = 0.0f;        // rad/s: giro de la rueda (> 0 rodando hacia delante)
    float brake_torque = 0.0f; // N·m pedido por el freno y el freno de mano
    float h = 0.0f;            // m: media del microrrelieve sobre la huella
};

// Telemetría del coche tras un paso (SimWorld::telemetry). Tamaño fijo y solo
// lectura: la simulación la rellena al final de step() si la captura está encendida y nada la
// vuelve a leer dentro de la simulación. Unidades SI. Las columnas del CSV, sus unidades y sus
// signos están en tools/telemetry/columns.md (lo genera rally_replay columns --md).
//
// Convenciones (ejes del coche: adelante = -z local, derecha = +x local, arriba = +y local):
//   heading  = atan2(adelante.x, -adelante.z): 0 mirando a -z del mundo; crece girando a la
//              derecha (como el rumbo psi de la zona de pruebas).
//   yaw_rate = -(ω · arriba): > 0 girando a la derecha (como SimInput::steer > 0).
//   beta     = atan2(v · derecha, v · adelante): > 0 cuando la velocidad apunta a la derecha
//              del morro. Sin suavizar ni recortar a baja velocidad.
//   a        = dv/dt del centro de masas en el paso (cinemática, sin la gravedad); 0 en el
//              paso que recoloca.
// Las tareas que añaden estado al coche (ruedas en la 2.4, neumático en la 2.5, motor y
// transmisión en la 2.6, ayudas en la 2.8) añaden aquí sus campos y sus columnas.
struct CarTelemetry {
    std::int64_t step = 0; // pasos desde que se construyó el mundo (1 tras el primero)

    // Centro de masas en el mundo (m).
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;

    // Velocidad del centro de masas (m/s): en ejes del mundo y en ejes del coche.
    float vx = 0.0f;
    float vy = 0.0f;
    float vz = 0.0f;
    float v_forward = 0.0f;
    float v_right = 0.0f;
    float v_up = 0.0f;
    float speed = 0.0f; // |v|

    float heading = 0.0f;  // rad, en (-π, π]
    float yaw_rate = 0.0f; // rad/s
    float beta = 0.0f;     // rad, en (-π, π]

    // Aceleración del centro de masas (m/s²): en ejes del mundo y en ejes del coche.
    float ax = 0.0f;
    float ay = 0.0f;
    float az = 0.0f;
    float a_forward = 0.0f;
    float a_right = 0.0f;

    SimInput target; // entrada del paso (objetivos y botones mantenidos)

    // Mandos aplicados (ControlState), normalizados: dirección en [-1, 1], el resto en [0, 1].
    float steer = 0.0f;
    float throttle = 0.0f;
    float brake = 0.0f;
    float handbrake = 0.0f;
    std::uint16_t pressed = 0; // botones que pasan de 0 a 1 en el paso (kButton*)

    // Dirección (tarea 2.5b): objetivo tras el recorte de la ayuda ([-1, 1]), modo del eje
    // (digital: teclado; analógico: stick o ratón), ventana de la ayuda (topes del objetivo hacia
    // cada lado, en [0, 1]; 1 sin recorte) y velocidad de referencia de las manos.
    float steer_aided = 0.0f;
    bool steer_digital = false;
    bool assist_active = false;
    bool assist_rear_sliding = false;
    float assist_right_max = 1.0f;
    float assist_left_max = 1.0f;
    float steer_speed = 0.0f; // m/s: |v| horizontal del centro del eje delantero

    // Superficie bajo el centro de masas (índice de Surface), o -1 sin zona de pruebas.
    std::int8_t surface = -1;

    // Cronómetro de la pista ciega: vueltas completas y pasos de la vuelta en curso (-1 si no
    // hay vuelta en curso o no hay zona).
    std::int32_t laps = 0;
    std::int64_t lap_steps = -1;

    float impact = 0.0f; // m/s (SimWorld::last_impact)

    // Coche (tarea 2.4): selector provisional (-1 R, 0 N, 1 adelante) y ruedas en el orden
    // FL, FR, RL, RR.
    std::int8_t gear = 0;
    std::array<WheelTelemetry, 4> wheels{};
};

} // namespace rally::sim
