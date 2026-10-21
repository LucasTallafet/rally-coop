#include "platform/demo_screen.h"

#include "platform/action_driver.h"
#include "platform/audio_output.h"
#include "platform/demo_common.h"
#include "platform/demo_screen_keys.h"
#include "platform/fixed_step_clock.h"
#include "platform/forest_layout.h"
#include "platform/frame_log.h"
#include "platform/frame_pacer.h"
#include "platform/gamepad.h"
#include "platform/input.h"
#include "platform/input_shaping.h"
#include "platform/keys_raylib.h"
#include "platform/mouse_steer.h"
#include "platform/perf_counter.h"
#include "platform/recording_session.h"
#include "platform/screen_layout.h"
#include "platform/telemetry_graphs.h"
#include "platform/ui_text.h"
#include "platform/zone_draw.h"

#include <SDL3/SDL_video.h>
#include <raylib.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>

namespace rally::platform {

using namespace demo;

namespace {

// Barra de un eje en [-1, 1]: el valor aplicado relleno desde el centro y el objetivo como
// una marca blanca. Medidas del diseño de la pantalla (axis_bar_width).
void draw_axis_bar(int x, int y, int width, int height, float target, float applied) {
    const int center = x + width / 2;
    const auto offset = [width](float value) {
        return static_cast<int>(std::clamp(value, -1.0f, 1.0f) * static_cast<float>(width / 2));
    };
    DrawRectangle(x, y, width, height, Color{60, 64, 72, 255});
    const int fill = offset(applied);
    DrawRectangle(std::min(center, center + fill), y, std::abs(fill), height,
                  Color{230, 160, 60, 255});
    const int mark = std::max(1, height / 5);
    DrawRectangle(center + offset(target) - 1, y - mark, 3, height + 2 * mark, RAYWHITE);
    DrawRectangle(center, y, 1, height, Color{150, 156, 168, 255});
}

// Tiempo en pasos como segundos, o "--" si no hay.
struct LapText {
    char text[24];
};
LapText lap_text(std::int64_t steps) {
    LapText out{};
    if (steps < 0) {
        std::snprintf(out.text, sizeof(out.text), "--");
    } else {
        std::snprintf(out.text, sizeof(out.text), "%.2f s",
                      static_cast<double>(steps) / kStepsPerSecond);
    }
    return out;
}

} // namespace

DemoScreen::DemoScreen(const DemoContext& ctx) {
    if (ctx.golden != nullptr) {
        help_page_ = ctx.golden->help_page;
        debug_visible_ = ctx.golden->debug;
        for (int i = 0; i < ctx.golden->graphs; ++i) {
            graphs_.press(ctx.mode.blind, default_graph_pages());
        }
    }
}

void DemoScreen::handle_keys(DemoContext& ctx, FramePacer& /*pacer*/, PerfCounter& perf) {
    const std::uint8_t context = ctx.frame_key_context;
    if (action_pressed(action(ScreenAction::Help), context)) {
        // Páginas: 1 → 2 → … → oculta (desde la 2.7 la ayuda no cabe en una).
        const int pages = ctx.overlay.layout().help.pages;
        help_page_ = help_page_ + 1 < pages ? help_page_ + 1 : -1;
    }
    if (action_pressed(action(ScreenAction::DebugPanel), context)) {
        debug_visible_ = !debug_visible_;
    }
    if (action_pressed(action(ScreenAction::Graphs), context)) {
        graphs_.press(ctx.mode.blind, default_graph_pages());
    }
    if (action_pressed(action(ScreenAction::Perf), context)) {
        perf.set_visible(!perf.visible());
    }
}

void DemoScreen::on_world_rebuilt(DemoContext& /*ctx*/) { telemetry_history_.clear(); }

void DemoScreen::after_step(DemoContext& ctx) { telemetry_history_.push(ctx.world->telemetry()); }

void DemoScreen::draw_2d(DemoContext& ctx, const DemoFrame& frame) {
    // Texto en pantalla: cada capa en su sitio (platform/screen_layout).
    draw_indicator(ctx, frame);
    const UiRect& hint = ctx.overlay.rect(Layer::HelpHint);
    ctx.overlay.text(Layer::HelpHint, hint.x, hint.y, layer_samples(Layer::HelpHint)[0], kDim);
    // La ayuda ocupa el sitio de los paneles de arriba: mientras se ve, no se dibujan
    // (screen_layout, layers_exclusive). Siguen encendidos y vuelven al cerrarla.
    if (help_page_ >= 0) {
        draw_help(ctx);
    } else {
        if (debug_visible_) {
            draw_debug(ctx, frame);
        }
        if (graphs_.visible(ctx.mode.blind) >= 0) {
            draw_telemetry_graphs(telemetry_history_, graphs_.visible(ctx.mode.blind), ctx.overlay);
        }
        if (ctx.audio != nullptr) {
            frame.perf.set_audio(ctx.audio->stats());
        }
        const int render_width = ctx.target.id != 0 ? ctx.target.texture.width : GetScreenWidth();
        const int render_height =
            ctx.target.id != 0 ? ctx.target.texture.height : GetScreenHeight();
        frame.perf.draw(frame.pacer, frame.clock, ctx.snap, render_width, render_height,
                        ctx.overlay);
    }
    // El anillo, siempre y el último: encima de la ayuda si está abierta.
    draw_action_ring(ctx);
}

void DemoScreen::draw_action_ring(DemoContext& ctx) {
    // Anillo de progreso de la acción en curso del copiloto (tarea 2.9): lo llena la duración de
    // la acción (en pasos) hasta que haya animaciones. Se dibuja siempre, también con la ayuda
    // abierta: encima de ella y con un fondo propio (screen_layout, layers_overdraw).
    if (ctx.actions == nullptr) {
        return;
    }
    const RingState ring = ring_state(ctx.actions->copilot_progress());
    if (!ring.visible) {
        return;
    }
    const UiRect& r = ctx.overlay.rect(Layer::ActionRing);
    if (help_page_ >= 0) {
        ctx.overlay.panel(Layer::ActionRing, kPanelBackground);
    }
    const RingGeometry& g = ctx.overlay.layout().ring;
    const float outer = 0.5f * static_cast<float>(g.diameter);
    const float inner = outer - static_cast<float>(g.thickness);
    const Vector2 center{static_cast<float>(r.x) + 0.5f * static_cast<float>(r.w),
                         static_cast<float>(r.y) + outer};
    DrawRing(center, inner, outer, 0.0f, 360.0f, 48, Color{60, 64, 72, 215});
    DrawRing(center, inner, outer, -90.0f, -90.0f + 360.0f * ring.fraction, 48, kStrong);
    // «acción · fase», centrado bajo el anillo (recortado si el nombre es largo).
    const std::string& name = ctx.actions->table().actions[ring.action].name;
    const std::string label = name + " · " + std::string(phase_name(ring.phase));
    const int width = text_width(label, ctx.overlay.metrics().font);
    const int x = width <= r.w ? r.x + (r.w - width) / 2 : r.x;
    ctx.overlay.text_fit(Layer::ActionRing, x, r.y + g.label_y, label, kText);
}

void DemoScreen::draw_indicator(DemoContext& ctx, const DemoFrame& frame) {
    const DriverInput& driver = frame.driver;
    // Velocidad horizontal, marcha (con la del cambio en curso), rpm, presión del turbo y modo
    // de la caja tras el último paso (indicadores provisionales: el salpicadero llega en la 2.7);
    // la configuración, salvo a ciegas o con --hide-config; en la zona, la vuelta en curso y la
    // mejor (contadas en pasos por la simulación); a ciegas, la prueba.
    const sim::BodyVelocity v = ctx.world->car_velocity();
    const double speed = std::sqrt(static_cast<double>(v.vx * v.vx + v.vz * v.vz));
    const sim::DrivetrainOutput& drive = ctx.world->car_controller().drivetrain();
    const char* gear_names[] = {"R", "N", "1", "2", "3", "4", "5"};
    char gear_text[16];
    if (drive.shifting) {
        std::snprintf(gear_text, sizeof(gear_text), "%s>%s", gear_names[drive.gear + 1],
                      gear_names[drive.target_gear + 1]);
    } else {
        std::snprintf(gear_text, sizeof(gear_text), "%s", gear_names[drive.gear + 1]);
    }
    char text[240];
    int used =
        std::snprintf(text, sizeof(text), "%.0f km/h   %s %s   %4.0f rpm   %.2f bar", speed * 3.6,
                      gear_text, ctx.settings.sequential ? "sec." : "auto.",
                      static_cast<double>(drive.engine_omega) * 30.0 / 3.14159265358979323846,
                      static_cast<double>(drive.boost) * 1.0e-5);
    if (!ctx.mode.config_hidden()) {
        const std::filesystem::path stem = utf8_path(ctx.car_path).stem();
        const std::u8string name = stem.u8string();
        used += std::snprintf(text + used, sizeof(text) - static_cast<std::size_t>(used), "   %s",
                              std::string(name.begin(), name.end()).c_str());
    }
    if (ctx.mode.zone) {
        const sim::LapState& lap = *ctx.world->lap();
        used += std::snprintf(text + used, sizeof(text) - static_cast<std::size_t>(used),
                              "   vuelta %s   mejor %s", lap_text(lap.current()).text,
                              lap_text(lap.best_lap).text);
        // Carretera: la pasada en curso o, si no hay, la última, con sus recolocaciones.
        const sim::RoadRunState& run = *ctx.world->road_run();
        const bool running = run.run_start >= 0;
        if (running || run.last_run >= 0) {
            const std::int32_t resets = running ? run.resets : run.last_resets;
            used += std::snprintf(text + used, sizeof(text) - static_cast<std::size_t>(used),
                                  "   carretera %s%s", running ? "" : "última ",
                                  lap_text(running ? run.current() : run.last_run).text);
            if (resets > 0) {
                used += std::snprintf(text + used, sizeof(text) - static_cast<std::size_t>(used),
                                      " · %d recol.", resets);
            }
        }
    }
    if (ctx.mode.blind) {
        used += std::snprintf(text + used, sizeof(text) - static_cast<std::size_t>(used),
                              "   A CIEGAS · Prueba %d/%d", ctx.mode.blind_trial + 1,
                              sim::kBlindTrials);
    }
    if (driver.mouse_enabled()) {
        // Volante del ratón: la O es su posición, de tope a tope; en el centro, +. Sin capturar
        // (pausa o foco perdido), el volante está en el centro.
        std::snprintf(text + used, sizeof(text) - static_cast<std::size_t>(used),
                      "   volante [%s]%s", wheel_bar(driver.mouse().steer()).data(),
                      driver.mouse_captured() ? "" : " suelto");
    }
    const UiRect& r = ctx.overlay.rect(Layer::Indicator);
    ctx.overlay.text(Layer::Indicator, r.x, r.y, text, kStrong, true);
}

void DemoScreen::draw_help(DemoContext& ctx) {
    // Encabezados de bloque en el color de los títulos; las teclas que no valen en el contexto
    // actual, en gris.
    const ScreenLayout& layout = ctx.overlay.layout();
    const HelpGrid& grid = layout.help;
    const std::span<const KeyBinding> table = key_table();
    const std::uint8_t context = ctx.key_context();
    ctx.overlay.panel(Layer::Help, kPanelBackground);
    const int x0 = ctx.overlay.content_x(Layer::Help);
    const int y0 = ctx.overlay.content_y(Layer::Help);
    // Si la ventana cambia de tamaño, puede haber menos páginas.
    const int page = std::min(help_page_, grid.pages - 1);
    ctx.overlay.text(Layer::Help, x0, y0, help_title(page, grid.pages), kDim);
    for (const HelpCell& cell : grid.cells) {
        if (cell.page != page) {
            continue;
        }
        const int x = x0 + cell.column * (grid.column_width + grid.column_gap);
        const int y = y0 + cell.row * layout.m.line;
        if (cell.binding < 0) {
            ctx.overlay.text(Layer::Help, x, y, key_block_name(cell.block), kTitle);
            continue;
        }
        const KeyBinding& b = table[static_cast<std::size_t>(cell.binding)];
        const Color color = (b.contexts & context) != 0 ? kText : kUnavailable;
        ctx.overlay.text(Layer::Help, x, y, binding_label_short(b), color);
        ctx.overlay.text(Layer::Help, x + grid.text_x, y, b.text, color);
    }
}

void DemoScreen::draw_debug(DemoContext& ctx, const DemoFrame& frame) {
    const FrameSteps& steps = frame.steps;
    const FramePacer& pacer = frame.pacer;
    const DriverInput& driver = frame.driver;
    const sim::SimInput& input = frame.input;
    const float frame_impact = frame.frame_impact;
    const Vec3d& eye = ctx.frame_camera.eye;
    // Las mismas líneas que las muestras de layer_samples(Layer::Debug), en el mismo orden.
    const UiMetrics& m = ctx.overlay.metrics();
    ctx.overlay.panel(Layer::Debug, kPanelBackground);
    const int x = ctx.overlay.content_x(Layer::Debug);
    int y = ctx.overlay.content_y(Layer::Debug);
    const auto line = [&](const char* text, Color color = kText) {
        ctx.overlay.text(Layer::Debug, x, y, text, color);
        y += m.line;
    };
    const auto line_fit = [&](const char* text) {
        ctx.overlay.text_fit(Layer::Debug, x, y, text, kText);
        y += m.line;
    };

    line("DEPURACIÓN (F3)", kTitle);
    const Gamepads& pads = driver.gamepads();
    if (ctx.golden != nullptr) {
        line("mando: (fijo en --golden)");
        line("gatillos: (fijo en --golden)");
    } else if (pads.active_name() != nullptr) {
        line_fit(TextFormat("mando: %s (%s)", pads.active_name(), pads.active_type()));
        line(TextFormat("gatillos: RT %s, LT %s   conectados: %d",
                        pads.active_throttle_digital() ? "digital" : "analógico",
                        pads.active_brake_digital() ? "digital" : "analógico", pads.connected()));
    } else {
        line("mando: ninguno activo");
        line(TextFormat("gatillos: -   conectados: %d", pads.connected()));
    }
    // Barras: dirección, acelerador y freno; blanco el objetivo y naranja el aplicado.
    const sim::ControlState& controls = ctx.world->controls();
    constexpr float kAxisScale = 1.0f / sim::kAxisMax;
    const int bar_width = axis_bar_width(m);
    const int bar_height = m.font * 4 / 5;
    const int bar_y = y + (m.line - bar_height) / 2;
    draw_axis_bar(x, bar_y, bar_width, bar_height, input.steer * kAxisScale,
                  controls.steer.applied * kAxisScale);
    draw_axis_bar(x + bar_width + m.font, bar_y, bar_width, bar_height, input.throttle * kAxisScale,
                  controls.throttle.applied * kAxisScale);
    draw_axis_bar(x + 2 * (bar_width + m.font), bar_y, bar_width, bar_height,
                  input.brake * kAxisScale, controls.brake.applied * kAxisScale);
    y += m.line;
    const auto mode = [&input](std::uint16_t bit) {
        return (input.buttons & bit) != 0 ? "digital" : "analógico";
    };
    line(TextFormat("barras: dirección %s, acelerador %s, freno %s", mode(sim::kButtonSteerDigital),
                    mode(sim::kButtonThrottleDigital), mode(sim::kButtonBrakeDigital)));
    line(TextFormat("freno de mano: %3.0f %%   subir %d, bajar %d, recolocar %d",
                    100.0f * controls.handbrake * kAxisScale, ctx.shift_up_presses,
                    ctx.shift_down_presses, ctx.reset_presses));
    line(TextFormat("impacto: %.2f m/s   último que vibró: %.2f m/s (umbral %.1f)", frame_impact,
                    ctx.last_rumble_impact, kImpactThreshold));
    line_fit(TextFormat("grabación: %s   pasos: %llu", ctx.recording->name().c_str(),
                        static_cast<unsigned long long>(ctx.recording->steps())));
    if (ctx.mode.config_hidden()) {
        line("coche: (oculto)");
    } else {
        line_fit(TextFormat("coche: %s (%d kg)", file_name(ctx.car_path).c_str(),
                            static_cast<int>(ctx.car.mass)));
    }
    {
        const sim::DrivetrainOutput& d = ctx.world->car_controller().drivetrain();
        line_fit(TextFormat(
            "motor: %.0f rpm, %.2f bar, %.0f N·m (acel. %.2f); embrague %s%s, desliz. %.1f rad/s, "
            "%.0f N·m",
            static_cast<double>(d.engine_omega) * 30.0 / 3.14159265358979323846,
            static_cast<double>(d.boost) * 1.0e-5, static_cast<double>(d.engine_torque),
            static_cast<double>(d.throttle),
            d.clutch == sim::ClutchMode::Closed ? "cerrado" : "arranque",
            d.clutch_open ? " (abierto)" : "", static_cast<double>(d.clutch_slip),
            static_cast<double>(d.clutch_torque)));
        line_fit(TextFormat(
            "ejes: %.0f / %.0f N·m; central Δω %.2f rad/s, visco %.0f N·m; autobl. %.0f / %.0f "
            "N·m; LCP %d%s",
            static_cast<double>(d.axle_torque[0]), static_cast<double>(d.axle_torque[1]),
            static_cast<double>(d.center_slip), static_cast<double>(d.viscous_torque),
            static_cast<double>(d.lsd_torque[0]), static_cast<double>(d.lsd_torque[1]),
            d.lcp_iterations, d.lcp_failures > 0 ? " SIN CONVERGER" : ""));
    }

    // Zona: a ciegas no se dice la superficie ni la configuración.
    if (!ctx.mode.zone) {
        line("zona: recinto de la fase 1");
        line("vueltas: -");
        line("suelo (CPU): -");
    } else {
        const sim::TestZone& zone = *ctx.world->zone();
        if (ctx.mode.blind) {
            line("zona: a ciegas");
        } else {
            const Pose& car = ctx.poses.current_car;
            const int area = zone.area_at(car.position.x, car.position.z);
            const char* where =
                area < 0 ? "fondo" : zone.areas()[static_cast<std::size_t>(area)].name;
            line(TextFormat("zona: %s (%s)   pista ciega: %s   carretera: %s", where,
                            sim::surface_name(zone.surface_at(car.position.x, car.position.z)),
                            sim::surface_name(zone.blind_surface()),
                            sim::surface_name(zone.road_surface())));
        }
        const sim::LapState& lap = *ctx.world->lap();
        line(TextFormat("vueltas: %d   última: %s", lap.laps, lap_text(lap.last_lap).text));
        const sim::RoadRunState& run = *ctx.world->road_run();
        line(TextFormat("carretera: %d pasadas   última: %s (%d recol.)   mejor sin recolocar: %s",
                        run.runs, lap_text(run.last_run).text, run.last_resets,
                        lap_text(run.best_clean).text));
        if (ctx.golden != nullptr) {
            line("suelo (CPU): (fijo en --golden)");
        } else {
            line(TextFormat("suelo (CPU): %.2f ms   media %.2f ms   máx. %.2f ms",
                            ctx.zone_draw->last_cpu_ms(), ctx.zone_draw->mean_cpu_ms(),
                            ctx.zone_draw->max_cpu_ms()));
        }
    }

    const char* view_name = ctx.zone_bench   ? "medida del suelo"
                            : ctx.view == 0  ? "ojo del conductor"
                            : ctx.view == 1  ? "persecución"
                            : !ctx.mode.zone ? "fija"
                                             : "toda la zona";
    line(TextFormat("cámara: %s   mundo: %s", view_name,
                    ctx.mode.zone              ? "zona de pruebas"
                    : ctx.mode.far_from_origin ? "recinto a 20 km del origen"
                                               : "recinto en el origen"));
    line(ctx.world_float ? "render: FLOAT DEL MUNDO (depuración)" : "render: relativo a la cámara");
    line(ctx.forest_mode == ForestMode::Off
             ? TextFormat("bosque: %s", forest_mode_name(ctx.forest_mode))
             : TextFormat("bosque: %s, %d árboles de %d triáng.", forest_mode_name(ctx.forest_mode),
                          kForestTrees, kTreeTriangles));
    line(TextFormat("ojo: %.3f, %.3f, %.3f m", eye.x, eye.y, eye.z));
    line(TextFormat("asiento: alto %d cm, adelante %d cm, derecha %d cm", ctx.settings.seat.up,
                    ctx.settings.seat.forward, ctx.settings.seat.right));
    line(aids_summary(ctx.settings).c_str());

    const char* target =
        pacer.target() == FrameTarget::MonitorRate ? "frecuencia del monitor" : "30 fps";
    line(
        TextFormat("ritmo: %s   sincronización vertical: %s", target, pacer.vsync() ? "sí" : "no"));
    if (ctx.golden != nullptr) {
        line("monitor: (fijo en --golden)");
        line("temporizador: (fijo en --golden)");
    } else {
        int actual_interval = 0;
        const bool interval_known = SDL_GL_GetSwapInterval(&actual_interval);
        line(TextFormat("monitor: %.2f Hz   intervalo pedido %d, real (SDL) %s",
                        pacer.refresh_rate(), pacer.swap_interval(),
                        interval_known ? TextFormat("%d", actual_interval) : "desconocido"));
        line(!pacer.uses_timer()             ? "temporizador: no"
             : pacer.high_resolution_timer() ? "temporizador: de alta resolución"
                                             : "temporizador: de 1 ms");
    }
    line(TextFormat("interpolación: %s   pantalla: %s", ctx.interpolate ? "sí" : "no",
                    IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE) ? "completa sin bordes"
                                                                 : "ventana"));
    line(TextFormat("registro: %s", ctx.frame_log->active() ? ctx.frame_log->name() : "no"));
    line(TextFormat("pasos en el fotograma: %d   alpha: %.2f", steps.count, steps.alpha));
}

} // namespace rally::platform
