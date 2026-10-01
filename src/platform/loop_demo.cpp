#include "platform/loop_demo.h"

#include "platform/fixed_step_clock.h"
#include "platform/frame_pacer.h"

#include <SDL3/SDL_video.h>
#include <raylib.h>

#include <cmath>
#include <numbers>
#include <string>

namespace rally::platform {

namespace {

constexpr double kStepSeconds = 1.0 / static_cast<double>(kStepsPerSecond);
constexpr double kOrbitSpeed = std::numbers::pi;      // media vuelta por segundo
constexpr double kSpinSpeed = 2.0 * std::numbers::pi; // una vuelta por segundo
constexpr double kOrbitRadius = 250.0;                // píxeles
constexpr float kSquareSize = 60.0f;
constexpr std::int64_t kStatsPeriodNs = 500'000'000;

Pose pose_at(double orbit, double spin) {
    const double center_x = GetScreenWidth() * 0.5;
    const double center_y = GetScreenHeight() * 0.5;
    Pose pose;
    pose.position = {center_x + kOrbitRadius * std::cos(orbit),
                     center_y + kOrbitRadius * std::sin(orbit), 0.0};
    pose.rotation = {0.0, 0.0, std::sin(spin * 0.5), std::cos(spin * 0.5)};
    return pose;
}

// Nombre de la gráfica (GL_RENDERER). raylib no lo expone; glGetString se pide a SDL.
std::string query_renderer() {
    constexpr unsigned int kGlRenderer = 0x1F01;
    using GetString = const unsigned char*(__stdcall*)(unsigned int);
    const auto get_string = reinterpret_cast<GetString>(SDL_GL_GetProcAddress("glGetString"));
    const unsigned char* name = get_string != nullptr ? get_string(kGlRenderer) : nullptr;
    return name != nullptr ? reinterpret_cast<const char*>(name) : "desconocida";
}

} // namespace

LoopDemo::LoopDemo()
    : previous_(pose_at(0.0, 0.0)), current_(previous_), renderer_(query_renderer()) {}

void LoopDemo::handle_keys(FramePacer& pacer) {
    if (IsKeyPressed(KEY_F)) {
        pacer.set_target(pacer.target() == FrameTarget::MonitorRate ? FrameTarget::Fps30
                                                                    : FrameTarget::MonitorRate);
    }
    if (IsKeyPressed(KEY_I)) {
        interpolate_ = !interpolate_;
    }
    if (IsKeyPressed(KEY_V)) {
        pacer.set_vsync(!pacer.vsync());
    }
}

DemoInput LoopDemo::read_input() const {
    DemoInput input;
    input.reverse = IsKeyDown(KEY_SPACE);
    return input;
}

void LoopDemo::step(const DemoInput& input) {
    const double direction = input.reverse ? -1.0 : 1.0;
    orbit_ = std::fmod(orbit_ + direction * kOrbitSpeed * kStepSeconds, 2.0 * std::numbers::pi);
    spin_ = std::fmod(spin_ + direction * kSpinSpeed * kStepSeconds, 2.0 * std::numbers::pi);
    previous_ = current_;
    current_ = pose_at(orbit_, spin_);
}

void LoopDemo::update_stats(std::int64_t frame_ns) {
    stats_ns_ += frame_ns;
    ++stats_frames_;
    if (stats_ns_ >= kStatsPeriodNs) {
        fps_ = stats_frames_ * 1e9 / static_cast<double>(stats_ns_);
        average_ms_ = static_cast<double>(stats_ns_) / stats_frames_ / 1e6;
        stats_ns_ = 0;
        stats_frames_ = 0;
    }
}

void LoopDemo::draw(const FrameSteps& steps, std::int64_t frame_ns, const FramePacer& pacer) {
    update_stats(frame_ns);
    const Pose pose = interpolate_ ? interpolate(previous_, current_, steps.alpha) : current_;
    const double angle = 2.0 * std::atan2(pose.rotation.z, pose.rotation.w);

    BeginDrawing();
    ClearBackground(Color{40, 44, 52, 255});

    DrawCircleLines(GetScreenWidth() / 2, GetScreenHeight() / 2, static_cast<float>(kOrbitRadius),
                    Color{70, 76, 88, 255});
    const Rectangle square{static_cast<float>(pose.position.x), static_cast<float>(pose.position.y),
                           kSquareSize, kSquareSize};
    DrawRectanglePro(square, Vector2{kSquareSize * 0.5f, kSquareSize * 0.5f},
                     static_cast<float>(angle * 180.0 / std::numbers::pi),
                     Color{230, 160, 60, 255});

    const char* target =
        pacer.target() == FrameTarget::MonitorRate ? "frecuencia del monitor" : "30 fps";
    const char* method = !pacer.uses_timer()             ? ""
                         : pacer.high_resolution_timer() ? " + temporizador de alta resolución"
                                                         : " + temporizador de 1 ms";
    const Color text = Color{220, 223, 228, 255};
    DrawText(TextFormat("%.1f fps   %.2f ms por fotograma (último %.2f)", fps_, average_ms_,
                        static_cast<double>(frame_ns) / 1e6),
             16, 16, 20, text);
    DrawText(TextFormat("pasos en el último fotograma: %d   alpha: %.2f", steps.count, steps.alpha),
             16, 42, 20, text);
    DrawText(
        TextFormat("ritmo: %s   sincronización vertical: %s", target, pacer.vsync() ? "sí" : "no"),
        16, 68, 20, text);
    DrawText(TextFormat("monitor: %.2f Hz   intervalo: %d%s", pacer.refresh_rate(),
                        pacer.swap_interval(), method),
             16, 94, 20, text);
    DrawText(TextFormat("interpolación: %s", interpolate_ ? "sí" : "no"), 16, 120, 20, text);
    DrawText(TextFormat("gráfica: %s", renderer_.c_str()), 16, 146, 20, text);
    DrawText("F: monitor/30 fps   I: interpolación   V: sincronización vertical   "
             "Espacio: al revés   Esc: salir",
             16, GetScreenHeight() - 36, 20, Color{150, 156, 168, 255});

    EndDrawing();
}

} // namespace rally::platform
