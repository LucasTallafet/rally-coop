#pragma once

#include "sim/surface.h"

#include <array>
#include <cstddef>
#include <string>
#include <string_view>

namespace rally::sim {

// Coche definido en datos (data/cars/*.car). Todo en SI y radianes; el lector convierte las
// claves con unidad en el nombre (_deg, _kw). Ejes del coche: adelante -z, derecha +x,
// arriba +y; el origen está en el suelo, bajo el punto medio entre los ejes, con el coche en
// equilibrio. La tracción es provisional (la sustituye la tarea 2.6).

// Neumático en una superficie ([tire.<superficie>] del archivo). Las magnitudes son las que se
// entienden al ajustar; la forma de la curva (shape_*) la calcula el lector al cargar
// (sim/tire.h, derive_tire_shape).
struct TireSurface {
    float mu_peak = 0.0f;            // μ pico a F_z0
    float slip_angle_peak = 0.0f;    // rad: α del pico lateral
    float slip_ratio_peak = 0.0f;    // κ del pico longitudinal
    float slide_ratio = 0.0f;        // μ deslizando / μ pico (asíntota)
    float curvature_e = 0.0f;        // E de la curva (< 1)
    float load_sensitivity = 0.0f;   // k: μ(F_z) = μ_pico·(1 − k·(F_z/F_z0 − 1))
    float relaxation_long = 0.0f;    // m: σ_x
    float relaxation_lat = 0.0f;     // m: σ_y
    float rolling_resistance = 0.0f; // coeficiente × carga

    // Derivados al cargar.
    float shape_b = 0.0f;        // B de la curva normalizada (pico en ρ = 1)
    float shape_c = 0.0f;        // C, entre 1 y 2
    float tan_alpha_peak = 0.0f; // tan(slip_angle_peak)
    float slide_cap = 0.0f;      // ρ máximo de la huella a velocidad (sim/tire.h)
};

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

    // [steering]
    float max_steer = 0.0f; // rad: ángulo de la rueda interior con la dirección a fondo
    float ackermann = 0.0f; // fracción de Ackermann en la rueda exterior

    // [tire]: lo común a todas las superficies.
    float carcass_damping_long = 0.0f; // ζ del modo carrocería–huella, longitudinal
    float carcass_damping_lat = 0.0f;  // ζ del modo carrocería–huella, lateral
    float carcass_damping_fade = 0.0f; // m/s: el amortiguamiento de la carcasa llega a 0
    // [tire.<superficie>]: un juego por superficie, en el orden de Surface.
    std::array<TireSurface, kSurfaceCount> tires{};

    // [drive] (provisional, tarea 2.6)
    float drive_torque = 0.0f;      // N·m total a las ruedas
    float drive_power = 0.0f;       // W
    float drive_front_share = 0.0f; // fracción del par al eje delantero

    // [driver]
    float eye_left = 0.0f;              // m a la izquierda del eje del coche
    float eye_height = 0.0f;            // m sobre el suelo, en equilibrio
    float eye_behind_front_axle = 0.0f; // m

    // Derivado al cargar: F_z0, la carga estática media por rueda (m·g/4), N.
    float nominal_load = 0.0f;

    const TireSurface& tire(Surface surface) const {
        return tires[static_cast<std::size_t>(surface)];
    }
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
