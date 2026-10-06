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
// equilibrio. Las velocidades de giro del motor se leen en rpm (_rpm) y se guardan en rad/s; la
// presión del turbo, en bar (_bar) y se guarda en Pa.

// Marchas adelante de la caja.
inline constexpr int kGearCount = 5;
// Puntos de la curva de par ([engine.torque]).
inline constexpr int kMaxTorquePoints = 16;

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

    // [engine] (tarea 2.6; sim/drivetrain.h)
    float idle_speed = 0.0f;      // rad/s
    float limiter_speed = 0.0f;   // rad/s: el corte llega a 0 aquí
    float limiter_ramp = 0.0f;    // rad/s: ancho del corte, por debajo de limiter_speed
    float engine_inertia = 0.0f;  // kg·m², con volante y embrague
    float engine_friction = 0.0f; // N·m·s/rad: freno motor con el acelerador cerrado = rozamiento·ω
    float torque_scale = 0.0f;    // 1 = Evo 1; hasta ~1,3 ≈ Evo 2 (D2)
    // [engine.torque]: par sin presión, "rpm = N·m", velocidades crecientes.
    int torque_points = 0;
    std::array<float, kMaxTorquePoints> torque_speed{}; // rad/s
    std::array<float, kMaxTorquePoints> torque_atm{};   // N·m

    // [turbo]
    float boost_max = 0.0f;             // Pa (0: atmosférico)
    float boost_gain = 0.0f;            // 1/Pa: T = T_atm·(1 + g·p)
    float boost_threshold_speed = 0.0f; // rad/s: por debajo, objetivo 0
    float boost_full_speed = 0.0f;      // rad/s: desde aquí, objetivo pleno
    float boost_rise_time = 0.0f;       // s: constante de subida a boost_rise_speed
    float boost_rise_speed = 0.0f;      // rad/s
    float boost_fall_time = 0.0f;       // s: constante de bajada

    // [clutch]
    float clutch_capacity_factor = 0.0f; // × par máximo a plena presión × torque_scale
    float bite_fraction = 0.0f;          // capacidad en arranque al llegar a la rampa (0..1)
    float engage_speed = 0.0f;           // rad/s: en arranque, capacidad 0 por debajo
    float launch_speed = 0.0f;           // rad/s: el motor en el arranque a fondo
    float launch_ramp = 0.0f;            // rad/s: rampa de la capacidad en arranque
    float open_speed = 0.0f;             // rad/s: cerrado vuelve a arranque por debajo

    // [gearbox]: relaciones totales (con el grupo).
    float reverse_ratio = 0.0f; // positivo en el archivo; la marcha atrás lo invierte
    float gear_ratio_1 = 0.0f;
    float gear_ratio_2 = 0.0f;
    float gear_ratio_3 = 0.0f;
    float gear_ratio_4 = 0.0f;
    float gear_ratio_5 = 0.0f;
    float shift_time = 0.0f;       // s: corte de par y embrague abierto
    float upshift_full = 0.0f;     // rad/s: automático, subir con el acelerador a fondo
    float upshift_closed = 0.0f;   // rad/s: ... y a 0 (lineal entre medias)
    float downshift_full = 0.0f;   // rad/s: bajar
    float downshift_closed = 0.0f; // rad/s
    float drift_hold_time = 0.0f;  // s: espera máxima de la subida en derrape
    float drift_angle = 0.0f;      // rad: |β| desde el que hay derrape

    // [driveline]
    float drive_front_share = 0.0f; // parte del par al eje delantero (0: propulsión)
    float viscous = 0.0f;           // N·m·s/rad del viscoacoplador central, entre ejes
    float lsd_front_preload = 0.0f; // N·m
    float lsd_front_drive = 0.0f;   // par de bloqueo / par de entrada al eje, acelerando
    float lsd_front_coast = 0.0f;   // ... reteniendo
    float lsd_rear_preload = 0.0f;
    float lsd_rear_drive = 0.0f;
    float lsd_rear_coast = 0.0f;

    // [driver]
    float eye_left = 0.0f;              // m a la izquierda del eje del coche
    float eye_height = 0.0f;            // m sobre el suelo, en equilibrio
    float eye_behind_front_axle = 0.0f; // m

    // Derivados al cargar.
    float nominal_load = 0.0f;    // N: F_z0, la carga estática media por rueda (m·g/4)
    float clutch_capacity = 0.0f; // N·m
    float idle_throttle = 0.0f;   // acelerador que sostiene el ralentí sin carga
    int shift_steps = 0;          // pasos de 1/240 s del cambio
    int drift_hold_steps = 0;     // pasos de la espera en derrape

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
