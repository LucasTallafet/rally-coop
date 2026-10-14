#include "platform/demo_measure.h"

#include "platform/demo_common.h"
#include "platform/demo_measure_keys.h"
#include "platform/fixed_step_clock.h"
#include "platform/frame_pacer.h"
#include "platform/keys_raylib.h"
#include "platform/screenshot.h"
#include "sim/test_road.h"
#include "sim/test_zone.h"

#include <raylib.h>

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <optional>
#include <system_error>

namespace rally::platform {

using namespace demo;

namespace {

// --bench-zone: velocidad de la cámara por la pista ciega y altura del ojo.
constexpr double kZoneBenchSpeed = 100.0 / 3.6;
constexpr double kZoneBenchEyeHeight = 1.1;

bool parse_count(const char* text, std::uint64_t& value) {
    const std::from_chars_result parsed = std::from_chars(text, text + std::strlen(text), value);
    return parsed.ec == std::errc{} && *parsed.ptr == '\0';
}

} // namespace

// --golden <grabación> <paso> <salida.png> [--view eye|chase|fixed] [--help-page N] [--debug]
// [--graphs N]. Vacío si no está; false en `ok` si está mal escrito.
std::optional<GoldenFrame> parse_golden_args(int argc, char** argv, bool& ok) {
    ok = true;
    std::optional<GoldenFrame> golden;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--golden") == 0) {
            if (i + 3 >= argc) {
                ok = false;
                return golden;
            }
            golden.emplace();
            golden->recording = argv[i + 1];
            ok = ok && parse_count(argv[i + 2], golden->step);
            golden->output = argv[i + 3];
            i += 3;
        }
    }
    if (!golden) {
        return golden;
    }
    for (int i = 1; i < argc; ++i) {
        const bool has_value = i + 1 < argc;
        std::uint64_t n = 0;
        if (std::strcmp(argv[i], "--view") == 0 && has_value) {
            const char* view = argv[++i];
            golden->view = std::strcmp(view, "chase") == 0   ? 1
                           : std::strcmp(view, "fixed") == 0 ? 2
                                                             : 0;
            ok = ok && (golden->view != 0 || std::strcmp(view, "eye") == 0);
        } else if (std::strcmp(argv[i], "--help-page") == 0 && has_value) {
            ok = ok && parse_count(argv[++i], n) && n >= 1;
            golden->help_page = static_cast<int>(n) - 1;
        } else if (std::strcmp(argv[i], "--graphs") == 0 && has_value) {
            ok = ok && parse_count(argv[++i], n);
            golden->graphs = static_cast<int>(n);
        } else if (std::strcmp(argv[i], "--debug") == 0) {
            golden->debug = true;
        }
    }
    return golden;
}

DemoMeasure::DemoMeasure(DemoContext& ctx) : ctx_(ctx) { ctx.frame_log = &frame_log_; }

DemoMeasure::~DemoMeasure() {
    if (ctx_.target.id != 0) {
        UnloadRenderTexture(ctx_.target);
    }
}

void DemoMeasure::set_render_size(DemoContext& ctx, int width, int height) {
    if (ctx.target.id != 0) {
        UnloadRenderTexture(ctx.target);
    }
    ctx.target = LoadRenderTexture(width, height);
    SetTextureFilter(ctx.target.texture, TEXTURE_FILTER_BILINEAR);
}

void DemoMeasure::start_zone_bench(DemoContext& ctx, bool road) {
    if (!ctx.mode.zone || ctx.mode.blind) {
        ctx.mode.zone = true;
        ctx.mode.blind = false;
        ctx.rebuild = Rebuild::Describe;
    }
    ctx.zone_bench = true;
    zone_bench_road_ = road;
    zone_bench_ns_ = 0;
    zone_bench_measuring_ = false;
    zone_bench_ground_ms_.reserve(static_cast<std::size_t>(kZoneBenchMeasureNs / 1'000'000));
}

void DemoMeasure::handle_keys(DemoContext& ctx, FramePacer& pacer, PerfCounter& /*perf*/) {
    const std::uint8_t context = ctx.frame_key_context;
    if (action_pressed(action(MeasureAction::FrameRate), context)) {
        pacer.set_target(pacer.target() == FrameTarget::MonitorRate ? FrameTarget::Fps30
                                                                    : FrameTarget::MonitorRate);
    }
    if (action_pressed(action(MeasureAction::Interpolation), context)) {
        ctx.interpolate = !ctx.interpolate;
    }
    if (action_pressed(action(MeasureAction::Vsync), context)) {
        pacer.set_vsync(!pacer.vsync());
    }
    if (action_pressed(action(MeasureAction::Pause), context)) {
        ctx.paused = !ctx.paused;
        if (ctx.paused) {
            ctx.overlay.set_message(Overlay::Message::Pause, "PAUSA (P)", kStrong);
        } else {
            ctx.overlay.clear_message(Overlay::Message::Pause);
        }
    }
    if (action_pressed(action(MeasureAction::Capture), context)) {
        capture_requested_ = true;
    }
    if (action_pressed(action(MeasureAction::Forest), context)) {
        ctx.forest_mode = next_forest_mode(ctx.forest_mode);
    }
    if (action_pressed(action(MeasureAction::Snap), context)) {
        ctx.snap = !ctx.snap;
    }
    if (action_pressed(action(MeasureAction::FrameLog), context)) {
        if (frame_log_.active()) {
            frame_log_.stop();
        } else {
            frame_log_.start();
        }
    }
}

void DemoMeasure::begin_frame(DemoContext& ctx, const DemoFrame& frame) {
    frame_log_.write(frame.frame_ns, frame.steps, frame.clock.debt(), frame.clock.snap_period(),
                     frame.pacer.frame_period());
    ctx.camera_override.reset();
    if (!ctx.zone_bench) {
        return;
    }
    // --bench-zone: el ojo recorre el eje de la pista ciega a 100 km/h; --bench-road, el de la
    // carretera de prueba, de la entrada a la frenada y vuelta a empezar.
    advance_zone_bench(ctx, frame.frame_ns);
    const double travelled = static_cast<double>(zone_bench_ns_) * 1e-9 * kZoneBenchSpeed;
    sim::AxisPoint p;
    if (zone_bench_road_) {
        const sim::TestRoad& road = ctx.world->zone()->road();
        const double s = road.first_s() + std::fmod(travelled, road.last_s() - road.first_s());
        const sim::RoadPoint q = road.point(s);
        p = {q.x, q.z, q.psi};
    } else {
        const sim::ZoneArea& track = ctx.world->zone()->area(sim::AreaId::BlindTrack);
        const double length = sim::TestZone::axis_length(track);
        p = sim::TestZone::axis_point(track, std::fmod(travelled, length));
    }
    const Vec3d eye{p.x, ctx.world->zone()->ground_height(p.x, p.z) + kZoneBenchEyeHeight, p.z};
    const Vec3d forward{std::sin(p.psi), -0.05, -std::cos(p.psi)};
    ctx.camera_override = std::pair{eye, forward};
}

void DemoMeasure::draw_3d(DemoContext& ctx, const DemoFrame& /*frame*/) {
    if (ctx.mode.zone) {
        return;
    }
    // El bosque rodea el recinto: su centro es el del suelo.
    forest_.draw(ctx.forest_mode,
                 relative_to_camera(Vec3d{ctx.desc.bodies[0].x, 0.0, ctx.desc.bodies[0].z},
                                    ctx.frame_camera.origin));
}

void DemoMeasure::end_frame(DemoContext& ctx, const DemoFrame& /*frame*/) {
    if (capture_requested_) {
        if (ctx.golden != nullptr) {
            save_screenshot_to(ctx.golden->output);
        } else {
            save_screenshot();
        }
        capture_requested_ = false;
    }
}

void DemoMeasure::advance_zone_bench(DemoContext& ctx, std::int64_t frame_ns) {
    if (zone_bench_done_) {
        return;
    }
    zone_bench_ns_ += frame_ns;
    if (!zone_bench_measuring_ && zone_bench_ns_ >= kZoneBenchWarmupNs) {
        zone_bench_measuring_ = true;
        ctx.zone_draw->reset_totals();
        return;
    }
    if (zone_bench_measuring_) {
        const double ms = static_cast<double>(frame_ns) / 1e6;
        zone_bench_frame_ms_total_ += ms;
        zone_bench_frame_ms_max_ = std::max(zone_bench_frame_ms_max_, ms);
        ++zone_bench_frames_;
        // CPU del dibujo del suelo del fotograma anterior (este aún no se ha dibujado).
        if (zone_bench_ground_ms_.size() < zone_bench_ground_ms_.capacity()) {
            zone_bench_ground_ms_.push_back(
                {ctx.zone_draw->last_cpu_ms(), ms, ctx.zone_draw->last_breakdown()});
        }
        if (zone_bench_ns_ >= kZoneBenchWarmupNs + kZoneBenchMeasureNs) {
            write_zone_bench_report(ctx);
            zone_bench_done_ = true;
        }
    }
}

void DemoMeasure::write_zone_bench_report(const DemoContext& ctx) const {
    int number = 1;
    const char* name = zone_bench_road_ ? "carretera" : "zona";
    while (FileExists(
        TextFormat("%s/rendimiento_%s_%03d.txt", GetApplicationDirectory(), name, number))) {
        ++number;
    }
    std::FILE* file = nullptr;
    if (fopen_s(&file,
                TextFormat("%s/rendimiento_%s_%03d.txt", GetApplicationDirectory(), name, number),
                "w") != 0 ||
        file == nullptr) {
        TraceLog(LOG_WARNING, "no se pudo escribir el informe de la zona");
        return;
    }
    const double frames = static_cast<double>(std::max<std::int64_t>(zone_bench_frames_, 1));
    std::fprintf(file, "Medida del dibujo del suelo de la zona de pruebas (%s)\n",
                 zone_bench_road_ ? "--bench-road" : "--bench-zone");
    std::fprintf(file, "Cámara por el eje de %s a 100 km/h, ojo a %.1f m\n",
                 zone_bench_road_ ? "la carretera de prueba (con sus árboles)" : "la pista ciega",
                 kZoneBenchEyeHeight);
    std::fprintf(file, "Render: %d x %d, pantalla: %s\n",
                 ctx.target.id != 0 ? ctx.target.texture.width : GetScreenWidth(),
                 ctx.target.id != 0 ? ctx.target.texture.height : GetScreenHeight(),
                 IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE) ? "completa sin bordes" : "ventana");
    std::fprintf(file, "Fotogramas medidos: %lld en %.1f s (tras %.1f s de calentamiento)\n",
                 static_cast<long long>(zone_bench_frames_),
                 static_cast<double>(kZoneBenchMeasureNs) / 1e9,
                 static_cast<double>(kZoneBenchWarmupNs) / 1e9);
    std::fprintf(file, "Fotograma: media %.3f ms, máximo %.3f ms\n",
                 zone_bench_frame_ms_total_ / frames, zone_bench_frame_ms_max_);
    std::fprintf(file, "CPU del dibujo del suelo: media %.3f ms, máximo %.3f ms (límite 1 ms)\n",
                 ctx.zone_draw->total_mean_ms(), ctx.zone_draw->total_max_ms());
    std::vector<double> sorted;
    sorted.reserve(zone_bench_ground_ms_.size());
    double cells = 0.0;
    double near_ms = 0.0;
    for (const ZoneBenchFrame& f : zone_bench_ground_ms_) {
        sorted.push_back(f.ground_ms);
        cells += f.breakdown.cells;
        near_ms += f.breakdown.near_ms;
    }
    std::sort(sorted.begin(), sorted.end());
    if (!sorted.empty()) {
        const std::size_t p99 = std::min(sorted.size() - 1, sorted.size() * 99 / 100);
        const auto over =
            std::count_if(sorted.begin(), sorted.end(), [](double ms) { return ms > 1.0; });
        const auto count = static_cast<double>(sorted.size());
        std::fprintf(file,
                     "CPU del dibujo del suelo: p99 %.3f ms; %lld de %zu fotogramas por encima "
                     "de 1 ms\n",
                     sorted[p99], static_cast<long long>(over), sorted.size());
        std::fprintf(file, "Rejilla cercana por fotograma: media %.1f celdas nuevas, %.3f ms\n",
                     cells / count, near_ms / count);
    }
    // Desglose de los fotogramas por encima de 1 ms (y del más lento).
    std::fprintf(file, "\nFotogramas lentos (suelo ms | rejilla ms | subidas ms | celdas | "
                       "subidas | fotograma ms):\n");
    double worst = -1.0;
    std::size_t worst_index = 0;
    for (std::size_t i = 0; i < zone_bench_ground_ms_.size(); ++i) {
        const ZoneBenchFrame& f = zone_bench_ground_ms_[i];
        if (f.ground_ms > worst) {
            worst = f.ground_ms;
            worst_index = i;
        }
    }
    for (std::size_t i = 0; i < zone_bench_ground_ms_.size(); ++i) {
        const ZoneBenchFrame& f = zone_bench_ground_ms_[i];
        if (f.ground_ms > 1.0 || i == worst_index) {
            std::fprintf(file, "  #%zu: %.3f | %.3f | %.3f | %d | %d | %.3f\n", i, f.ground_ms,
                         f.breakdown.near_ms, f.breakdown.upload_ms, f.breakdown.cells,
                         f.breakdown.uploads, f.frame_ms);
        }
    }
    std::fclose(file);
}

} // namespace rally::platform
