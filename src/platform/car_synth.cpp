#include "platform/car_synth.h"

#include <algorithm>
#include <cmath>

namespace rally::platform {

namespace {

constexpr double kTwoPi = 6.283185307179586476925;
constexpr float kPi = 3.14159265358979323846f;
constexpr std::uint64_t kSeed = 0x2a7e;

// Rodadura por perfil (superficies en el orden de sim::Surface y la neutra del modo a ciegas):
// frecuencia central del ruido, amortiguamiento del filtro y ganancia a 30 m/s.
struct RollingProfile {
    float cutoff_hz;
    float damping;
    float gain;
};
constexpr std::array<RollingProfile, sim::kSurfaceCount + 1> kRolling = {{
    {250.0f, 1.2f, 0.10f},  // asfalto: zumbido grave
    {2200.0f, 0.9f, 0.22f}, // grava: crujido brillante
    {700.0f, 1.0f, 0.16f},  // tierra
    {1400.0f, 1.4f, 0.10f}, // nieve
    {450.0f, 1.0f, 0.18f},  // arena: sordo
    {900.0f, 1.0f, 0.15f},  // neutro (a ciegas)
}};
constexpr std::size_t kNeutralProfile = sim::kSurfaceCount;

constexpr float kSkidSlipSpeedFull = 3.0f; // m/s de deslizamiento para el chirrido pleno
constexpr float kSkidLooseHz = 500.0f;
constexpr float kSkidNeutralHz = 700.0f;
constexpr float kStonesPerSecond = 30.0f; // por rueda sobre grava a 20 m/s

float smooth_coefficient(float seconds) {
    return 1.0f - std::exp(-1.0f / (kSynthSampleRate * seconds));
}

void approach(float& value, float target, float a) { value += a * (target - value); }

} // namespace

void CarSynth::Svf::tune(float cutoff_hz, float damping) {
    const float fc = std::min(cutoff_hz, kSynthSampleRate / 7.0f);
    f = 2.0f * std::sin(kPi * fc / kSynthSampleRate);
    q = damping;
}

float CarSynth::Svf::step(float x) {
    low += f * band;
    const float high = x - low - q * band;
    band += f * high;
    return band;
}

void CarSynth::OnePole::tune(float cutoff_hz) {
    a = 1.0f - std::exp(-2.0f * kPi * cutoff_hz / kSynthSampleRate);
}

CarSynth::CarSynth() {
    for (int i = 0; i <= kTableSize; ++i) {
        table_[static_cast<std::size_t>(i)] =
            static_cast<float>(std::sin(kTwoPi * static_cast<double>(i) / kTableSize));
    }
    smooth_a_ = smooth_coefficient(kSynthSmoothSeconds);
    fade_a_ = smooth_coefficient(kSynthFadeSeconds);
    blowoff_decay_ = std::exp(-1.0f / (kSynthSampleRate * 0.15f));
    stone_decay_ = std::exp(-1.0f / (kSynthSampleRate * 0.003f));
    shift_decay_ = std::exp(-1.0f / (kSynthSampleRate * 0.012f));
    impact_decay_ = std::exp(-1.0f / (kSynthSampleRate * 0.12f));
    reset();
}

void CarSynth::reset() {
    started_ = false;
    generation_ = 0;
    shifts_ = blowoffs_ = impacts_ = 0;
    rpm_ = load_ = pedal_ = speed_ = Smoothed{};
    gate_ = Smoothed{1.0f, 1.0f};
    rolling_ = {};
    skid_tonal_ = skid_noise_ = gravel_ = Smoothed{};
    pause_gain_ = 0.0f;
    partials_ = 0;
    partial_phase_ = {};
    partial_order_ = {};
    partial_amp_ = {};
    limiter_phase_ = 0.0;
    engine_noise_filter_ = {};
    engine_rng_.seed(kSeed, 1);
    blowoff_filter_ = {};
    blowoff_filter_.tune(1800.0f, 0.7f);
    blowoff_env_ = 0.0f;
    turbo_rng_.seed(kSeed, 2);
    rolling_filter_ = {};
    rolling_level_ = {};
    for (std::size_t p = 0; p < rolling_filter_.size(); ++p) {
        rolling_filter_[p].tune(kRolling[p].cutoff_hz, kRolling[p].damping);
    }
    rolling_rng_.seed(kSeed, 3);
    squeal_phase_ = vibrato_phase_ = 0.0;
    skid_filter_ = {};
    skid_rng_.seed(kSeed, 4);
    stone_env_ = 0.0f;
    stone_rate_ = 0.0f;
    stone_lowpass_ = {};
    stone_lowpass_.tune(2000.0f);
    stones_rng_.seed(kSeed, 5);
    wind_filter_ = {};
    wind_level_ = 0.0f;
    wind_rng_.seed(kSeed, 6);
    shift_env_ = 0.0f;
    shift_filter_ = {};
    shift_filter_.tune(900.0f);
    impact_env_ = 0.0f;
    impact_phase_ = 0.0;
    impact_filter_ = {};
    impact_filter_.tune(150.0f);
    events_rng_.seed(kSeed, 7);
}

float CarSynth::sine(double phase) const {
    const double x = (phase - std::floor(phase)) * kTableSize;
    const int i = static_cast<int>(x);
    const float frac = static_cast<float>(x - i);
    const float a = table_[static_cast<std::size_t>(i)];
    const float b = table_[static_cast<std::size_t>(i + 1)];
    return a + (b - a) * frac;
}

void CarSynth::set_targets(const AudioSnapshot& s) {
    rpm_.target = s.rpm;
    load_.target = kLoadThrottleShare * std::clamp(s.engine_throttle, 0.0f, 1.0f) +
                   (1.0f - kLoadThrottleShare) * std::clamp(s.torque_load, 0.0f, 1.0f);
    pedal_.target = std::clamp(s.throttle, 0.0f, 1.0f);
    speed_.target = s.speed;
    std::array<float, kProfiles> rolling{};
    float tonal = 0.0f;
    float scrub = 0.0f;
    float gravel = 0.0f;
    for (const WheelSound& w : s.wheels) {
        if (!w.contact || w.surface < 0) {
            continue;
        }
        const auto surface = static_cast<std::size_t>(w.surface);
        rolling[s.blind ? kNeutralProfile : surface] += 0.25f;
        const float skid = std::clamp(w.rho - 1.0f, 0.0f, 1.0f) *
                           std::min(w.slip_speed / kSkidSlipSpeedFull, 1.0f);
        if (!s.blind && static_cast<sim::Surface>(w.surface) == sim::Surface::Asphalt) {
            tonal += 0.25f * skid;
        } else {
            scrub += 0.25f * skid;
        }
        if (!s.blind && static_cast<sim::Surface>(w.surface) == sim::Surface::Gravel) {
            gravel += 0.25f;
        }
    }
    for (std::size_t p = 0; p < rolling_.size(); ++p) {
        rolling_[p].target = rolling[p];
    }
    skid_tonal_.target = tonal;
    skid_noise_.target = scrub;
    gravel_.target = gravel;

    // Parciales del motor: órdenes de 0,5 hasta 3 × el de encendido.
    const int cylinders = std::clamp(s.cylinders, 1, 16);
    const int count = std::min(3 * cylinders, kMaxPartials);
    if (count != partials_) {
        partials_ = count;
        const float firing = 0.5f * static_cast<float>(cylinders);
        float total = 0.0f;
        for (int k = 0; k < count; ++k) {
            const float order = 0.5f * static_cast<float>(k + 1);
            const float harmonic = order / firing;
            float amp = 0.0f;
            if (std::abs(harmonic - std::round(harmonic)) < 1e-4f) {
                const int m = static_cast<int>(std::round(harmonic));
                amp = m == 1 ? 1.0f : (m == 2 ? 0.5f : 0.25f);
            } else {
                amp = 0.15f / (1.0f + harmonic);
            }
            partial_order_[static_cast<std::size_t>(k)] = order;
            partial_amp_[static_cast<std::size_t>(k)] = amp;
            total += amp;
        }
        for (int k = 0; k < count; ++k) {
            partial_amp_[static_cast<std::size_t>(k)] /= total;
        }
    }
}

void CarSynth::jump_to_targets() {
    for (Smoothed* p : {&rpm_, &load_, &pedal_, &speed_, &skid_tonal_, &skid_noise_, &gravel_}) {
        p->value = p->target;
    }
    for (Smoothed& r : rolling_) {
        r.value = r.target;
    }
}

void CarSynth::tune_chunk(const AudioSnapshot& s) {
    const float rpm = std::max(rpm_.value, 0.0f);
    engine_noise_filter_.tune(300.0f + 2500.0f * std::min(rpm / 8000.0f, 1.2f));
    const float speed = std::max(speed_.value, 0.0f);
    const float speed_factor = std::pow(std::min(speed / 30.0f, 1.5f), 0.8f);
    for (std::size_t p = 0; p < rolling_level_.size(); ++p) {
        rolling_level_[p] = kRolling[p].gain * speed_factor;
    }
    skid_filter_.tune(s.blind ? kSkidNeutralHz : kSkidLooseHz, 1.0f);
    stone_rate_ = 4.0f * kStonesPerSecond * std::min(speed / 20.0f, 2.0f) / kSynthSampleRate;
    wind_filter_.tune(150.0f + 12.0f * speed);
    const float w = speed / 45.0f;
    wind_level_ = 0.10f * std::min(w * w, 1.5f);
}

void CarSynth::render(float* out, int frames, const AudioSnapshot& s, unsigned buses) {
    if (!s.active) {
        // Sin coche: silencio, y la próxima instantánea con coche empieza de cero.
        for (int i = 0; i < frames; ++i) {
            out[i] = 0.0f;
        }
        started_ = false;
        pause_gain_ = 0.0f;
        return;
    }
    set_targets(s);
    if (!started_ || s.generation != generation_) {
        // Mundo nuevo: sin suavizado desde el anterior y sin sucesos falsos.
        jump_to_targets();
        generation_ = s.generation;
        shifts_ = s.shifts;
        blowoffs_ = s.blowoffs;
        impacts_ = s.impacts;
        blowoff_env_ = shift_env_ = impact_env_ = stone_env_ = 0.0f;
        started_ = true;
    }
    if (s.shifts != shifts_) {
        shifts_ = s.shifts;
        shift_env_ = 0.25f;
    }
    if (s.blowoffs != blowoffs_) {
        blowoffs_ = s.blowoffs;
        if (s.boost_max_bar > 0.0f) {
            blowoff_env_ = 0.6f * std::min(s.blowoff_bar / s.boost_max_bar, 1.0f);
        }
    }
    if (s.impacts != impacts_) {
        impacts_ = s.impacts;
        impact_env_ = 0.15f + 0.6f * std::clamp((s.impact_peak - 1.0f) / 5.0f, 0.0f, 1.0f);
    }
    const float pause_target = s.paused ? 0.0f : 1.0f;
    const float volume = volume_.load(std::memory_order_relaxed);
    const float limiter_start = s.limiter_rpm - s.limiter_ramp_rpm;
    const float firing = 0.5f * static_cast<float>(std::clamp(s.cylinders, 1, 16));

    for (int i = 0; i < frames; ++i) {
        if (i % kChunk == 0) {
            tune_chunk(s);
        }
        const float a = smooth_a_;
        approach(rpm_.value, rpm_.target, a);
        approach(load_.value, load_.target, a);
        approach(pedal_.value, pedal_.target, a);
        approach(speed_.value, speed_.target, a);
        approach(skid_tonal_.value, skid_tonal_.target, a);
        approach(skid_noise_.value, skid_noise_.target, a);
        approach(gravel_.value, gravel_.target, a);
        for (Smoothed& r : rolling_) {
            approach(r.value, r.target, a);
        }
        approach(pause_gain_, pause_target, fade_a_);

        float sample = 0.0f;
        const float load = load_.value;
        if ((buses & kBusEngine) != 0) {
            const double crank_hz = std::max(rpm_.value, 0.0f) / 60.0;
            float tone = 0.0f;
            for (int k = 0; k < partials_; ++k) {
                const auto p = static_cast<std::size_t>(k);
                const double hz = partial_order_[p] * crank_hz;
                partial_phase_[p] += hz / kSynthSampleRate;
                if (partial_phase_[p] >= 1.0) {
                    partial_phase_[p] -= std::floor(partial_phase_[p]);
                }
                if (hz > 12000.0) {
                    continue;
                }
                const float bright =
                    1.0f - (1.0f - load) * 0.6f * partial_order_[p] / (3.0f * firing);
                tone += partial_amp_[p] * bright * sine(partial_phase_[p]);
            }
            limiter_phase_ += kLimiterBounceHz / kSynthSampleRate;
            if (limiter_phase_ >= 1.0) {
                limiter_phase_ -= 1.0;
            }
            const bool bounce = rpm_.value > limiter_start && pedal_.value > 0.5f;
            approach(gate_.value, bounce ? (limiter_phase_ < 0.5 ? 1.0f : 0.3f) : 1.0f, 0.02f);
            const float hiss = engine_noise_filter_.step(noise(engine_rng_));
            sample += gate_.value *
                      (0.28f * (0.35f + 0.65f * load) * tone + 0.08f * (0.2f + load) * hiss);
        }
        if ((buses & kBusTurbo) != 0) {
            if (blowoff_env_ > 1e-4f) {
                sample += blowoff_env_ * blowoff_filter_.step(noise(turbo_rng_));
                blowoff_env_ *= blowoff_decay_;
            }
        }
        if ((buses & kBusRolling) != 0) {
            const float n = noise(rolling_rng_);
            for (std::size_t p = 0; p < rolling_filter_.size(); ++p) {
                const float band = rolling_filter_[p].step(n);
                sample += rolling_[p].value * rolling_level_[p] * band;
            }
        }
        if ((buses & kBusSkid) != 0) {
            vibrato_phase_ += 6.0 / kSynthSampleRate;
            if (vibrato_phase_ >= 1.0) {
                vibrato_phase_ -= 1.0;
            }
            squeal_phase_ += (850.0 + 40.0 * sine(vibrato_phase_)) / kSynthSampleRate;
            if (squeal_phase_ >= 1.0) {
                squeal_phase_ -= std::floor(squeal_phase_);
            }
            sample += 0.12f * skid_tonal_.value * sine(squeal_phase_);
            sample += 0.30f * skid_noise_.value * skid_filter_.step(noise(skid_rng_));
        }
        if ((buses & kBusStones) != 0 && gravel_.value > 1e-4f) {
            if (stones_rng_.next_float01() < stone_rate_ * gravel_.value) {
                stone_env_ = 0.05f + 0.10f * stones_rng_.next_float01();
            }
            if (stone_env_ > 1e-4f) {
                const float n = noise(stones_rng_);
                sample += stone_env_ * (n - stone_lowpass_.step(n));
                stone_env_ *= stone_decay_;
            }
        }
        if ((buses & kBusWind) != 0) {
            sample += wind_level_ * wind_filter_.step(noise(wind_rng_));
        }
        if ((buses & kBusEvents) != 0) {
            if (shift_env_ > 1e-4f) {
                sample += shift_env_ * shift_filter_.step(noise(events_rng_));
                shift_env_ *= shift_decay_;
            }
            if (impact_env_ > 1e-4f) {
                impact_phase_ += 55.0 / kSynthSampleRate;
                if (impact_phase_ >= 1.0) {
                    impact_phase_ -= 1.0;
                }
                sample += impact_env_ * (0.7f * sine(impact_phase_) +
                                         0.6f * impact_filter_.step(noise(events_rng_)));
                impact_env_ *= impact_decay_;
            }
        }
        out[i] = std::clamp(sample * volume * pause_gain_, -1.0f, 1.0f);
    }
}

} // namespace rally::platform
