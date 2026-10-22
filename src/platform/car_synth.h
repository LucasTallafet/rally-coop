#pragma once

#include "platform/audio_snapshot.h"
#include "sim/pcg32.h"
#include "sim/surface.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace rally::platform {

// Sonido sintético provisional del coche (tarea 2.7; el grabado y la mezcla final son de la fase
// 6). Mono, en float. Lo llama el callback de audio (otro hilo) con la última instantánea: no
// reserva memoria, no toma cerrojos y su estado es de tamaño fijo. El ruido sale de un PCG32 por
// capa con semilla fija: con las mismas instantáneas, las mismas muestras.
//
// Capas:
// - Motor: parciales en órdenes de 0,5 del giro del cigüeñal hasta 3 × el de encendido
//   (cylinders / 2); los de encendido pesan 1; 0,5; 0,25 y los de media en media, menos (sin
//   ellos el 5 cilindros sonaría como un 4 más agudo). Amplitud y brillo según la carga, más
//   ruido filtrado. Carga = 0,3·acelerador efectivo + 0,7·par / par pleno (kLoadThrottleShare):
//   a fondo sin presión queda en ~0,6 y con la presión plena llega a ~1, así que la presión se
//   oye en el motor; el corte del cambio también, porque caen los dos términos. Rebote del
//   limitador: dentro de su rampa y con el pedal pisado, corte periódico a kLimiterBounceHz.
// - Turbo: soplido de la válvula (contador `blowoffs`). Sin silbido hasta la fase 6 (sonido
//   grabado): el sintético «no está muy conseguido» (parada de la 2.7).
// - Rodadura: ruido filtrado por superficie y velocidad, por ruedas en el suelo.
// - Deslizamiento: por rueda, un nivel según ρ y la velocidad de deslizamiento (skid_level). En
//   asfalto (sin modo a ciegas) se reparte entre un chirrido tonal cerca del pico y ruido con la
//   rueda bloqueada (asphalt_tone_share); en las demás superficies, todo ruido. Hay dos modelos
//   (SkidModel): el nuevo (lote 3) y el de la 2.7, que se queda para el A/B (tecla 6).
// - Piedras: chasquidos aleatorios en las ruedas sobre grava, más con la velocidad.
// - Viento: ruido grave que crece con v².
// - Sucesos: clac del cambio (`shifts`) y golpe sordo del impacto (`impacts`, de 1 a 6 m/s como
//   rumble_for_impact; igual en todas las superficies).
//
// Modo a ciegas: rodadura y deslizamiento con un perfil neutro igual para todas las superficies,
// sin piedras. Motor, turbo, viento y sucesos no cambian.

inline constexpr float kSynthSampleRate = 48000.0f;
inline constexpr float kSynthSmoothSeconds = 0.015f; // suavizado de los parámetros
inline constexpr float kSynthFadeSeconds = 0.010f;   // pausa: fundido a silencio
inline constexpr float kLimiterBounceHz = 15.0f;
// Parte del acelerador efectivo en la carga del motor; el resto es par / par pleno.
inline constexpr float kLoadThrottleShare = 0.3f;

// Sonido del deslizamiento (lote 3, abs-sonido). Todas las constantes son valores de partida: se
// juzgan de oído con el A/B de la tecla 6 (lote 3). En la fase 6 todo el sonido se sustituye por
// sonido grabado.
enum class SkidModel : std::uint8_t {
    New,      // nivel con la raíz de la velocidad de deslizamiento; ruido con la rueda bloqueada
    Legacy27, // el de la 2.7: satura a 3 m/s y en asfalto todo es tono
};
// Valor de partida, se juzga de oído (lote 3): velocidad de deslizamiento del nivel pleno.
inline constexpr float kSkidSlipSpeedFullNew = 20.0f; // m/s
// Valor de partida, se juzga de oído (lote 3): la de la 2.7.
inline constexpr float kSkidSlipSpeedFullLegacy = 3.0f; // m/s
// Valor de partida, se juzga de oído (lote 3): ρ hasta la que el asfalto es todo tono (w = 1) y
// desde la que es todo ruido (w = 0); entre las dos, w baja en línea recta.
inline constexpr float kSkidToneRhoFull = 1.0f;
inline constexpr float kSkidNoiseRhoFull = 3.0f;
// Valor de partida, se juzga de oído (lote 3): ganancia del tono (en la 2.7, 0,12) y del ruido.
inline constexpr float kSkidToneGainNew = 0.05f;
inline constexpr float kSkidToneGainLegacy = 0.12f;
inline constexpr float kSkidNoiseGain = 0.30f;

// Nivel de una rueda [0, 1]: clamp(ρ − 1, 0, 1) por la velocidad de deslizamiento (nuevo: su raíz
// sobre kSkidSlipSpeedFullNew; la 2.7: lineal sobre 3 m/s).
float skid_level(float rho, float slip_speed, SkidModel model);
// Parte del nivel que va al tono en asfalto sin modo a ciegas, w en [0, 1]; el resto va al
// ruido. La 2.7: todo al tono (1).
float asphalt_tone_share(float rho, SkidModel model);

// Capas, para los tests (CarSynth::render con una máscara).
enum SynthBus : unsigned {
    kBusEngine = 1u << 0,
    kBusTurbo = 1u << 1,
    kBusRolling = 1u << 2,
    kBusSkid = 1u << 3,
    kBusStones = 1u << 4,
    kBusWind = 1u << 5,
    kBusEvents = 1u << 6,
    kBusAll = (1u << 7) - 1,
};

class CarSynth {
public:
    CarSynth();

    // Escribe `frames` muestras en `out` con la instantánea `s`. `buses`: qué capas suenan.
    void render(float* out, int frames, const AudioSnapshot& s, unsigned buses = kBusAll);

    // Volumen general [0, 1]; lo cambia el bucle principal (F4) sin llamar a raylib.
    void set_volume(float volume) { volume_.store(volume, std::memory_order_relaxed); }
    float volume() const { return volume_.load(std::memory_order_relaxed); }

    // Modelo del sonido del deslizamiento (tecla 6). Atómico: el callback corre en otro hilo.
    // Empieza en el nuevo; reset() no lo toca (no es estado del sonido).
    void set_skid_model(SkidModel model) { skid_model_.store(model, std::memory_order_relaxed); }
    SkidModel skid_model() const { return skid_model_.load(std::memory_order_relaxed); }

    // Estado a cero (como al construirlo): ruido, fases y envolventes.
    void reset();

private:
    // Filtro de estado variable (Chamberlin): paso bajo y paso banda.
    struct Svf {
        float low = 0.0f;
        float band = 0.0f;
        float f = 0.0f; // 2·sin(π·fc/fs)
        float q = 1.0f; // amortiguamiento (1/Q)
        void tune(float cutoff_hz, float damping);
        float step(float x); // devuelve el paso banda
    };
    struct OnePole {
        float y = 0.0f;
        float a = 0.0f;
        void tune(float cutoff_hz);
        float step(float x) { return y += a * (x - y); }
    };
    // Parámetros suavizados (objetivo y valor).
    struct Smoothed {
        float value = 0.0f;
        float target = 0.0f;
    };
    static constexpr int kTableSize = 4096;
    static constexpr int kMaxPartials = 48;
    static constexpr int kChunk = 32;                        // muestras entre afinados de filtros
    static constexpr int kProfiles = sim::kSurfaceCount + 1; // superficies y la neutra

    void set_targets(const AudioSnapshot& s, SkidModel model);
    void jump_to_targets();
    void tune_chunk(const AudioSnapshot& s);
    float sine(double phase) const; // phase en ciclos
    static float noise(sim::Pcg32& rng) { return rng.next_float01() * 2.0f - 1.0f; }

    std::array<float, kTableSize + 1> table_{};
    std::atomic<float> volume_{1.0f};
    std::atomic<SkidModel> skid_model_{SkidModel::New};
    // Contadores vistos y generación.
    bool started_ = false;
    std::uint32_t generation_ = 0;
    std::uint32_t shifts_ = 0;
    std::uint32_t blowoffs_ = 0;
    std::uint32_t impacts_ = 0;

    float smooth_a_ = 0.0f;
    float fade_a_ = 0.0f;
    Smoothed rpm_, load_, pedal_, speed_, gate_;
    std::array<Smoothed, kProfiles> rolling_{}; // fracción de ruedas en el suelo por perfil
    Smoothed skid_tonal_, skid_noise_, gravel_;
    float pause_gain_ = 0.0f;

    // Motor.
    int partials_ = 0;
    std::array<double, kMaxPartials> partial_phase_{};
    std::array<float, kMaxPartials> partial_order_{};
    std::array<float, kMaxPartials> partial_amp_{};
    double limiter_phase_ = 0.0;
    OnePole engine_noise_filter_;
    sim::Pcg32 engine_rng_{0x2a7e, 1};
    // Soplido de la válvula.
    Svf blowoff_filter_;
    float blowoff_env_ = 0.0f;
    sim::Pcg32 turbo_rng_{0x2a7e, 2};
    // Rodadura y deslizamiento.
    std::array<Svf, kProfiles> rolling_filter_{};
    std::array<float, kProfiles> rolling_level_{};
    sim::Pcg32 rolling_rng_{0x2a7e, 3};
    double squeal_phase_ = 0.0;
    double vibrato_phase_ = 0.0;
    Svf skid_filter_;
    sim::Pcg32 skid_rng_{0x2a7e, 4};
    // Piedras.
    float stone_env_ = 0.0f;
    float stone_rate_ = 0.0f; // por muestra
    OnePole stone_lowpass_;
    sim::Pcg32 stones_rng_{0x2a7e, 5};
    // Viento.
    OnePole wind_filter_;
    float wind_level_ = 0.0f;
    sim::Pcg32 wind_rng_{0x2a7e, 6};
    // Sucesos.
    float shift_env_ = 0.0f;
    OnePole shift_filter_;
    float impact_env_ = 0.0f;
    double impact_phase_ = 0.0;
    OnePole impact_filter_;
    sim::Pcg32 events_rng_{0x2a7e, 7};
    // Decaimientos por muestra (afinados en el constructor).
    float blowoff_decay_ = 0.0f;
    float stone_decay_ = 0.0f;
    float shift_decay_ = 0.0f;
    float impact_decay_ = 0.0f;
};

} // namespace rally::platform
