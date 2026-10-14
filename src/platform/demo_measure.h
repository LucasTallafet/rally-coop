#pragma once

#include "platform/demo_part.h"
#include "platform/forest_scene.h"
#include "platform/frame_log.h"
#include "platform/zone_draw.h"

#include <cstdint>
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
};

} // namespace rally::platform
