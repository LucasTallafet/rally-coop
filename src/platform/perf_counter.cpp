#include "platform/perf_counter.h"

#include "platform/fixed_step_clock.h"
#include "platform/frame_graph.h"
#include "platform/frame_pacer.h"
#include "platform/keys_raylib.h"
#include "platform/overlay.h"

#include <raylib.h>
#include <rlgl.h>

#include <algorithm>
#include <chrono>

namespace rally::platform {

namespace {

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

// Fila con media y máximo en ms, con las columnas del diseño (PerfGeometry).
void draw_ms_row(Overlay& overlay, const char* label, const PerfChannel& channel, int x, int y) {
    const PerfGeometry& g = overlay.layout().perf;
    overlay.text(Layer::Perf, x, y, label, kText);
    if (!channel.has_data()) {
        overlay.text_right(Layer::Perf, x + g.average_right, y, "n/d", kDim);
        return;
    }
    overlay.text_right(Layer::Perf, x + g.average_right, y,
                       TextFormat("%.2f ms", to_ms(channel.average())), kText);
    overlay.text_right(Layer::Perf, x + g.max_right, y,
                       TextFormat("%.2f ms", to_ms(static_cast<double>(channel.max()))), kDim);
}

} // namespace

PerfCounter::PerfCounter() : renderer_(gl_renderer()) { refresh_memory(); }

void PerfCounter::handle_keys() {
    if (action_pressed(Action::Perf)) {
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
                       int render_width, int render_height, Overlay& overlay) const {
    if (!visible_) {
        return;
    }
    const UiMetrics& m = overlay.metrics();
    const PerfGeometry& g = overlay.layout().perf;
    overlay.panel(Layer::Perf, kBackground);
    const int x = overlay.content_x(Layer::Perf);
    const int top = overlay.content_y(Layer::Perf);
    int y = top;
    const auto next = [&y, &m] { y += m.line; };

    overlay.text(Layer::Perf, x, y, TextFormat("RENDIMIENTO (F1)   %.1f fps", stats_.fps()),
                 kTitle);
    next();
    overlay.text_right(Layer::Perf, x + g.average_right, y, "media", kDim);
    overlay.text_right(Layer::Perf, x + g.max_right, y, "máx. 2 s", kDim);
    next();
    draw_ms_row(overlay, "fotograma", stats_.frame(), x, y);
    next();
    draw_ms_row(overlay, "CPU simulación", stats_.sim(), x, y);
    next();
    draw_ms_row(overlay, "  por paso", stats_.step(), x, y);
    overlay.text(Layer::Perf, x + g.max_right + g.steps_gap, y,
                 TextFormat("%.2f pasos", stats_.steps().average()), kDim);
    next();
    draw_ms_row(overlay, "CPU render", stats_.render(), x, y);
    next();
    draw_ms_row(overlay, "presentación", stats_.present(), x, y);
    next();
    draw_ms_row(overlay, "resto", stats_.other(), x, y);
    next();
    if (gpu_.available()) {
        draw_ms_row(overlay, "GPU", stats_.gpu(), x, y);
    } else {
        overlay.text(Layer::Perf, x, y, "GPU: no disponible (sin consultas de tiempo)", kDim);
    }
    next();
    overlay.text(Layer::Perf, x, y, "draw calls", kText);
    if (draw_calls_.available()) {
        overlay.text_right(Layer::Perf, x + g.average_right, y,
                           TextFormat("%.1f", stats_.draw_calls().average()), kText);
        overlay.text_right(Layer::Perf, x + g.max_right, y,
                           TextFormat("%lld", static_cast<long long>(stats_.draw_calls().max())),
                           kDim);
        next();
        overlay.text(Layer::Perf, x, y,
                     TextFormat("  instanciadas %.1f con %.0f instancias",
                                stats_.instanced_calls().average(), stats_.instances().average()),
                     kDim);
    } else {
        overlay.text_right(Layer::Perf, x + g.max_right, y, "no disponible", kDim);
        next();
    }
    next();
    overlay.text(Layer::Perf, x, y,
                 TextFormat("memoria: working set %.1f MB   privados %.1f MB",
                            to_mb(memory_.working_set), to_mb(memory_.private_bytes)),
                 kText);
    next();
    overlay.text(Layer::Perf, x, y,
                 TextFormat("render: %d×%d   pantalla: %d×%d", render_width, render_height,
                            GetScreenWidth(), GetScreenHeight()),
                 kText);
    next();
    overlay.text_fit(Layer::Perf, x, y, TextFormat("gráfica: %s", renderer_), kText);
    next();
    overlay.text(Layer::Perf, x, y, TextFormat("GPU: muestras perdidas %d", gpu_.lost()), kDim);
    next();
    if (audio_.ready) {
        overlay.text(Layer::Perf, x, y,
                     TextFormat("audio: CPU %.2f %%   llamada máx. %.2f ms   cortes %lld",
                                audio_.cpu_fraction * 100.0, audio_.max_call_ms,
                                static_cast<long long>(audio_.gaps)),
                     kText);
    } else {
        overlay.text(Layer::Perf, x, y, "audio: sin dispositivo", kDim);
    }
    draw_frame_graph(history_, pacer.frame_period(), clock.debt(), snap_enabled, x + g.graph_border,
                     top + g.graph_top + g.graph_border, overlay);
}

} // namespace rally::platform
