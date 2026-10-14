#include "platform/demo_driver_feel.h"

#include "platform/audio_snapshot.h"
#include "platform/cockpit_draw.h"
#include "platform/demo_common.h"
#include "platform/demo_driver_feel_keys.h"
#include "platform/fixed_step_clock.h"
#include "platform/gamepad.h"
#include "platform/input_shaping.h"
#include "platform/keys_raylib.h"

#include <optional>
#include <string>

namespace rally::platform {

using namespace demo;

DemoDriverFeel::DemoDriverFeel(DemoContext& ctx) {
    if (ctx.golden != nullptr) {
        return;
    }
    audio_ = std::make_unique<AudioOutput>();
    audio_->set_volume(static_cast<float>(ctx.settings.volume_pct) / 100.0f);
    ctx.audio = audio_.get();
    if (!audio_->ready()) {
        ctx.overlay.set_message(Overlay::Message::Settings, "audio: no hay dispositivo, sin sonido",
                                kError, kSettingsMessageNs);
    }
}

void DemoDriverFeel::handle_keys(DemoContext& ctx, FramePacer& /*pacer*/, PerfCounter& /*perf*/) {
    if (action_pressed(action(DriverFeelAction::Volume), ctx.frame_key_context) && audio_) {
        ctx.settings.volume_pct = next_volume_pct(ctx.settings.volume_pct);
        ctx.settings_saver.changed(now_ns());
        audio_->set_volume(static_cast<float>(ctx.settings.volume_pct) / 100.0f);
        ctx.overlay.set_message(Overlay::Message::Settings,
                                "F4: volumen " + std::to_string(ctx.settings.volume_pct) + " %",
                                kText, kShortMessageNs);
    }
}

void DemoDriverFeel::on_world_rebuilt(DemoContext& ctx) {
    // Todo a cero con el mundo nuevo (sin soplido ni golpe falso).
    feedback_.rebuild(ctx.car);
    cockpit_ = build_cockpit(ctx.car);
    previous_steer_ = 0.0f;
    current_steer_ = 0.0f;
    ++audio_generation_;
    rebuilt_ = true;
}

void DemoDriverFeel::after_step(DemoContext& ctx) {
    // Con la telemetría y la orientación de este paso.
    const sim::CarTelemetry& telemetry = ctx.world->telemetry();
    previous_steer_ = current_steer_;
    current_steer_ = telemetry.steer;
    feedback_.step(telemetry, ctx.poses.current_car.rotation);
}

void DemoDriverFeel::after_steps(DemoContext& ctx, Gamepads& pads, float frame_impact,
                                 std::int64_t frame_ns) {
    // En pausa y al reconstruir el mundo, la vibración se para.
    const RumbleInput rumble_input = feedback_.take_frame(ctx.mode.blind);
    const Rumble continuous = continuous_rumble(rumble_input, ctx.mode.blind);
    if (const std::optional<Rumble> rumble =
            rumble_mixer_.update(continuous, frame_impact, frame_ns, ctx.paused || rebuilt_)) {
        pads.rumble(*rumble);
    }
    if (audio_ && audio_->ready()) {
        AudioSnapshot snapshot;
        feedback_.fill(snapshot);
        snapshot.generation = audio_generation_;
        snapshot.paused = ctx.paused;
        snapshot.blind = ctx.mode.blind;
        audio_->publish(snapshot);
    }
    rebuilt_ = false;
}

void DemoDriverFeel::begin_frame(DemoContext& ctx, const DemoFrame& frame) {
    if (frame.frame_impact > kImpactThreshold) {
        ctx.last_rumble_impact = frame.frame_impact;
    }
    // La cabeza (F9), interpolada con el mismo alpha que la pose del coche (demo_camera).
    ctx.head.reset();
    if (ctx.settings.head_motion) {
        ctx.head = feedback_.head().drawn(ctx.blend ? frame.steps.alpha : 1.0);
    }
}

void DemoDriverFeel::draw_3d(DemoContext& ctx, const DemoFrame& frame) {
    if (ctx.view != 0 || ctx.zone_bench) {
        return;
    }
    // Interior (tarea 2.7): lo fijo, el volante con la dirección aplicada interpolada y la
    // pantalla con la marcha, las rpm y la presión del último paso.
    const sim::CarData& car = ctx.car;
    cockpit_frame_ = cockpit_;
    const float steer = ctx.blend ? previous_steer_ + (current_steer_ - previous_steer_) *
                                                          static_cast<float>(frame.steps.alpha)
                                  : current_steer_;
    append_steering_wheel(cockpit_frame_, car, steering_wheel_angle(steer, car));
    const sim::CarTelemetry& t = ctx.world->telemetry();
    CockpitDisplay display;
    display.gear = t.gear;
    display.rpm_fraction = t.engine_omega / car.limiter_speed;
    display.red_fraction = car.upshift_full / car.limiter_speed;
    display.turbo = car.boost_max > 0.0f;
    display.boost_fraction = display.turbo ? t.boost / car.boost_max : 0.0f;
    append_display(cockpit_frame_, car, display);
    draw_cockpit(cockpit_frame_, ctx.poses.drawn_car, ctx.frame_camera.origin);
}

} // namespace rally::platform
