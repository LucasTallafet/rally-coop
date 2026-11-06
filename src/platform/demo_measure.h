#pragma once

#include "platform/demo_part.h"
#include "platform/forest_scene.h"
#include "platform/frame_log.h"
#include "platform/perf_stats.h"
#include "platform/process_memory.h"
#include "platform/stage_draw.h"
#include "platform/zone_draw.h"
#include "sim/stage_track.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

namespace rally::platform {

// --golden <grabación> <paso> <salida.png> [--view eye|chase|fixed] [--help-page N] [--debug]
// [--graphs N] (tools/ci/golden.ps1). Vacío si no está; false en `ok` si está mal escrito.
std::optional<GoldenFrame> parse_golden_args(int argc, char** argv, bool& ok);

// Medidas y diagnóstico de la demo: el bosque de medida alrededor del recinto (B,
// --bench-forest), el recorrido de la cámara por la pista ciega o por la carretera de prueba
// (--bench-zone, --bench-road), las capturas (C y --golden), el registro de fotogramas (L,
// --frame-log), el ritmo (F: 30 fps, V: sincronización vertical, N: ajuste del dt al refresco),
// la pausa (P) y la interpolación (I). También el dibujo a una RenderTexture de tamaño fijo.
class DemoMeasure final : public DemoPart {
public:
    explicit DemoMeasure(DemoContext& ctx);
    ~DemoMeasure() override;
    DemoMeasure(const DemoMeasure&) = delete;
    DemoMeasure& operator=(const DemoMeasure&) = delete;

    // --bench-zone: la cámara recorre la pista ciega a 100 km/h a la altura del ojo durante
    // kZoneBenchWarmupNs + kZoneBenchMeasureNs y escribe rendimiento_zona_NNN.txt junto al exe.
    // Con `road`, la carretera de prueba (--bench-road, rendimiento_carretera_NNN.txt). Pide la
    // zona sin el modo a ciegas si hace falta.
    static constexpr std::int64_t kZoneBenchWarmupNs = 3'000'000'000;
    static constexpr std::int64_t kZoneBenchMeasureNs = 20'000'000'000;
    void start_zone_bench(DemoContext& ctx, bool road);
    bool zone_bench_done() const { return zone_bench_done_; }
    // --bench-stage (tramo-dibujo): primero la cámara recorre lazadas de s = −5 m a la meta a
    // 60 km/h con el ojo a kZoneBenchEyeHeight, dibujando el tramo con StageDraw (sin simular el
    // coche); después, en la misma ejecución, la carretera de prueba como --bench-road. Escribe
    // rendimiento_tramo_NNN.txt junto al exe. Pide la zona para tener la carretera de referencia.
    static constexpr double kStageBenchSpeed = 60.0 / 3.6;
    static constexpr double kStageBenchStartS = -5.0;
    void start_stage_bench(DemoContext& ctx);
    void request_capture() { capture_requested_ = true; }
    void start_frame_log() { frame_log_.start(); }
    void set_forest_mode(DemoContext& ctx, ForestMode mode) { ctx.forest_mode = mode; }
    // Dibuja la escena en una RenderTexture de width × height y la muestra escalada a la
    // ventana (--bench-forest: medida a 1920 × 1080 en cualquier monitor).
    void set_render_size(DemoContext& ctx, int width, int height);

    void handle_keys(DemoContext& ctx, FramePacer& pacer, PerfCounter& perf) override;
    void begin_frame(DemoContext& ctx, const DemoFrame& frame) override;
    void draw_3d(DemoContext& ctx, const DemoFrame& frame) override;
    void end_frame(DemoContext& ctx, const DemoFrame& frame) override;

private:
    void advance_zone_bench(DemoContext& ctx, std::int64_t frame_ns);
    void write_zone_bench_report(const DemoContext& ctx) const;
    void advance_stage_bench(DemoContext& ctx, const DemoFrame& frame);
    void write_stage_bench_report(const DemoContext& ctx) const;

    DemoContext& ctx_;
    ForestScene forest_;             // B: escena de medida
    FrameLog frame_log_;             // L: registro de diagnóstico
    bool capture_requested_ = false; // C: captura del búfer al final del fotograma
    // --bench-zone: recorrido de la cámara y tiempo transcurrido.
    bool zone_bench_road_ = false; // --bench-road
    bool zone_bench_done_ = false;
    std::int64_t zone_bench_ns_ = 0;
    bool zone_bench_measuring_ = false;
    double zone_bench_frame_ms_total_ = 0.0;
    double zone_bench_frame_ms_max_ = 0.0;
    std::int64_t zone_bench_frames_ = 0;
    // CPU del suelo por fotograma medido, con su desglose.
    struct ZoneBenchFrame {
        double ground_ms = 0.0;
        double frame_ms = 0.0;
        ZoneDraw::Breakdown breakdown;
    };
    std::vector<ZoneBenchFrame> zone_bench_ground_ms_;

    // --bench-stage: el tramo y, después, la carretera de referencia. Cada fase junta lo que mide
    // el contador (PerfTotals) y la CPU de su dibujo por fotograma.
    enum class StageBenchPhase : std::uint8_t { Off, Stage, Road };
    struct StageBenchFrame {
        double frame_ms = 0.0;
        double draw_ms = 0.0;
        double near_ms = 0.0;
        int near_cells = 0;
        std::int64_t triangles = 0; // 0 en la carretera: ZoneDraw no los cuenta
    };
    struct StageBenchData {
        PerfTotals totals;
        ProcessMemory memory; // máximos
        std::vector<StageBenchFrame> frames;
    };
    void collect(StageBenchData& data, const DemoFrame& frame, double draw_ms, double near_ms,
                 int near_cells, std::int64_t triangles);
    const char* stage_renderer_ = "";
    int stage_captures_ = 0;
    StageBenchPhase stage_bench_ = StageBenchPhase::Off;
    std::unique_ptr<sim::StageTrack> stage_track_;
    std::unique_ptr<StageDraw> stage_draw_;
    std::uint64_t stage_ground_hash_ = 0;
    double stage_build_ms_ = 0.0;
    std::int64_t stage_ns_ = 0; // tiempo de la fase del tramo
    double stage_full_fill_ms_ = 0.0;
    double stage_length_ = 0.0;
    StageBenchData stage_data_;
    StageBenchData road_data_;
};

} // namespace rally::platform
