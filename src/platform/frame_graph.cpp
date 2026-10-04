#include "platform/frame_graph.h"

#include "platform/fixed_step_clock.h"
#include "platform/frame_history.h"
#include "platform/overlay.h"

#include <raylib.h>

#include <algorithm>

namespace rally::platform {

namespace {

constexpr double kMaxMs = 50.0;
constexpr int kMaxSteps = 12; // 3 periodos a 60 Hz

constexpr Color kBackground{20, 22, 26, 200};
constexpr Color kExpected{120, 170, 120, 255};
constexpr Color kNoise{230, 200, 70, 255};
constexpr Color kDropped{230, 80, 70, 255};
constexpr Color kUnknown{150, 156, 168, 255};
constexpr Color kReference{90, 96, 108, 255};
constexpr Color kUsed{240, 240, 240, 255};
constexpr Color kText{220, 223, 228, 255};

double units_to_ms(std::int64_t units) {
    return static_cast<double>(units) / static_cast<double>(kStepsPerSecond) / 1e6;
}

int ms_to_pixels(double ms, int time_height) {
    return static_cast<int>(std::clamp(ms, 0.0, kMaxMs) / kMaxMs * time_height + 0.5);
}

Color frame_color(const FrameRecord& record, std::int64_t frame_period, int expected_steps) {
    if (expected_steps == 0) {
        return kUnknown;
    }
    if (record.steps == expected_steps) {
        return kExpected;
    }
    return FrameHistory::is_dropped(record, frame_period) ? kDropped : kNoise;
}

} // namespace

void draw_frame_graph(const FrameHistory& history, std::int64_t frame_period, std::int64_t debt,
                      bool snap_enabled, int x, int y, Overlay& overlay) {
    const PerfGeometry& g = overlay.layout().perf;
    const int line = overlay.metrics().line;
    const int width = g.graph_width;
    const int steps_height = kMaxSteps * g.step_pixels;
    const FrameCounts counts = history.count(frame_period);
    const int time_bottom = y + g.time_height;
    const int steps_top = time_bottom + g.graph_gap;
    const int steps_bottom = steps_top + steps_height;
    const int text_top = steps_bottom + g.graph_gap;
    const auto to_pixels = [&g](double ms) { return ms_to_pixels(ms, g.time_height); };

    DrawRectangle(x - g.graph_border, y - g.graph_border, width + 2 * g.graph_border,
                  steps_bottom - y + 2 * g.graph_border, kBackground);
    if (frame_period > 0) {
        for (int periods = 1; periods <= 2; ++periods) {
            const int line_y = time_bottom - to_pixels(periods * units_to_ms(frame_period));
            DrawLine(x, line_y, x + width, line_y, kReference);
        }
    }
    if (counts.expected_steps > 0) {
        const int line_y = steps_bottom - counts.expected_steps * g.step_pixels;
        DrawLine(x, line_y, x + width, line_y, kReference);
    }

    for (int i = 0; i < history.size(); ++i) {
        const FrameRecord& record = history.at(i);
        const Color color = frame_color(record, frame_period, counts.expected_steps);
        const int bar_x = x + i * g.bar_width;
        const int height = to_pixels(static_cast<double>(record.measured_ns) / 1e6);
        DrawRectangle(bar_x, time_bottom - height, g.bar_width, height, color);
        const int used_y = time_bottom - to_pixels(units_to_ms(record.used));
        DrawRectangle(bar_x, used_y, g.bar_width, 1, kUsed);
        const int steps = std::min(record.steps, kMaxSteps);
        DrawRectangle(bar_x, steps_bottom - steps * g.step_pixels, g.bar_width,
                      steps * g.step_pixels, color);
    }

    const char* snap = snap_enabled ? "sí" : "no";
    if (counts.expected_steps > 0) {
        overlay.text(Layer::Perf, x, text_top,
                     TextFormat("últimos %d: %d fuera de %d pasos (perdidos %d, ruido %d)",
                                counts.frames, counts.off_steps, counts.expected_steps,
                                counts.dropped, counts.noise),
                     kText);
    } else {
        overlay.text(Layer::Perf, x, text_top,
                     TextFormat("últimos %d: sin pasos previstos por fotograma", counts.frames),
                     kText);
    }
    overlay.text(Layer::Perf, x, text_top + line,
                 TextFormat("ajuste: %s   sin ajustar: %s   deuda: %+.3f ms", snap,
                            snap_enabled ? TextFormat("%d", counts.unsnapped) : "-",
                            units_to_ms(debt)),
                 kText);
}

} // namespace rally::platform
