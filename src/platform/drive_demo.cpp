#include "platform/drive_demo.h"

#include "platform/fixed_step_clock.h"
#include "platform/keys.h"
#include "platform/perf_counter.h"
#include "sim/sim_world.h"

#include <raylib.h>
#include <rlgl.h>

#include <filesystem>
#include <utility>

namespace rally::platform {

static_assert(sim::kStepsPerSecond == kStepsPerSecond, "la simulación y el bucle usan otro paso");

std::uint8_t DemoContext::key_context() const {
    if (!mode.zone) {
        return kContextArena;
    }
    return mode.blind ? kContextBlind : kContextZone;
}

DriveDemo::DriveDemo(std::string car_path)
    : world_(ctx_, std::move(car_path)), measure_(ctx_), driver_feel_(ctx_), screen_(ctx_) {
    rlSetClipPlanes(DemoCamera::kNearPlane, DemoCamera::kFarPlane);
    ctx_.rebuild = Rebuild::Describe;
    apply_rebuild();
}

DriveDemo::DriveDemo(const GoldenFrame& golden, const io::Recording& recording)
    : ctx_(&golden), golden_recording_(&recording), world_(ctx_, recording), measure_(ctx_),
      driver_feel_(ctx_), screen_(ctx_) {
    rlSetClipPlanes(DemoCamera::kNearPlane, DemoCamera::kFarPlane);
    ctx_.view = golden.view;
    // La descripción es la de la grabación: mundo nuevo sin describir.
    ctx_.rebuild = Rebuild::Reset;
    apply_rebuild();
}

void DriveDemo::apply_rebuild() {
    if (ctx_.rebuild == Rebuild::None) {
        return;
    }
    if (ctx_.rebuild == Rebuild::Describe) {
        world_.describe(ctx_);
    }
    ctx_.rebuild = Rebuild::None;
    world_.reset(ctx_);
    for (DemoPart* part : parts_) {
        part->on_world_rebuilt(ctx_);
    }
}

int DriveDemo::render_width() const {
    return ctx_.target.id != 0 ? ctx_.target.texture.width : GetScreenWidth();
}

int DriveDemo::render_height() const {
    return ctx_.target.id != 0 ? ctx_.target.texture.height : GetScreenHeight();
}

void DriveDemo::use_arena() {
    ctx_.mode.zone = false;
    ctx_.mode.blind = false;
    ctx_.view = 0;
    ctx_.rebuild = Rebuild::Describe;
    apply_rebuild();
}

void DriveDemo::start_zone_bench(bool road) {
    measure_.start_zone_bench(ctx_, road);
    apply_rebuild();
}

void DriveDemo::handle_keys(FramePacer& pacer, PerfCounter& perf) {
    // El contexto de las teclas es el del principio del fotograma para todas las partes.
    ctx_.frame_key_context = ctx_.key_context();
    for (DemoPart* part : parts_) {
        part->handle_keys(ctx_, pacer, perf);
    }
    apply_rebuild();
}

float DriveDemo::step(const sim::SimInput& input) {
    if (ctx_.paused) {
        return 0.0f;
    }
    world_.step(ctx_, input);
    for (DemoPart* part : parts_) {
        part->after_step(ctx_);
    }
    return ctx_.world->last_impact();
}

void DriveDemo::update_feedback(Gamepads& pads, float frame_impact, std::int64_t frame_ns) {
    for (DemoPart* part : parts_) {
        part->after_steps(ctx_, pads, frame_impact, frame_ns);
    }
}

void DriveDemo::draw(const FrameSteps& steps, std::int64_t frame_ns, const FramePacer& pacer,
                     const FixedStepClock& clock, const DriverInput& driver,
                     const sim::SimInput& input, float frame_impact, PerfCounter& perf) {
    const DemoFrame frame{steps, frame_ns, pacer, clock, driver, input, frame_impact, perf};
    for (DemoPart* part : parts_) {
        part->begin_frame(ctx_, frame);
    }

    constexpr Color kSky{40, 44, 52, 255};
    BeginDrawing();
    perf.begin_render();
    if (ctx_.target.id != 0) {
        BeginTextureMode(ctx_.target);
    }
    ClearBackground(kSky);
    BeginMode3D(ctx_.frame_camera.camera);
    for (DemoPart* part : parts_) {
        part->draw_3d(ctx_, frame);
    }
    EndMode3D();
    if (ctx_.target.id != 0) {
        EndTextureMode();
        // Las RenderTexture de OpenGL quedan invertidas en y: el rectángulo de origen, también.
        const auto width = static_cast<float>(ctx_.target.texture.width);
        const auto height = static_cast<float>(ctx_.target.texture.height);
        DrawTexturePro(ctx_.target.texture, Rectangle{0.0f, 0.0f, width, -height},
                       Rectangle{0.0f, 0.0f, static_cast<float>(GetScreenWidth()),
                                 static_cast<float>(GetScreenHeight())},
                       Vector2{0.0f, 0.0f}, 0.0f, WHITE);
    }

    ctx_.overlay.begin(GetScreenWidth(), GetScreenHeight());
    for (DemoPart* part : parts_) {
        part->draw_2d(ctx_, frame);
    }
    ctx_.overlay.draw_messages();
    for (DemoPart* part : parts_) {
        part->end_frame(ctx_, frame);
    }
    perf.end_render();
    EndDrawing();
    perf.end_present();
}

bool DriveDemo::draw_golden(const FramePacer& pacer, const FixedStepClock& clock,
                            const DriverInput& driver, PerfCounter& perf, std::string& error) {
    std::uint64_t done = 0;
    sim::SimInput input;
    for (const io::InputRun& run : golden_recording_->runs) {
        for (std::uint32_t i = 0; i < run.count && done < ctx_.golden->step; ++i, ++done) {
            input = run.input;
            step(input);
        }
    }
    if (done < ctx_.golden->step) {
        error = "la grabación tiene " + std::to_string(done) + " pasos";
        return false;
    }
    // Tres fotogramas iguales (sin pasos entre ellos) y se guarda el último: lo que se crea al
    // dibujar por primera vez ya está creado.
    FrameSteps steps;
    steps.count = 1;
    steps.alpha = 1.0;
    for (int frame = 0; frame < 3; ++frame) {
        if (frame == 2) {
            measure_.request_capture();
        }
        draw(steps, 0, pacer, clock, driver, input, 0.0f, perf);
    }
    std::error_code missing;
    const std::string& output = ctx_.golden->output;
    if (!std::filesystem::exists(std::filesystem::path(std::u8string(output.begin(), output.end())),
                                 missing)) {
        error = "no se ha podido guardar " + output;
        return false;
    }
    return true;
}

} // namespace rally::platform
