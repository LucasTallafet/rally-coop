#pragma once

#include "platform/input_shaping.h"
#include "sim/surface.h"

#include <array>
#include <cstdint>
#include <optional>

namespace rally::platform {

// Vibración continua del mando (tarea 2.7): motor grave por la rugosidad de la superficie × la
// velocidad más la velocidad de la suspensión (baches); motor agudo por el deslizamiento por
// encima del pico. Los impactos siguen como en la 2.2 (rumble_for_impact) y se mezclan por
// máximo en RumbleMixer.
//
// Modo a ciegas: la rugosidad es la neutra en todas las superficies y no hay término de la
// suspensión (el microrrelieve físico, distinto por superficie, delataría la superficie). El
// deslizamiento y los impactos siguen.

// Rugosidad por superficie, en el orden de sim::Surface, y la neutra del modo a ciegas.
inline constexpr std::array<float, sim::kSurfaceCount> kSurfaceRoughness = {
    0.10f, // asfalto
    0.70f, // grava
    0.40f, // tierra
    0.25f, // nieve
    0.45f, // arena
};
inline constexpr float kBlindRoughness = 0.35f;

// low = min(kLowMax, kRoughGain·r·min(v / kRoughSpeedFull, 1) + kBumpGain·b), con
// b = clamp((|ż| − kBumpSpeedStart) / (kBumpSpeedFull − kBumpSpeedStart), 0, 1)
// high = kHighMax·clamp(ρ_max − 1, 0, 1)
inline constexpr float kRoughGain = 0.5f;
inline constexpr float kRoughSpeedFull = 30.0f; // m/s
inline constexpr float kBumpGain = 0.5f;
// |ż| medio (las cuatro ruedas, media de los pasos del fotograma), en m/s, donde empieza el
// término de los baches y donde llega a 1. Medido en la 2.7 (test «car_feedback: suspension speed
// and head bounce per surface on a straight» y la telemetría de las grabaciones): en recta, solo
// con el microrrelieve, la grava a 100 km/h da 0,161 m/s de media (la arena, 0,187; el asfalto,
// 0,067), y los baches de la zona y la cresta de la carretera dan picos de 0,33 a 0,45 m/s. Con
// una escala lineal no se cumplen las dos cosas (el microrrelieve ≤ ~0,3 del término y los baches
// a 1): de ahí el umbral.
inline constexpr float kBumpSpeedStart = 0.10f;
inline constexpr float kBumpSpeedFull = 0.40f;
inline constexpr float kLowMax = 0.6f;
inline constexpr float kHighMax = 0.6f;

float surface_roughness(sim::Surface surface, bool blind);

// Término de los baches, en [0, 1], para un |ż| medio en m/s.
float bump_term(float suspension_speed);

// Entrada de la vibración continua de un fotograma (CarFeedback::take_frame).
struct RumbleInput {
    float speed = 0.0f;            // m/s, |v| tras el último paso
    float roughness = 0.0f;        // media de las ruedas en el suelo (0 sin ninguna)
    float suspension_speed = 0.0f; // m/s: |ż| medio de las cuatro ruedas, media de los pasos
    float rho_max = 0.0f;          // ρ mayor de las ruedas en el suelo
};

// Rugosidad media de las ruedas en el suelo: `surfaces` con -1 para las que están en el aire.
float mean_roughness(const std::array<std::int8_t, 4>& surfaces, bool blind);

Rumble continuous_rumble(const RumbleInput& input, bool blind);

// Qué se manda al mando en cada fotograma. SDL_RumbleGamepad sustituye la vibración anterior:
// aquí se toma, por motor, el máximo entre la continua y la envolvente del impacto (la de la
// 2.2: rumble_for_impact durante kRumbleMs), y solo se envía si cambia más de kRumbleSendDelta o
// han pasado kRumbleResendNs, con una duración de kRumbleSendMs para que no se corte entre envíos
// (con el PS4 por Bluetooth, un informe en cada fotograma puede saturar el enlace).
inline constexpr float kRumbleSendDelta = 0.02f;
inline constexpr std::int64_t kRumbleResendNs = 100'000'000;
inline constexpr int kRumbleSendMs = 200;

class RumbleMixer {
public:
    // `silent`: en pausa o al reconstruir el mundo; manda 0 (una vez) y olvida el impacto.
    // Devuelve lo que hay que enviar en este fotograma, si hay que enviar algo.
    std::optional<Rumble> update(const Rumble& continuous, float frame_impact, std::int64_t now_ns,
                                 bool silent);

private:
    Rumble impact_; // niveles del último impacto
    std::int64_t impact_until_ns_ = 0;
    Rumble sent_; // lo último enviado (duración 0 si nada)
    std::int64_t sent_ns_ = 0;
    bool any_sent_ = false;
};

} // namespace rally::platform
