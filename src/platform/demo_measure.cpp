#include "platform/demo_measure.h"

#include "io/recording.h"
#include "platform/demo_common.h"
#include "platform/demo_measure_keys.h"
#include "platform/demo_world.h"
#include "platform/fixed_step_clock.h"
#include "platform/frame_pacer.h"
#include "platform/keys_raylib.h"
#include "platform/perf_counter.h"
#include "platform/screenshot.h"
#include "sim/test_road.h"
#include "sim/test_zone.h"

#include <raylib.h>

#include <algorithm>
#include <charconv>
#include <chrono>
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
        ctx.mode.set_kind(WorldKind::Zone);
        ctx.mode.blind = false;
        ctx.rebuild = Rebuild::Describe;
    }
    ctx.zone_bench = true;
    zone_bench_road_ = road;
    zone_bench_ns_ = 0;
    zone_bench_measuring_ = false;
    zone_bench_ground_ms_.reserve(static_cast<std::size_t>(kZoneBenchMeasureNs / 1'000'000));
}

void DemoMeasure::start_stage_bench(DemoContext& ctx) {
    // La carretera de referencia: pone la zona y la medida por el eje de la carretera.
    start_zone_bench(ctx, true);
    const auto fail = [this](const std::string& message) {
        TraceLog(LOG_ERROR, "--bench-stage: %s", message.c_str());
        zone_bench_done_ = true;
    };
    std::string text;
    std::string error;
    if (!io::read_file(find_data_file("data/stages/lazadas.stage"), text, error)) {
        fail(error);
        return;
    }
    stage_track_ = std::make_unique<sim::StageTrack>();
    const sim::StageParseResult parsed = sim::StageTrack::build(text, *stage_track_);
    if (!parsed.ok) {
        fail(parsed.error);
        return;
    }
    stage_length_ = stage_track_->axis().length();
    {
        // El hash del suelo que da la física, con un mundo de solo tramo que se libera enseguida.
        sim::WorldDesc desc;
        desc.stage_text = text;
        sim::SimWorld world;
        world.build(desc);
        world.seal();
        stage_ground_hash_ = world.stage_ground_hash();
    }
    using Clock = std::chrono::steady_clock;
    const Clock::time_point t0 = Clock::now();
    stage_draw_ = std::make_unique<StageDraw>();
    if (!stage_draw_->build(*stage_track_, stage_ground_hash_, error)) {
        fail(error);
        return;
    }
    stage_build_ms_ = std::chrono::duration<double, std::milli>(Clock::now() - t0).count();
    ctx.stage_draw = stage_draw_.get();
    stage_bench_ = StageBenchPhase::Stage;
    stage_ns_ = 0;
    stage_data_.frames.reserve(100000);
    road_data_.frames.reserve(40000);
}

void DemoMeasure::collect(StageBenchData& data, const DemoFrame& frame, double draw_ms,
                          double near_ms, int near_cells, std::int64_t triangles) {
    const FrameSample& sample = frame.perf.last_sample();
    data.totals.add(sample);
    data.memory.working_set = std::max(data.memory.working_set, frame.perf.memory().working_set);
    data.memory.private_bytes =
        std::max(data.memory.private_bytes, frame.perf.memory().private_bytes);
    if (data.frames.size() < data.frames.capacity()) {
        data.frames.push_back(
            {static_cast<double>(sample.frame_ns) / 1e6, draw_ms, near_ms, near_cells, triangles});
    }
}

void DemoMeasure::advance_stage_bench(DemoContext& ctx, const DemoFrame& frame) {
    stage_renderer_ = frame.perf.renderer();
    const bool was_measuring = stage_ns_ >= kZoneBenchWarmupNs;
    stage_ns_ += frame.frame_ns;
    const bool measuring = stage_ns_ >= kZoneBenchWarmupNs;
    const NearStats& near_stats = stage_draw_->last_near();
    if (!measuring) {
        // El primer dibujo llena todo el anillo.
        stage_full_fill_ms_ = std::max(stage_full_fill_ms_, near_stats.near_ms);
    } else if (!was_measuring) {
        stage_draw_->reset_totals();
    } else if (stage_skip_ > 0) {
        --stage_skip_; // el fotograma de una captura no cuenta
    } else {
        collect(stage_data_, frame, stage_draw_->last_cpu_ms(), near_stats.near_ms,
                near_stats.cells, stage_draw_->last_triangles());
    }
    const double travelled =
        measuring ? static_cast<double>(stage_ns_ - kZoneBenchWarmupNs) * 1e-9 * kStageBenchSpeed
                  : 0.0;
    const double s = kStageBenchStartS + travelled;
    if (s >= stage_length_) {
        // Meta: sigue la carretera de prueba.
        stage_bench_ = StageBenchPhase::Road;
        ctx.stage_draw = nullptr;
        zone_bench_ns_ = 0;
        zone_bench_measuring_ = false;
        return;
    }
    // Capturas desde el asiento en una recta, en la horquilla y en la salida de las lazadas
    // (junto al exe, como la tecla C): una vez al pasar cada s.
    constexpr double kCaptureS[3] = {50.0, 740.0, 1000.0};
    if (measuring && stage_captures_ < 3 && s >= kCaptureS[stage_captures_]) {
        capture_requested_ = true;
        ++stage_captures_;
        stage_skip_ = 3;
    }
    const sim::StagePose pose = stage_track_->pose(s);
    double ground = pose.y;
    stage_draw_->ground_height(pose.x, pose.z, ground);
    const Vec3d eye{pose.x, ground + kZoneBenchEyeHeight, pose.z};
    const Vec3d forward{std::sin(pose.heading), -0.05, -std::cos(pose.heading)};
    ctx.camera_override = std::pair{eye, forward};
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
    if (stage_bench_ == StageBenchPhase::Stage) {
        advance_stage_bench(ctx, frame);
        return;
    }
    // --bench-zone: el ojo recorre el eje de la pista ciega a 100 km/h; --bench-road, el de la
    // carretera de prueba, de la entrada a la frenada y vuelta a empezar.
    if (stage_bench_ == StageBenchPhase::Road && zone_bench_measuring_) {
        const ZoneDraw::Breakdown& b = ctx.zone_draw->last_breakdown();
        collect(road_data_, frame, ctx.zone_draw->last_cpu_ms(), b.near_ms, b.cells, 0);
    }
    stage_renderer_ = frame.perf.renderer();
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
    if (ctx.mode.kind() != WorldKind::Arena) {
        return; // el bosque rodea el recinto: ni la zona ni el tramo tienen cuerpos (bodies[0])
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
            if (stage_bench_ == StageBenchPhase::Road) {
                write_stage_bench_report(ctx);
            } else {
                write_zone_bench_report(ctx);
            }
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

namespace {

double mb_of(std::uint64_t bytes) { return static_cast<double>(bytes) / (1024.0 * 1024.0); }

double p99_of(std::vector<double> values) {
    if (values.empty()) {
        return 0.0;
    }
    std::sort(values.begin(), values.end());
    return values[std::min(values.size() - 1, values.size() * 99 / 100)];
}

double mean_of(const std::vector<double>& values) {
    double sum = 0.0;
    for (const double v : values) {
        sum += v;
    }
    return values.empty() ? 0.0 : sum / static_cast<double>(values.size());
}

double max_of(const std::vector<double>& values) {
    return values.empty() ? 0.0 : *std::max_element(values.begin(), values.end());
}

} // namespace

void DemoMeasure::write_stage_bench_report(const DemoContext& ctx) const {
    int number = 1;
    while (FileExists(
        TextFormat("%s/rendimiento_tramo_%03d.txt", GetApplicationDirectory(), number))) {
        ++number;
    }
    std::FILE* file = nullptr;
    if (fopen_s(&file,
                TextFormat("%s/rendimiento_tramo_%03d.txt", GetApplicationDirectory(), number),
                "w") != 0 ||
        file == nullptr) {
        TraceLog(LOG_WARNING, "no se pudo escribir el informe del tramo");
        return;
    }
    std::fprintf(file, "# Medida del dibujo del tramo (--bench-stage)\n\n");
    std::fprintf(file, "- Gráfica: %s\n", stage_renderer_);
    std::fprintf(file, "- Render: %d x %d, pantalla: %s\n",
                 ctx.target.id != 0 ? ctx.target.texture.width : GetScreenWidth(),
                 ctx.target.id != 0 ? ctx.target.texture.height : GetScreenHeight(),
                 IsWindowState(FLAG_BORDERLESS_WINDOWED_MODE) ? "completa sin bordes" : "ventana");
    std::fprintf(file,
                 "- Tramo: data/stages/lazadas.stage, %.0f m; cámara por el eje de s = %.0f m a "
                 "la meta a %.0f km/h, ojo a %.1f m (sin simular el coche)\n",
                 stage_length_, kStageBenchStartS, kStageBenchSpeed * 3.6, kZoneBenchEyeHeight);
    std::fprintf(file,
                 "- Referencia: la carretera de prueba de la 2.6b (como --bench-road, a %.0f "
                 "km/h), en la misma ejecución\n",
                 kZoneBenchSpeed * 3.6);
    std::fprintf(file, "- Distancia de dibujo del tramo: %.0f m; rejilla cercana: radio %.0f m\n",
                 kStageDrawDistance, StageDraw::kNearRadius);
    std::fprintf(file, "- Hash del suelo (física = dibujo): 0x%016llx\n",
                 static_cast<unsigned long long>(stage_ground_hash_));
    std::fprintf(file,
                 "- StageDraw::build: %.0f ms; %d trozos, %lld triángulos en total, %.1f MB de "
                 "búferes de vértices e índices\n",
                 stage_build_ms_, stage_draw_->chunk_count(),
                 static_cast<long long>(stage_draw_->triangle_total()),
                 mb_of(stage_draw_->gpu_bytes()));
    std::fprintf(file, "- Primer dibujo (llena todo el anillo de 241² celdas): %.1f ms\n\n",
                 stage_full_fill_ms_);
    struct Row {
        const char* name;
        const StageBenchData* data;
    };
    const Row rows[2] = {{"Tramo (lazadas)", &stage_data_},
                         {"Carretera de prueba (2.6b)", &road_data_}};
    for (const Row& row : rows) {
        const StageBenchData& d = *row.data;
        std::vector<double> frame_ms;
        std::vector<double> draw_ms;
        std::vector<double> near_ms;
        std::vector<double> cells;
        std::vector<double> triangles;
        for (const StageBenchFrame& f : d.frames) {
            frame_ms.push_back(f.frame_ms);
            draw_ms.push_back(f.draw_ms);
            near_ms.push_back(f.near_ms);
            cells.push_back(f.near_cells);
            triangles.push_back(static_cast<double>(f.triangles));
        }
        const PerfTotals& t = d.totals;
        std::fprintf(file, "## %s\n\n", row.name);
        std::fprintf(file, "- Fotogramas medidos: %lld\n", static_cast<long long>(t.frames));
        std::fprintf(file, "- Fotograma ms: media %.3f, p99 %.3f, máximo %.3f\n", mean_of(frame_ms),
                     p99_of(frame_ms), max_of(frame_ms));
        std::fprintf(file, "- CPU del dibujo ms: media %.3f, p99 %.3f, máximo %.3f\n",
                     mean_of(draw_ms), p99_of(draw_ms), max_of(draw_ms));
        std::fprintf(file,
                     "- De ello, la rejilla cercana ms: media %.3f, p99 %.3f, máximo %.3f; celdas "
                     "nuevas por fotograma: media %.1f, máximo %.0f\n",
                     mean_of(near_ms), p99_of(near_ms), max_of(near_ms), mean_of(cells),
                     max_of(cells));
        std::fprintf(file, "- GPU ms: media %.3f, máximo %.3f\n", t.gpu.mean() / 1e6,
                     static_cast<double>(t.gpu.max) / 1e6);
        std::fprintf(file, "- Draw calls: media %.1f, máximo %lld\n", t.draw_calls.mean(),
                     static_cast<long long>(t.draw_calls.max));
        if (max_of(triangles) == 0.0) {
            std::fprintf(file, "- Triángulos dibujados: no medidos (ZoneDraw no los cuenta)\n");
        } else {
            std::fprintf(file, "- Triángulos dibujados: media %.0f, máximo %.0f\n",
                         mean_of(triangles), max_of(triangles));
        }
        std::fprintf(file,
                     "- Memoria del proceso (máx.): working set %.0f MB, privados %.0f MB\n\n",
                     mb_of(d.memory.working_set), mb_of(d.memory.private_bytes));
    }
    std::fclose(file);
}

} // namespace rally::platform
