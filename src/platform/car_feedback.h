#pragma once

#include "platform/audio_snapshot.h"
#include "platform/head_motion.h"
#include "platform/interpolation.h"
#include "platform/surface_feel.h"
#include "sim/car_data.h"
#include "sim/telemetry.h"

#include <array>
#include <cstdint>

namespace rally::platform {

// Lo que la cabeza, la vibración y el sonido sacan de cada paso de la simulación (tarea 2.7),
// calculado en la plataforma a partir de la telemetría y de la pose del coche de ese paso: la
// simulación no cambia y no hay columnas nuevas. Solo lee.
//
// - Fuerza específica en ejes del coche (HeadInput) para la cabeza.
// - Velocidad de la suspensión: diferencia de `compression` entre pasos, rueda a rueda; 0 en el
//   paso que recoloca y en el siguiente (en el de recolocar las ruedas salen sin contacto, con
//   compresión 0, y vuelven al suelo en el siguiente) y en el primero tras reconstruir el mundo.
// - Deslizamiento normalizado ρ de cada rueda en el suelo (platform/slip).
// - Sucesos como contadores: cambio de marcha (empieza un cambio), soplido de la válvula (el pedal
//   baja de kBlowoffBelow con más de kBlowoffBoostBar de presión; se vuelve a armar cuando el pedal
//   pasa de kBlowoffRearm) e impacto (sim::SimWorld::last_impact pasa de kImpactThreshold).

// Carga del par para el sonido: clamp(max(0, par neto) / par pleno; 0; 1), con el par pleno de
// la curva [engine.torque] a esas rpm × (1 + boost_gain × boost_max) × torque_scale. Con la
// presión plena y el acelerador a fondo se acerca a 1; sin presión, a 1 / (1 + g·p_max).
float engine_torque_load(float engine_torque, float engine_omega, const sim::CarData& car);

inline constexpr float kBlowoffBelow = 0.2f;
inline constexpr float kBlowoffRearm = 0.5f;
inline constexpr float kBlowoffBoostBar = 0.3f;

class CarFeedback {
public:
    // Datos del coche del mundo nuevo y estado a cero: el siguiente paso es el primero.
    void rebuild(const sim::CarData& car);

    // Tras cada paso: su telemetría y la orientación del coche en ese paso (no la interpolada).
    void step(const sim::CarTelemetry& telemetry, const Quatd& body_rotation);

    const HeadMotion& head() const { return head_; }
    const HeadInput& head_input() const { return head_input_; }
    // Por rueda (FL, FR, RL, RR), del último paso.
    const std::array<float, 4>& suspension_speed() const { return suspension_speed_; }
    const std::array<float, 4>& rho() const { return rho_; }

    // Entrada de la vibración de los pasos desde la última llamada (media de |ż|, ρ mayor) y del
    // último; deja el acumulado a cero.
    RumbleInput take_frame(bool blind);

    // Rellena la instantánea del sonido (todo menos generation, paused y blind).
    void fill(AudioSnapshot& snapshot) const;

    std::uint32_t shifts() const { return shifts_; }
    std::uint32_t blowoffs() const { return blowoffs_; }
    std::uint32_t impacts() const { return impacts_; }

private:
    const sim::CarData* car_ = nullptr;
    HeadMotion head_;
    HeadInput head_input_;
    bool first_step_ = true;
    int skip_suspension_ = 1; // pasos que aún dan velocidad de la suspensión 0
    std::array<float, 4> compression_{};
    std::array<float, 4> suspension_speed_{};
    std::array<float, 4> rho_{};
    sim::CarTelemetry last_{};
    // Sucesos.
    bool was_shifting_ = false;
    bool blowoff_armed_ = false;
    bool in_impact_ = false;
    std::uint32_t shifts_ = 0;
    std::uint32_t blowoffs_ = 0;
    float blowoff_bar_ = 0.0f;
    std::uint32_t impacts_ = 0;
    float impact_peak_ = 0.0f;
    // Acumulado del fotograma para la vibración.
    float frame_suspension_sum_ = 0.0f;
    int frame_steps_ = 0;
    float frame_rho_max_ = 0.0f;
};

} // namespace rally::platform
