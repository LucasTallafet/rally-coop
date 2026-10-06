#pragma once

#include "sim/car_data.h"
#include "sim/surface.h"

#include <string>

namespace rally::sim {

// Neumático de la 1.0 (tarea 2.5): Pacejka simplificada con una curva normalizada por
// superficie, fricción combinada por deslizamiento normalizado, sensibilidad a la carga,
// deformación de la huella como estado (relajación) y rodadura como par de Coulomb.
//
// Deslizamiento normalizado: ρ_x = κ_r/κ_pico y ρ_y = tan α_r/tan α_pico, con κ_r = u/σ_x y
// tan α_r = w/σ_y; ρ = |(ρ_x, ρ_y)|. Curva normalizada, con el pico en ρ = 1:
//   f(ρ) = sin(C·atan(Bρ − E·(Bρ − atan Bρ))),
// con f(1) = 1 y f → slide_ratio cuando ρ → ∞. El módulo de la fuerza es D·f(ρ), con
// D = μ(F_z)·F_z y μ(F_z) = μ_pico·(1 − k·(F_z/F_z0 − 1)), F_z/F_z0 acotado a [0, 3]. La
// dirección es la de (ρ_x, ρ_y) en adherencia y la de la velocidad de deslizamiento,
// (κ_r, tan α_r), al deslizar, con una mezcla suave entre ρ = 1,5 y 3.

// Cota de F_z/F_z0 en la sensibilidad a la carga.
inline constexpr float kMaxLoadRatio = 3.0f;
// Mezcla de la dirección de la fuerza: normalizada hasta aquí...
inline constexpr float kDirectionBlendStart = 1.5f;
// ... y la de la velocidad de deslizamiento desde aquí.
inline constexpr float kDirectionBlendEnd = 3.0f;
// Recorte de la huella a velocidad: el deslizamiento normalizado de una rueda bloqueada que
// desliza con este ángulo (rad, 75°). Por debajo, una rueda bloqueada no llega al recorte y la
// fuerza lleva exactamente la dirección de su velocidad de deslizamiento (con σ_x ≠ σ_y, el
// recorte desvía la dirección: con 1/κ_pico, hasta 11° entre 30° y 80°).
inline constexpr float kSlideCapAngle = 1.30899694f;

// Calcula al cargar la forma de la curva (C, B, tan α_pico y el recorte a velocidad) de las
// magnitudes del archivo:
// C = 2 − (2/π)·asin(slide_ratio), la raíz entre 1 y 2 de sin(C·π/2) = slide_ratio; B con
// Newton (número fijo de iteraciones) para que el pico caiga en ρ = 1. Falla (con `error`) si
// E ≥ 1 o si el pico pedido no se puede alcanzar.
bool derive_tire_shape(TireSurface& tire, std::string& error);

// Curva normalizada f(ρ) y su derivada df/dρ (ρ ≥ 0).
float tire_curve(const TireSurface& tire, float rho);
float tire_curve_slope(const TireSurface& tire, float rho);

// μ(F_z) con la sensibilidad a la carga. `nominal_load` es F_z0.
float peak_mu(const TireSurface& tire, float load, float nominal_load);

// Fuerza del neumático en un subpaso (ejes del contacto). Mz (par de autoalineación) iría aquí
// más adelante (03 §4); hoy no se calcula.
struct TireForce {
    float fx = 0.0f;      // N, adelante +
    float fy = 0.0f;      // N, derecha +
    float fx_pure = 0.0f; // N: la que daría κ_r solo (diagnóstico)
    float fy_pure = 0.0f; // N: la que daría α_r solo (diagnóstico)
};

// Fuerza estacionaria de la curva con los deslizamientos relajados κ_r y tan α_r, sin el
// amortiguamiento de la carcasa: módulo D·f(ρ) y dirección mezclada. |F| ≤ D siempre.
TireForce tire_force(const TireSurface& tire, float kappa, float tan_alpha, float load,
                     float nominal_load);

// Estado dinámico de una rueda: giro (rad/s, > 0 rodando hacia delante) y deformación de la
// huella, longitudinal u y lateral w (m). Recolocar lo pone a cero.
struct WheelDynamics {
    float omega = 0.0f;
    float u = 0.0f;
    float w = 0.0f;
};

// Entradas del neumático de una rueda, fijas durante los subpasos de un paso.
struct TireInput {
    float vx = 0.0f;                 // m/s: velocidad del cuerpo respecto al suelo en el contacto,
    float vy = 0.0f;                 //      longitudinal (adelante +) y lateral (derecha +)
    float load = 0.0f;               // N (0 en el aire)
    Surface surface = Surface::Dirt; // la del contacto
    float radius = 0.0f;             // m: radio efectivo (condición)
    float drive_torque = 0.0f;       // N·m
    float stop_torque = 0.0f;        // N·m: freno, freno de mano y rodadura, pares de Coulomb (≥ 0)
};

// Un subpaso h de una rueda (deformación de la huella como estado, modelo transitorio). Vale a
// cualquier velocidad, parado incluido:
//   s_x = ω·r − v_x, s_y = −v_y; du/dt = s_x − (|v_x|/σ_x)·u y dw/dt = s_y − (|v_x|/σ_y)·w,
//   integradas de forma exponencial, con σ de la superficie;
//   fuerza elástica = tire_force(u/σ_x, w/σ_y), más el amortiguamiento de la carcasa c·s·g;
//   recorte de la huella: ρ ≤ ρ_max = 1 + (slide_cap − 1)·(1 − g). Parado y con la rueda
//   quieta (g = 1), ρ ≤ 1: límite de Coulomb en el pico, la huella desliza y no acumula. Desde
//   carcass_damping_fade (g = 0), ρ ≤ slide_cap, el de una rueda bloqueada deslizando a 75°:
//   la rueda entra en la rama descendente. g = clamp(1 − v_ref/carcass_damping_fade, 0, 1) con
//   v_ref = max(|v| en el contacto, |ω|·r): una rueda que patina con el coche parado desliza.
//   La fuerza total (elástica más carcasa) se recorta a D.
// El giro de la rueda es semi-implícito en F_x (∂F_x/∂ω de la curva con la dirección
// normalizada, ≥ 0; 0 con la huella o la fuerza en su límite; `implicit` = false lo quita, solo
// para el diagnóstico de los tests). El freno y la rodadura son pares de Coulomb: nunca
// invierten el giro y bloquean la rueda (ω = 0) mientras el par que la mueve no los supere. Sin
// carga (en el aire), u = w = 0 y la fuerza es 0.
TireForce step_tire(const CarData& car, const TireInput& in, float h, bool implicit,
                    WheelDynamics& state);

// step_tire partido en dos para el tren motriz (tarea 2.6), que resuelve el giro de las cuatro
// ruedas y del motor a la vez (sim/drivetrain.h). step_tire es exactamente
//   p = predict_tire(...); ω = coulomb(ω + k·(drive − r·F_x0), k·stop) con
//   k = h/(I_w + h·r·slope); finish_tire(..., p, ω, state).
// Predicción con el giro sin cambiar: la fuerza longitudinal F_x0 que frena la rueda y su
// derivada respecto a ω (slope ≥ 0, 0 con la huella o la fuerza en su límite, o sin
// `implicit`). El resto son los términos que finish_tire reutiliza.
struct TirePrediction {
    bool loaded = false;
    float fx0 = 0.0f;   // N
    float slope = 0.0f; // N·s/rad: ∂F_x/∂ω, sin el radio
    // Para finish_tire.
    float limit = 0.0f;
    float c_x = 0.0f;
    float c_y = 0.0f;
    float fade = 0.0f;
    float rho_max = 0.0f;
    float rx_e = 1.0f;
    float rx_phi = 0.0f;
    float ry_e = 1.0f;
    float ry_phi = 0.0f;
};
TirePrediction predict_tire(const CarData& car, const TireInput& in, float h, bool implicit,
                            const WheelDynamics& state);
// Con el giro nuevo `omega`: deformación de la huella, su recorte y la fuerza del subpaso.
// Escribe omega, u y w en `state`.
TireForce finish_tire(const CarData& car, const TireInput& in, float h,
                      const TirePrediction& prediction, float omega, WheelDynamics& state);
// Inercia efectiva de la rueda en el subpaso: I_w + h·r·slope (el término semi-implícito).
float wheel_effective_inertia(const CarData& car, const TireInput& in, float h,
                              const TirePrediction& prediction);

} // namespace rally::sim
