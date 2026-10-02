#include "platform/perf_counter.h"

#include "platform/fixed_step_clock.h"
#include "platform/frame_graph.h"
#include "platform/frame_pacer.h"

#include <raylib.h>
#include <rlgl.h>

#include <algorithm>
#include <chrono>

namespace rally::platform {

namespace {

constexpr int kFontSize = 20;
constexpr int kLine = 22;
constexpr int kPanelWidth = 640;
constexpr int kPadding = 10;
// Columnas: etiqueta, media (alineada a la derecha) y máximo de 2 s (a la derecha).
constexpr int kAverageRight = 290;
constexpr int kMaxRight = 430;

constexpr Color kBackground{20, 22, 26, 225};
constexpr Color kTitle{240, 200, 120, 255};
constexpr Color kText{220, 223, 228, 255};
constexpr Color kDim{150, 156, 168, 255};

// El mismo reloj que FramePacer.
std::int64_t now_ns() {
    return std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

double to_ms(double ns) { return ns / 1e6; }
double to_mb(std::uint64_t bytes) { return static_cast<double>(bytes) / (1024.0 * 1024.0); }

void draw_right(const char* text, int right, int y, Color color) {
    DrawText(text, right - MeasureText(text, kFontSize), y, kFontSize, color);
}

// Fila con media y máximo en ms.
void draw_ms_row(const char* label, const PerfChannel& channel, int x, int y) {
    DrawText(label, x, y, kFontSize, kText);
    if (!channel.has_data()) {
        draw_right("n/d", x + kAverageRight, y, kDim);
        return;
    }
    draw_right(TextFormat("%.2f ms", to_ms(channel.average())), x + kAverageRight, y, kText);
    draw_right(TextFormat("%.2f ms", to_ms(static_cast<double>(channel.max()))), x + kMaxRight, y,
               kDim);
}

} // namespace

PerfCounter::PerfCounter() : renderer_(gl_renderer()) { refresh_memory(); }

void PerfCounter::handle_keys() {
    if (IsKeyPressed(KEY_F1)) {
        visible_ = !visible_;
    }
}

void PerfCounter::refresh_memory() { memory_ = query_process_memory(); }

void PerfCounter::begin_frame(std::int64_t frame_ns, std::int64_t pacer_wait_ns) {
    const DrawCallCounts calls = draw_calls_.take();
    const std::int64_t gpu = gpu_.take_result();
    if (first_frame_) {
        // Sin fotograma anterior completo: solo se descartan las cuentas de antes.
        first_frame_ = false;
        current_ = FrameSample{};
        return;
    }
    current_.frame_ns = frame_ns;
    current_.present_ns += pacer_wait_ns;
    current_.gpu_ns = gpu;
    current_.draw_calls = calls.calls;
    current_.instanced_calls = calls.instanced_calls;
    current_.instances = calls.instances;
    if (stats_.push(now_ns(), current_)) {
        refresh_memory();
    }
    last_ = current_;
    current_ = FrameSample{};
}

void PerfCounter::record_steps(std::int64_t frame_ns, const FrameSteps& steps) {
    history_.push(FrameRecord{frame_ns, steps.used, steps.count, steps.snapped});
}

void PerfCounter::begin_step() { step_start_ns_ = now_ns(); }

void PerfCounter::end_step() {
    const std::int64_t elapsed = now_ns() - step_start_ns_;
    current_.sim_ns += elapsed;
    current_.step_max_ns = std::max(current_.step_max_ns, elapsed);
    ++current_.steps;
}

void PerfCounter::begin_render() {
    render_start_ns_ = now_ns();
    gpu_.begin();
}

void PerfCounter::end_render() {
    rlDrawRenderBatchActive();
    gpu_.end();
    present_start_ns_ = now_ns();
    current_.render_ns = present_start_ns_ - render_start_ns_;
}

void PerfCounter::end_present() { current_.present_ns = now_ns() - present_start_ns_; }

void PerfCounter::draw(const FramePacer& pacer, const FixedStepClock& clock, bool snap_enabled,
                       int render_width, int render_height) const {
    if (!visible_) {
        return;
    }
    const int panel_x = GetScreenWidth() - kPanelWidth - 16;
    const int x = panel_x + kPadding;
    int y = 16 + kPadding;
    constexpr int kRows = 15;
    constexpr int kGraphHeight = 240;
    DrawRectangle(panel_x, 16, kPanelWidth, 2 * kPadding + kRows * kLine + kGraphHeight,
                  kBackground);

    DrawText(TextFormat("RENDIMIENTO (F1)   %.1f fps", stats_.fps()), x, y, kFontSize, kTitle);
    y += kLine;
    draw_right("media", x + kAverageRight, y, kDim);
    draw_right("máx. 2 s", x + kMaxRight, y, kDim);
    y += kLine;
    draw_ms_row("fotograma", stats_.frame(), x, y);
    y += kLine;
    draw_ms_row("CPU simulación", stats_.sim(), x, y);
    y += kLine;
    draw_ms_row("  por paso", stats_.step(), x, y);
    DrawText(TextFormat("%.2f pasos", stats_.steps().average()), x + kMaxRight + 12, y, kFontSize,
             kDim);
    y += kLine;
    draw_ms_row("CPU render", stats_.render(), x, y);
    y += kLine;
    draw_ms_row("presentación", stats_.present(), x, y);
    y += kLine;
    draw_ms_row("resto", stats_.other(), x, y);
    y += kLine;
    if (gpu_.available()) {
        draw_ms_row("GPU", stats_.gpu(), x, y);
    } else {
        DrawText("GPU: no disponible (sin consultas de tiempo)", x, y, kFontSize, kDim);
    }
    y += kLine;
    DrawText("draw calls", x, y, kFontSize, kText);
    if (draw_calls_.available()) {
        draw_right(TextFormat("%.1f", stats_.draw_calls().average()), x + kAverageRight, y, kText);
        draw_right(TextFormat("%lld", static_cast<long long>(stats_.draw_calls().max())),
                   x + kMaxRight, y, kDim);
        y += kLine;
        DrawText(TextFormat("  instanciadas %.1f con %.0f instancias",
                            stats_.instanced_calls().average(), stats_.instances().average()),
                 x, y, kFontSize, kDim);
    } else {
        draw_right("no disponible", x + kMaxRight, y, kDim);
        y += kLine;
    }
    y += kLine;
    DrawText(TextFormat("memoria: working set %.1f MB   privados %.1f MB",
                        to_mb(memory_.working_set), to_mb(memory_.private_bytes)),
             x, y, kFontSize, kText);
    y += kLine;
    DrawText(TextFormat("render: %d×%d   pantalla: %d×%d", render_width, render_height,
                        GetScreenWidth(), GetScreenHeight()),
             x, y, kFontSize, kText);
    y += kLine;
    DrawText(TextFormat("gráfica: %s", renderer_), x, y, kFontSize, kText);
    y += kLine;
    DrawText(TextFormat("GPU: muestras perdidas %d", gpu_.lost()), x, y, kFontSize, kDim);
    y += kLine + 8;
    draw_frame_graph(history_, pacer.frame_period(), clock.debt(), snap_enabled, x + 4, y);
}

} // namespace rally::platform
