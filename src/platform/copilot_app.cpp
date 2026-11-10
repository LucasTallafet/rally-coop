#include "platform/copilot_app.h"

#include "game/direction_changes.h"
#include "io/recording.h"
#include "net/copilot.h"
#include "platform/camera_space.h"
#include "platform/cockpit_draw.h"
#include "platform/copilot_keys.h"
#include "platform/copilot_view.h"
#include "platform/copilot_world.h"
#include "platform/cursor_capture.h"
#include "platform/debug_panel.h"
#include "platform/demo_camera.h"
#include "platform/demo_camera_keys.h"
#include "platform/demo_common.h"
#include "platform/demo_part.h"
#include "platform/demo_screen_keys.h"
#include "platform/fixed_step_clock.h"
#include "platform/frame_pacer.h"
#include "platform/gamepad.h"
#include "platform/input_shaping.h"
#include "platform/keys_raylib.h"
#include "platform/perf_counter.h"
#include "platform/pointer_mode.h"
#include "platform/scene_draw.h"
#include "platform/screen_layout.h"
#include "platform/stage_scene.h"
#include "platform/tablet_cursor.h"
#include "platform/tablet_draw.h"
#include "platform/tablet_geometry.h"
#include "platform/tablet_view.h"
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
#include <vector>

namespace rally::platform {

using namespace demo;

namespace {

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
void place_window(int width, int height) {
    SDL_Window* window = SDL_GL_GetCurrentWindow();
    if (window == nullptr) {
        return;
    }
    SDL_Rect usable{};
    if (!SDL_GetDisplayUsableBounds(SDL_GetDisplayForWindow(window), &usable)) {
        return;
    }
    // Acotada a la zona útil: con una ventana tan alta como el monitor, la barra de título no se
    // sale por arriba.
    SetWindowPosition(std::max(usable.x, usable.x + usable.w - width),
                      std::max(usable.y, usable.y + usable.h - height));
}

class CopilotApp {
public:
    explicit CopilotApp(const CopilotArgs& args) {
        ctx_.overlay.set_kind(ScreenKind::Copilot);
        ctx_.copilot_seat = true; // vista 0: el asiento derecho, la de arranque
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
        handle_keys(perf, now_s);
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
    void handle_keys(PerfCounter& perf, double now_s) {
        if (action_pressed(action(CameraAction::Camera), kCopilotKeyContext)) {
            ctx_.view = next_camera_view(ctx_.view, DemoCamera::camera_view_count(ctx_.mode));
            // asiento / persecución / toda la zona (o fija); en el tramo, también la fija
        }
        update_look(now_s);
        update_tablet_keys();
        if (action_pressed(action(ScreenAction::DebugPanel), kCopilotKeyContext)) {
            debug_.press();
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

    // Mirar con la cabeza (stick derecho del mando y ratón), solo con la ventana con el foco; el
    // ratón captura el cursor con el foco y lo suelta al perderlo (Alt+Tab). Solo se nota con la
    // cámara del asiento.
    void update_look(double now_s) {
        const bool focused = IsWindowFocused();
        const PadState pad = gamepads_.read(); // siempre: mantiene al día el mando activo
        pad_dpad_up_ = pad.dpad_up;
        pad_dpad_down_ = pad.dpad_down;
        // El puntero (Espacio mantenido): el ratón sale de la captura y la cabeza se congela; al
        // soltar, el ratón vuelve a mirar desde donde estaba.
        pointer_ = pointer_frame(
            focused && action_down(action(CopilotAction::Pointer), kCopilotKeyContext),
            mouse_use(false, true, focused) != MouseUse::None);
        ctx_.head_look.set_frozen(pointer_.frozen);
        HeadLookInput look;
        look.dt_s = std::max(0.0, now_s - last_look_s_);
        last_look_s_ = now_s;
        if (focused) {
            look.stick_x = pad.look.x;
            look.stick_y = pad.look.y;
        }
        cursor_.update(pointer_.capture);
        float dx = 0.0f;
        float dy = 0.0f;
        if (cursor_.take_delta(dx, dy)) {
            look.mouse_dx_px = dx;
            look.mouse_dy_px = dy;
        }
        look.recenter = cursor_.captured() && IsMouseButtonPressed(MOUSE_BUTTON_MIDDLE);
        ctx_.head_look.update(look);
    }

    // La orientación del mapa (O), el cursor (↑ y ↓ o la cruceta) y el clic del puntero.
    void update_tablet_keys() {
        if (action_pressed(action(CopilotAction::MapOrientation), kCopilotKeyContext)) {
            orientation_ =
                orientation_ == MapOrientation::Axis ? MapOrientation::Ahead : MapOrientation::Axis;
            ctx_.overlay.set_message(
                Overlay::Message::Settings,
                orientation_ == MapOrientation::Axis
                    ? "O: mapa orientado por la carretera"
                    : "O: mapa orientado hacia el punto del eje 40 m por delante",
                kText, kShortMessageNs);
        }
        const bool focused = IsWindowFocused();
        const bool up =
            focused &&
            (action_down(action(CopilotAction::CursorNext), kCopilotKeyContext) || pad_dpad_up_);
        const bool down =
            focused && (action_down(action(CopilotAction::CursorPrevious), kCopilotKeyContext) ||
                        pad_dpad_down_);
        held_dir_ = up == down ? 0 : (up ? 1 : -1);
        pointer_click_ = pointer_.clicks && IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
        pointer_x_ = static_cast<double>(GetMouseX());
        pointer_y_ = static_cast<double>(GetMouseY());
    }

    // Construye el mundo con el mismo código que el host y sin llamar nunca a step.
    void build_world(const net::WorldInfo& info) {
        CopilotWorld built = build_copilot_world(info);
        if (!built.ok) {
            ctx_.overlay.set_message(Overlay::Message::Car, built.error, kError);
            return;
        }
        world_.reset();
        ctx_.stage_track = nullptr;
        ctx_.stage_overview.reset();
        std::unique_ptr<sim::SimWorld> world = std::move(built.world);
        const sim::WorldDesc& desc = built.desc;
        ctx_.desc = desc;
        ctx_.car = built.car;
        ctx_.mode.set_kind(world->stage() != nullptr ? WorldKind::Stage
                           : desc.test_zone          ? WorldKind::Zone
                                                     : WorldKind::Arena);
        if (ctx_.view >= DemoCamera::camera_view_count(ctx_.mode)) {
            ctx_.view = 0;
        }
        if (world->zone() != nullptr) {
            zone_draw_.build(*world->zone(), false);
        }
        zone_matches_ = built.zone_matches;
        if (!zone_matches_) {
            ctx_.overlay.set_message(
                Overlay::Message::Car,
                "la zona no coincide con la del conductor; se dibuja la recibida", kError,
                kSettingsMessageNs);
        }
        // El tramo se dibuja con el suelo que construye esta ventana (como la zona: «se dibuja la
        // recibida»); el hash del host solo se compara para decir si coincide.
        stage_present_ = world->stage() != nullptr;
        stage_matches_ = built.stage_matches;
        if (stage_present_) {
            std::string error;
            bool loaded = false;
            if (!stage_scene_.prepare(*world->stage(), desc.stage_text, world->stage_ground_hash(),
                                      error, loaded)) {
                ctx_.overlay.set_message(Overlay::Message::Stage, "tramo: no se dibuja: " + error,
                                         kError, kSettingsMessageNs);
            }
            if (!stage_matches_) {
                ctx_.overlay.set_message(
                    Overlay::Message::Stage,
                    "el tramo no coincide con el del conductor; se dibuja el recibido", kError,
                    kSettingsMessageNs);
            }
            ctx_.stage_track = world->stage();
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
        have_sample_ = false;
        extra_ = net::SampledState{}; // el volante del mundo anterior no se queda
        tablet_view_.reset();
        tablet_cursor_.reset();
        changes_.clear();
        if (stage_present_) {
            changes_ = game::direction_changes(ctx_.stage_track->axis());
            tablet_draw_.set_stage(*ctx_.stage_track);
        }
        tablet_drawn_ = false;
        cockpit_ = build_cockpit(built.car);
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
        if (!net_.buffer().sample(now_s, sample, extra_)) {
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
        have_sample_ = true;
        update_tablet(now_s);
    }

    // La tablet con la `s` y la `t` que manda el host (nunca una proyección propia).
    void update_tablet(double now_s) {
        if (!stage_present_ || ctx_.stage_track == nullptr) {
            return;
        }
        const sim::StageTrack& track = *ctx_.stage_track;
        tablet_view_.update(track, extra_.s, extra_.t, now_s, orientation_);
        const double dt = std::max(0.0, now_s - last_tablet_s_);
        last_tablet_s_ = now_s;
        tablet_cursor_.update(changes_, extra_.s, track.axis().length(), held_dir_, dt);
        // Un clic del puntero coloca el cursor en el eje más cercano, en lo que se ve.
        // Solo cuenta si la tablet se dibujó en el último fotograma (con la ayuda abierta no).
        if (pointer_click_ && tablet_drawn_) {
            double s = 0.0;
            const double max_px = ctx_.overlay.metrics().px(12.0);
            if (pick_s(track, tablet_draw_.map_view(), extra_.s, pointer_x_, pointer_y_, max_px,
                       s)) {
                tablet_cursor_.place(s, extra_.s, track.axis().length());
            }
        }
    }

    void draw_scene() {
        const Vec3d& origin = ctx_.frame_camera.origin;
        const ScenePoses& p = ctx_.poses;
        if (ctx_.mode.stage) {
            stage_scene_.draw(ctx_.frame_camera.eye, origin,
                              ctx_.view == 2 && ctx_.stage_overview
                                  ? ctx_.stage_overview->draw_distance
                                  : kStageDrawDistance);
        } else if (ctx_.mode.zone) {
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
        if (ctx_.view == 0 && !cockpit_.empty()) {
            // Interior del conductor con lo que llega por la red: volante y pantalla.
            const CopilotCockpit c = copilot_cockpit(extra_, ctx_.car);
            cockpit_frame_ = cockpit_;
            append_steering_wheel(cockpit_frame_, ctx_.car, c.wheel_angle);
            append_display(cockpit_frame_, ctx_.car, c.display);
            draw_cockpit(cockpit_frame_, p.drawn_car, origin);
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
        tablet_drawn_ = false;
        if (help_page_ >= 0) {
            draw_help();
        } else {
            // La tablet va debajo de F3 y de F1 (que se dibujan encima, con su fondo).
            if (tablet_visible_ && stage_present_ && have_sample_ && tablet_view_.initialized() &&
                ctx_.stage_track != nullptr) {
                tablet_draw_.draw(overlay, *ctx_.stage_track, tablet_view_, tablet_cursor_,
                                  changes_, orientation_);
                tablet_drawn_ = true;
                const std::int64_t now = now_ns();
                tablet_cost_.add(now,
                                 tablet_draw_.last_geometry_ns() + tablet_draw_.last_draw_ns());
                if (now - last_tablet_publish_ns_ >= PerfStats::kPeriodNs) {
                    tablet_cost_.publish(now);
                    last_tablet_publish_ns_ = now;
                }
            }
            perf.set_tablet(&tablet_cost_);
            if (debug_.visible()) {
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
        DebugText text;
        const auto line = [&text](const std::string& s, Color color = kText) {
            text.add(s, color);
        };
        const net::LinkStats link = net_.stats();
        const net::BufferStats buffer = net_.buffer().stats(now_s);
        text.block();
        line(TextFormat("estado: %s", std::string(net_.status()).c_str()));
        line(TextFormat("RTT: %.1f ms   estados: %llu", link.rtt_s * 1000.0,
                        static_cast<unsigned long long>(link.received)));
        line(TextFormat("perdidos: %llu   tardíos: %llu",
                        static_cast<unsigned long long>(buffer.missing),
                        static_cast<unsigned long long>(buffer.late)));
        line(TextFormat("retardo: %.1f ms   extrapola: %s", buffer.delay_s * 1000.0,
                        buffer.extrapolating ? "sí" : "no"));
        text.block();
        line(TextFormat("paso del host: %u   mundo: %d", buffer.host_step,
                        world_ ? static_cast<int>(generation_) : -1));
        line(!world_         ? "zona: sin mundo"
             : zone_matches_ ? "zona: coincide con la del conductor"
                             : "zona: no coincide con la del conductor",
             zone_matches_ || !world_ ? kText : kError);
        if (stage_present_) {
            line(stage_matches_ ? "tramo: hash OK" : "tramo: distinto",
                 stage_matches_ ? kText : kError);
        }
        text.block();
        line(TextFormat("red simulada: %s", std::string(simulated_net_name(level_)).c_str()));
        line(TextFormat("cámara: %s", ctx_.view == 0   ? "asiento"
                                      : ctx_.view == 1 ? "persecución"
                                      : ctx_.mode.stage
                                          ? (ctx_.view == 2 ? "todo el tramo" : "fija")
                                      : ctx_.mode.zone ? "toda la zona"
                                                       : "fija"));
        debug_.draw(ctx_.overlay, text, now_s);
    }

    DemoContext ctx_;
    DemoCamera camera_;
    ZoneDraw zone_draw_;
    StageScene stage_scene_;
    net::Copilot net_;
    std::unique_ptr<sim::SimWorld> world_;
    std::int64_t start_ns_ = 0;
    std::string last_status_;
    std::uint8_t generation_ = 0;
    bool zone_matches_ = true;
    bool stage_present_ = false; // el mundo recibido tiene tramo
    bool stage_matches_ = true;  // y su suelo es el del host (stage_ground_hash)
    bool have_last_car_ = false;
    net::SampledState extra_;
    std::vector<CockpitTri> cockpit_;       // interior fijo, al construir el mundo
    std::vector<CockpitTri> cockpit_frame_; // el del fotograma (se reutiliza)
    Gamepads gamepads_;
    CursorCapture cursor_;
    double last_look_s_ = 0.0;
    DebugPanel debug_;
    int help_page_ = -1;
    SimulatedNet level_ = SimulatedNet::Off;
    // La tablet (lote 6). «A la vista» vale siempre en este lote: guardarla y sacarla necesitan
    // los mensajes de acción por la red, que no existen todavía.
    bool tablet_visible_ = true;
    bool have_sample_ = false;  // ya llegó un estado del mundo actual
    bool tablet_drawn_ = false; // la tablet se dibujó en el último fotograma (vale el clic)
    TabletView tablet_view_;
    TabletCursor tablet_cursor_;
    TabletDraw tablet_draw_;
    std::vector<game::DirectionChange> changes_;
    MapOrientation orientation_ = MapOrientation::Axis;
    PerfChannel tablet_cost_;
    std::int64_t last_tablet_publish_ns_ = 0;
    double last_tablet_s_ = 0.0;
    PointerFrame pointer_;
    bool pad_dpad_up_ = false;
    bool pad_dpad_down_ = false;
    int held_dir_ = 0;
    bool pointer_click_ = false;
    double pointer_x_ = 0.0;
    double pointer_y_ = 0.0;
};

} // namespace

int run_copilot(const CopilotArgs& args) {
    // Siempre encima de la ventana del juego (hay que pedirlo antes de abrir la ventana).
    SetConfigFlags(FLAG_WINDOW_TOPMOST);
    Window window(args.width, args.height, "Rally Coop · copiloto");
    place_window(args.width, args.height);
    FramePacer pacer;
    FixedStepClock clock;
    PerfCounter perf;
    CopilotApp app(args);
    while (!window.should_close()) {
        const std::int64_t frame_ns = pacer.begin_frame();
        perf.begin_frame(frame_ns, pacer.last_wait_ns());
        perf.record_steps(frame_ns, FrameSteps{});
        window.handle_keys(kCopilotKeyContext);
        app.update_and_draw(perf, pacer, clock);
    }
    return 0;
}

} // namespace rally::platform
