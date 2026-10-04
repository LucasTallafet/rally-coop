#pragma once

#include "sim/surface.h"

#include <string>
#include <string_view>

namespace rally::sim {

// Coche definido en datos (data/cars/*.car). Todo en SI y radianes; el lector convierte las
// claves con unidad en el nombre (_deg, _kw). Ejes del coche: adelante -z, derecha +x,
// arriba +y; el origen está en el suelo, bajo el punto medio entre los ejes, con el coche en
// equilibrio. El neumático y la tracción son provisionales (los sustituyen las tareas 2.5 y
// 2.6).
struct CarData {
    // [chassis]
    float mass = 0.0f;          // kg, con ocupantes y combustible
    float front_weight = 0.0f;  // fracción del peso sobre el eje delantero
    float wheelbase = 0.0f;     // m
    float track = 0.0f;         // m
    float cg_height = 0.0f;     // m sobre el suelo
    float inertia_yaw = 0.0f;   // kg·m² (eje y)
    float inertia_pitch = 0.0f; // kg·m² (eje x)
    float inertia_roll = 0.0f;  // kg·m² (eje z)
    float box_length = 0.0f;    // m: caja de colisión de la carrocería
    float box_width = 0.0f;
    float box_height = 0.0f;
    float ground_clearance = 0.0f; // m: del suelo a la cara de abajo de la caja, en equilibrio
    float cda = 0.0f;              // m²: coeficiente aerodinámico por área frontal

    // [wheels]
    float wheel_radius = 0.0f;
    float wheel_width = 0.0f;
    float wheel_inertia = 0.0f;    // kg·m², rueda, freno y palier
    float footprint_length = 0.0f; // m: largo de la huella (la media de h)

    // [suspension]
    float travel = 0.0f;          // m: recorrido total
    float frequency_front = 0.0f; // Hz
    float frequency_rear = 0.0f;
    float damping_ratio = 0.0f;  // fracción del crítico
    float antiroll_front = 0.0f; // N/m
    float antiroll_rear = 0.0f;

    // [brakes]
    float brake_torque = 0.0f;      // N·m, total con el freno a fondo
    float brake_front_share = 0.0f; // fracción al eje delantero
    float handbrake_torque = 0.0f;  // N·m por rueda trasera
    float hold_frequency = 0.0f;    // Hz: muelle del ancla de parada
    float hold_speed = 0.0f;        // m/s: por debajo, el freno ancla la rueda

    // [steering]
    float max_steer = 0.0f; // rad: ángulo de la rueda interior con la dirección a fondo
    float ackermann = 0.0f; // fracción de Ackermann en la rueda exterior

    // [tire] (provisional, tarea 2.5)
    float long_stiffness = 0.0f; // Cκ / Fz, por unidad de κ
    float lat_stiffness = 0.0f;  // Cα / Fz, por radián
    float mu_asphalt = 0.0f;
    float mu_dirt = 0.0f;
    float mu_gravel = 0.0f;
    float mu_snow = 0.0f;
    float mu_sand = 0.0f;
    float rolling_resistance = 0.0f; // coeficiente × carga
    float min_slip_speed = 0.0f;     // m/s: denominador mínimo de κ y α
    float low_speed_blend = 0.0f;    // m/s: por debajo, mezcla con el amortiguamiento lateral

    // [drive] (provisional, tarea 2.6)
    float drive_torque = 0.0f;      // N·m total a las ruedas
    float drive_power = 0.0f;       // W
    float drive_front_share = 0.0f; // fracción del par al eje delantero

    // [driver]
    float eye_left = 0.0f;              // m a la izquierda del eje del coche
    float eye_height = 0.0f;            // m sobre el suelo, en equilibrio
    float eye_behind_front_axle = 0.0f; // m

    float mu(Surface surface) const;
};

struct CarParseResult {
    bool ok = false;
    std::string error; // "línea N: ..." si !ok
    CarData car;
};

// Lee el texto de un .car: secciones [nombre], "clave = valor" y comentarios con #. Los
// números se leen con std::from_chars (no dependen del idioma del proceso). Una clave
// desconocida, repetida o fuera de sección, un número mal escrito, un valor fuera de rango o
// una clave que falta son errores con su número de línea.
CarParseResult parse_car(std::string_view text);

// parse_car o aborta (RALLY_CHECK) con el error y `name` delante. Al arrancar y en los tests.
CarData load_car_or_abort(std::string_view text, std::string_view name);

} // namespace rally::sim
