#pragma once

#include "platform/audio_output.h"
#include "platform/car_feedback.h"
#include "platform/cockpit.h"
#include "platform/demo_part.h"
#include "platform/surface_feel.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace rally::platform {

// Conductor en el coche (desde la tarea 2.7): lo que sale de cada paso (platform/car_feedback),
// la cabeza del fotograma para la cámara, el interior provisional (platform/cockpit) en la vista
// del ojo, la vibración por superficie, deslizamiento e impacto (platform/surface_feel) y el
// sonido sintético (platform/car_synth en su hilo, platform/audio_output; F4 el volumen). Todo
// solo lee la simulación.
class DemoDriverFeel final : public DemoPart {
public:
    // Abre el audio, salvo con --golden (sin sonido).
    explicit DemoDriverFeel(DemoContext& ctx);

    void handle_keys(DemoContext& ctx, FramePacer& pacer, PerfCounter& perf) override;
    void on_world_rebuilt(DemoContext& ctx) override;
    void after_step(DemoContext& ctx) override;
    void after_steps(DemoContext& ctx, Gamepads& pads, float frame_impact,
                     std::int64_t frame_ns) override;
    void begin_frame(DemoContext& ctx, const DemoFrame& frame) override;
    void draw_3d(DemoContext& ctx, const DemoFrame& frame) override;

private:
    CarFeedback feedback_;
    // Interior: lo fijo y el del fotograma (con el volante y la pantalla), y la dirección
    // aplicada en los dos últimos pasos (el volante se dibuja interpolado).
    std::vector<CockpitTri> cockpit_;
    std::vector<CockpitTri> cockpit_frame_;
    float previous_steer_ = 0.0f;
    float current_steer_ = 0.0f;
    RumbleMixer rumble_mixer_;
    std::unique_ptr<AudioOutput> audio_; // null con --golden
    std::uint32_t audio_generation_ = 0;
    bool rebuilt_ = true; // el mundo se ha reconstruido desde el último after_steps
};

} // namespace rally::platform
