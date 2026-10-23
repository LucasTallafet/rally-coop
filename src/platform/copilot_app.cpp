#include "platform/copilot_app.h"

#include "io/recording.h"
#include "net/copilot.h"
#include "platform/camera_space.h"
#include "platform/copilot_keys.h"
#include "platform/demo_camera.h"
#include "platform/demo_camera_keys.h"
#include "platform/demo_common.h"
#include "platform/demo_part.h"
#include "platform/demo_screen_keys.h"
#include "platform/fixed_step_clock.h"
#include "platform/frame_pacer.h"
#include "platform/keys_raylib.h"
#include "platform/perf_counter.h"
#include "platform/scene_draw.h"
#include "platform/screen_layout.h"
#include "platform/wheel_spin.h"
#include "platform/window.h"
#include "platform/zone_draw.h"
#include "sim/car_data.h"
#include "sim/check.h"
#include "sim/sim_world.h"

#include <SDL3/SDL_video.h>
#include <raylib.h>
#include <rlgl.h>

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <string>

namespace rally::platform {

using namespace demo;

namespace {

constexpr int kWindowWidth = 640;
constexpr int kWindowHeight = 360;
constexpr std::uint64_t kLinkSeed = 1; // red simulada repetible
constexpr Color kSky{40, 44, 52, 255};

Pose to_pose(const sim::BodyPose& body) {
    Pose pose;
    pose.position = {body.x, body.y, body.z};
    pose.rotation = {body.qx, body.qy, body.qz, body.qw};
    return pose;
}

// Ventana en la esquina inferior derecha del área útil del monitor donde se abrió (sin la barra
// de tareas), para que el conductor la vea junto a la suya en un solo monitor.
void place_window() {
    SDL_Window* window = SDL_GL_GetCurrentWindow();
    if (window == nullptr) {
        return;
    }
    SDL_Rect usable{};
    if (!SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(window), &usable)) {
        return;
    }
    SetWindowPosition(usable.x + usable.w - kWindowWidth, usable.y + usable.h - kWindowHeight);
}

class CopilotApp {
public:
    explicit CopilotApp(const CopilotArgs& args) {
        ctx_.overlay.set_kind(ScreenKind::Copilot);
        ctx_.view = 1; // persecución por defecto
        rlSetClipPlanes(DemoCamera::kNearPlane, DemoCamera::kFarPlane);
        start_ns_ = now_ns();
        std::string error;
        if (!net_.connect(args.address, args.port, error)) {
            // No se puede abrir el socket o la dirección no vale: se dice en pantalla.
            ctx_.overlay.set_message(Overlay::Message::Network, "red: " + error, kError);
        }
        last_status_ = std::string(net_.status());
    }

    bool update_and_draw(PerfCounter& perf, const FramePacer& pacer, const FixedStepClock& clock) {
        // Un solo reloj por fotograma para el servicio, la muestra y las estadísticas.
        const double now_s = static_cast<double>(now_ns() - start_ns_) * 1e-9;
        handle_keys(perf);
        net_.service(now_s);
        if (const std::optional<net::WorldInfo> info = net_.take_world()) {
            build_world(*info);
        }
        const std::string status(net_.status());
        if (status != last_status_) {
            last_status_ = status;
            ctx_.overlay.set_message(Overlay::Message::Network, "red: " + status, kText,
                                     kShortMessageNs);
        }
        update_car(now_s);
        draw(now_s, perf, pacer, clock);
        return true;
    }

private:
    void handle_keys(PerfCounter& perf) {
        if (action_pressed(action(CameraAction::Camera), kCopilotKeyContext)) {
            ctx_.view = ctx_.view == 1 ? 2 : 1; // persecución / toda la zona (o fija en el recinto)
        }
        if (action_pressed(action(ScreenAction::DebugPanel), kCopilotKeyContext)) {
            debug_visible_ = !debug_visible_;
        }
        if (action_pressed(action(ScreenAction::Help), kCopilotKeyContext)) {
            const int pages = ctx_.overlay.layout().help.pages;
            help_page_ = help_page_ + 1 < pages ? help_page_ + 1 : -1;
        }
        if (action_pressed(action(ScreenAction::Perf), kCopilotKeyContext)) {
            perf.set_visible(!perf.visible());
        }
        if (action_pressed(action(CopilotAction::SimulatedNet), kCopilotKeyContext)) {
            level_ = next_simulated_net(level_);
            net_.set_impairment(simulated_net_params(level_), kLinkSeed);
            ctx_.overlay.set_message(Overlay::Message::Settings,
                                     "Z: red simulada " + std::string(simulated_net_name(level_)),
                                     kText, kShortMessageNs);
        }
    }

    // Construye el mundo con el mismo código que el host y sin llamar nunca a step.
    void build_world(const net::WorldInfo& info) {
        sim::WorldDesc desc;
        std::string error;
        if (!io::parse_world(info.world_bytes, desc, error)) {
            ctx_.overlay.set_message(Overlay::Message::Car,
                                     "mundo del conductor no válido: " + error, kError);
            return;
        }
        const sim::CarParseResult car = sim::parse_car(info.car_text, sim::CarKeys::Full);
        if (!car.ok) {
            ctx_.overlay.set_message(Overlay::Message::Car,
                                     "coche del conductor no válido: " + car.error, kError);
            return;
        }
        desc.car_data = car.car; // la descripción serializada no lleva los datos del coche
        world_.reset();
        auto world = std::make_unique<sim::SimWorld>();
        world->build(desc);
        world->seal();
        ctx_.desc = desc;
        ctx_.car = car.car;
        ctx_.mode.zone = desc.test_zone;
        if (world->zone() != nullptr) {
            zone_draw_.build(*world->zone(), false);
        }
        zone_matches_ = world->zone_hash() == info.zone_hash;
        if (!zone_matches_) {
            ctx_.overlay.set_message(
                Overlay::Message::Car,
                "la zona no coincide con la del conductor; se dibuja la recibida", kError,
                kSettingsMessageNs);
        }
        // Cuerpos de la descripción donde empezaron (sus poses no viajan) y ruedas en su reposo:
        // anclaje, radio y ancho del mundo construido, con la suspensión en equilibrio.
        ScenePoses& p = ctx_.poses;
        p.drawn.resize(static_cast<std::size_t>(world->body_count()));
        for (std::size_t i = 0; i < p.drawn.size(); ++i) {
            p.drawn[i] = to_pose(world->pose(static_cast<int>(i)));
        }
        for (int i = 0; i < sim::kWheelCount; ++i) {
            sim::SimWorld::WheelPose w = world->car_wheel(i);
            w.suspension_length = static_cast<float>(rest_suspension_length(w.attach_y, w.radius));
            w.steer = 0.0f;
            w.rotation = 0.0f;
            p.drawn_wheels[static_cast<std::size_t>(i)] = w;
        }
        p.drawn_car = to_pose(world->car_pose());
        have_last_car_ = false;
        generation_ = info.generation;
        world_ = std::move(world);
    }

    // La pose del coche a la hora de ahora (menos el retardo de 100 ms del búfer) y el giro de las
    // ruedas, integrado del avance.
    void update_car(double now_s) {
        if (!world_ || !ctx_.desc.car) {
            return;
        }
        net::Pose sample;
        if (!net_.buffer().sample(now_s, sample)) {
            return;
        }
        Pose& car = ctx_.poses.drawn_car;
        const Vec3d previous = car.position;
        car.position = {sample.x, sample.y, sample.z};
        car.rotation = {sample.qx, sample.qy, sample.qz, sample.qw};
        if (have_last_car_) {
            const Vec3d forward = rotate(car.rotation, Vec3d{0.0, 0.0, -1.0});
            const double advance = (car.position.x - previous.x) * forward.x +
                                   (car.position.y - previous.y) * forward.y +
                                   (car.position.z - previous.z) * forward.z;
            for (sim::SimWorld::WheelPose& w : ctx_.poses.drawn_wheels) {
                w.rotation = static_cast<float>(
                    wrap_angle(static_cast<double>(w.rotation) +
                               wheel_spin_delta(advance, static_cast<double>(w.radius))));
            }
        }
        have_last_car_ = true;
    }

    void draw_scene() {
        const Vec3d& origin = ctx_.frame_camera.origin;
        const ScenePoses& p = ctx_.poses;
        if (ctx_.mode.zone) {
            zone_draw_.draw(ctx_.frame_camera.eye, origin);
            for (std::size_t i = 0; i < p.drawn.size(); ++i) {
                draw_box(p.drawn[i], ctx_.desc.bodies[i], body_color(ctx_.desc, i), origin);
            }
        } else {
            for (std::size_t i = 0; i < p.drawn.size(); ++i) {
                draw_box(p.drawn[i], ctx_.desc.bodies[i], body_color(ctx_.desc, i), origin);
                if (i == 0) {
                    draw_floor_grid(ctx_.desc.bodies[0], origin);
                }
            }
            draw_posts(ctx_.desc.bodies[0], origin);
        }
        if (ctx_.desc.car) {
            draw_car(p.drawn_car, ctx_.car, p.drawn_wheels, origin);
        }
    }

    void draw(double now_s, PerfCounter& perf, const FramePacer& pacer,
              const FixedStepClock& clock) {
        if (world_) {
            camera_.update(ctx_);
        }
        BeginDrawing();
        perf.begin_render();
        ClearBackground(kSky);
        if (world_) {
            BeginMode3D(ctx_.frame_camera.camera);
            draw_scene();
            EndMode3D();
        }
        Overlay& overlay = ctx_.overlay;
        overlay.begin(GetScreenWidth(), GetScreenHeight());
        const UiRect& indicator = overlay.rect(Layer::Indicator);
        const bool connected = net_.stats().connected;
        std::string status(net_.status());
        if (connected && !world_) {
            status += ", esperando el mundo";
        }
        overlay.text_fit(Layer::Indicator, indicator.x, indicator.y, status,
                         connected ? kStrong : kError);
        const UiRect& hint = overlay.rect(Layer::HelpHint);
        overlay.text(Layer::HelpHint, hint.x, hint.y, layer_samples(Layer::HelpHint)[0], kDim);
        if (help_page_ >= 0) {
            draw_help();
        } else {
            if (debug_visible_) {
                draw_debug(now_s);
            }
            perf.draw(pacer, clock, false, GetScreenWidth(), GetScreenHeight(), overlay);
        }
        overlay.draw_messages();
        perf.end_render();
        EndDrawing();
        perf.end_present();
    }

    void draw_help() {
        Overlay& overlay = ctx_.overlay;
        const ScreenLayout& layout = overlay.layout();
        const HelpGrid& grid = layout.help;
        const std::span<const KeyBinding> table = help_table(ScreenKind::Copilot);
        overlay.panel(Layer::Help, kPanelBackground);
        const int x0 = overlay.content_x(Layer::Help);
        const int y0 = overlay.content_y(Layer::Help);
        const int page = std::min(help_page_, grid.pages - 1);
        overlay.text(Layer::Help, x0, y0, help_title(page, grid.pages), kDim);
        for (const HelpCell& cell : grid.cells) {
            if (cell.page != page) {
                continue;
            }
            const int x = x0 + cell.column * (grid.column_width + grid.column_gap);
            const int y = y0 + cell.row * layout.m.line;
            if (cell.binding < 0) {
                overlay.text(Layer::Help, x, y, key_block_name(cell.block), kTitle);
                continue;
            }
            const KeyBinding& b = table[static_cast<std::size_t>(cell.binding)];
            overlay.text(Layer::Help, x, y, binding_label_short(b), kText);
            overlay.text(Layer::Help, x + grid.text_x, y, b.text, kText);
        }
    }

    void draw_debug(double now_s) {
        Overlay& overlay = ctx_.overlay;
        const UiMetrics& m = overlay.metrics();
        overlay.panel(Layer::Debug, kPanelBackground);
        const int x = overlay.content_x(Layer::Debug);
        int y = overlay.content_y(Layer::Debug);
        const auto line = [&](const char* text, Color color = kText) {
            overlay.text_fit(Layer::Debug, x, y, text, color);
            y += m.line;
        };
        const net::LinkStats link = net_.stats();
        const net::BufferStats buffer = net_.buffer().stats(now_s);
        line("DEPURACIÓN (F3)", kTitle);
        line(TextFormat("estado: %s", std::string(net_.status()).c_str()));
        line(TextFormat("RTT: %.1f ms   estados: %llu", link.rtt_s * 1000.0,
                        static_cast<unsigned long long>(link.received)));
        line(TextFormat("perdidos: %llu   tardíos: %llu",
                        static_cast<unsigned long long>(buffer.missing),
                        static_cast<unsigned long long>(buffer.late)));
        line(TextFormat("retardo: %.1f ms   extrapola: %s", buffer.delay_s * 1000.0,
                        buffer.extrapolating ? "sí" : "no"));
        line(TextFormat("paso del host: %u   mundo: %d", buffer.host_step,
                        world_ ? static_cast<int>(generation_) : -1));
        line(!world_         ? "zona: sin mundo"
             : zone_matches_ ? "zona: coincide con la del conductor"
                             : "zona: no coincide con la del conductor",
             zone_matches_ || !world_ ? kText : kError);
        line(TextFormat("red simulada: %s", std::string(simulated_net_name(level_)).c_str()));
        line(TextFormat("cámara: %s", ctx_.view == 1   ? "persecución"
                                      : ctx_.mode.zone ? "toda la zona"
                                                       : "fija"));
    }

    DemoContext ctx_;
    DemoCamera camera_;
    ZoneDraw zone_draw_;
    net::Copilot net_;
    std::unique_ptr<sim::SimWorld> world_;
    std::int64_t start_ns_ = 0;
    std::string last_status_;
    std::uint8_t generation_ = 0;
    bool zone_matches_ = true;
    bool have_last_car_ = false;
    bool debug_visible_ = false;
    int help_page_ = -1;
    SimulatedNet level_ = SimulatedNet::Off;
};

} // namespace

int run_copilot(const CopilotArgs& args) {
    // Siempre encima de la ventana del juego (hay que pedirlo antes de abrir la ventana).
    SetConfigFlags(FLAG_WINDOW_TOPMOST);
    Window window(kWindowWidth, kWindowHeight, "Rally Coop · copiloto");
    place_window();
    FramePacer pacer;
    FixedStepClock clock;
    PerfCounter perf;
    CopilotApp app(args);
    while (!window.should_close()) {
        const std::int64_t frame_ns = pacer.begin_frame();
        perf.begin_frame(frame_ns, pacer.last_wait_ns());
        perf.record_steps(frame_ns, FrameSteps{});
        window.handle_keys();
        app.update_and_draw(perf, pacer, clock);
    }
    return 0;
}

} // namespace rally::platform
