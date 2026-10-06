#include "platform/car_feedback.h"

#include "platform/camera_space.h"
#include "platform/input_shaping.h"
#include "platform/slip.h"
#include "sim/sim_input.h"

#include <algorithm>
#include <cmath>

namespace rally::platform {

namespace {

constexpr float kStepRate = 240.0f;
constexpr float kRadPerSecToRpm = 30.0f / 3.14159265358979323846f;
constexpr float kPaPerBar = 1.0e5f;

double dot(const Vec3d& a, const Vec3d& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

} // namespace

void CarFeedback::rebuild(const sim::CarData& car) {
    car_ = &car;
    head_.reset();
    head_input_ = {};
    first_step_ = true;
    skip_suspension_ = 1;
    compression_ = {};
    suspension_speed_ = {};
    rho_ = {};
    last_ = {};
    was_shifting_ = false;
    blowoff_armed_ = false;
    in_impact_ = false;
    shifts_ = 0;
    blowoffs_ = 0;
    blowoff_bar_ = 0.0f;
    impacts_ = 0;
    impact_peak_ = 0.0f;
    frame_suspension_sum_ = 0.0f;
    frame_steps_ = 0;
    frame_rho_max_ = 0.0f;
}

void CarFeedback::step(const sim::CarTelemetry& t, const Quatd& body_rotation) {
    const bool reset_step = (t.pressed & sim::kButtonReset) != 0;

    // Fuerza específica en ejes del coche menos la de reposo.
    const Vec3d f{t.ax, static_cast<double>(t.ay) + kHeadGravity, t.az};
    const Vec3d forward = rotate(body_rotation, {0.0, 0.0, -1.0});
    const Vec3d right = rotate(body_rotation, {1.0, 0.0, 0.0});
    const Vec3d up = rotate(body_rotation, {0.0, 1.0, 0.0});
    head_input_ = {dot(f, forward), dot(f, right), dot(f, up) - kHeadGravity};
    if (reset_step) {
        head_.reset();
    } else {
        head_.step(head_input_);
    }

    // Suspensión y deslizamiento por rueda.
    if (reset_step) {
        skip_suspension_ = 2;
    }
    const bool skip = skip_suspension_ > 0;
    if (skip) {
        --skip_suspension_;
    }
    float suspension_sum = 0.0f;
    float rho_max = 0.0f;
    for (std::size_t i = 0; i < 4; ++i) {
        const sim::WheelTelemetry& w = t.wheels[i];
        suspension_speed_[i] = skip ? 0.0f : (w.compression - compression_[i]) * kStepRate;
        compression_[i] = w.compression;
        suspension_sum += std::abs(suspension_speed_[i]);
        rho_[i] = 0.0f;
        if (w.contact != 0 && w.surface >= 0 && car_ != nullptr) {
            rho_[i] =
                normalized_slip(w.kappa, w.alpha, car_->tire(static_cast<sim::Surface>(w.surface)));
            rho_max = std::max(rho_max, rho_[i]);
        }
    }
    frame_suspension_sum_ += 0.25f * suspension_sum;
    ++frame_steps_;
    frame_rho_max_ = std::max(frame_rho_max_, rho_max);

    // Sucesos (no en el paso que recoloca ni en el primero).
    if (!first_step_ && !reset_step) {
        if (t.shifting && !was_shifting_) {
            ++shifts_;
        }
        const float boost_bar = t.boost / kPaPerBar;
        if (t.throttle > kBlowoffRearm) {
            blowoff_armed_ = true;
        } else if (blowoff_armed_ && t.throttle < kBlowoffBelow) {
            blowoff_armed_ = false;
            if (boost_bar > kBlowoffBoostBar) {
                ++blowoffs_;
                blowoff_bar_ = boost_bar;
            }
        }
        if (t.impact > kImpactThreshold) {
            if (!in_impact_) {
                ++impacts_;
                impact_peak_ = 0.0f;
            }
            impact_peak_ = std::max(impact_peak_, t.impact);
            in_impact_ = true;
        } else {
            in_impact_ = false;
        }
    }
    was_shifting_ = t.shifting;
    first_step_ = false;
    last_ = t;
}

RumbleInput CarFeedback::take_frame(bool blind) {
    RumbleInput in;
    in.speed = last_.speed;
    std::array<std::int8_t, 4> surfaces{};
    for (std::size_t i = 0; i < 4; ++i) {
        surfaces[i] = last_.wheels[i].contact != 0 ? last_.wheels[i].surface : std::int8_t{-1};
    }
    in.roughness = mean_roughness(surfaces, blind);
    in.suspension_speed =
        frame_steps_ > 0 ? frame_suspension_sum_ / static_cast<float>(frame_steps_) : 0.0f;
    in.rho_max = frame_rho_max_;
    frame_suspension_sum_ = 0.0f;
    frame_steps_ = 0;
    frame_rho_max_ = 0.0f;
    return in;
}

void CarFeedback::fill(AudioSnapshot& s) const {
    if (car_ != nullptr) {
        s.cylinders = car_->cylinders > 0 ? car_->cylinders : 4;
        s.idle_rpm = car_->idle_speed * kRadPerSecToRpm;
        s.limiter_rpm = car_->limiter_speed * kRadPerSecToRpm;
        s.limiter_ramp_rpm = car_->limiter_ramp * kRadPerSecToRpm;
        s.boost_max_bar = car_->boost_max / kPaPerBar;
    }
    s.active = car_ != nullptr && !first_step_;
    s.rpm = last_.engine_omega * kRadPerSecToRpm;
    s.engine_throttle = last_.engine_throttle;
    s.throttle = last_.throttle;
    s.boost_bar = last_.boost / kPaPerBar;
    s.speed = last_.speed;
    for (std::size_t i = 0; i < 4; ++i) {
        const sim::WheelTelemetry& w = last_.wheels[i];
        s.wheels[i].contact = w.contact != 0;
        s.wheels[i].surface = w.contact != 0 ? w.surface : std::int8_t{-1};
        s.wheels[i].rho = rho_[i];
        s.wheels[i].slip_speed = std::hypot(w.slip_x, w.slip_y);
    }
    s.shifts = shifts_;
    s.blowoffs = blowoffs_;
    s.blowoff_bar = blowoff_bar_;
    s.impacts = impacts_;
    s.impact_peak = impact_peak_;
}

} // namespace rally::platform
