#include "platform/frame_graph.h"

#include "platform/fixed_step_clock.h"
#include "platform/frame_history.h"

#include <raylib.h>

#include <algorithm>

namespace rally::platform {

namespace {

constexpr int kBarWidth = 2;
constexpr int kWidth = FrameHistory::kCapacity * kBarWidth;
constexpr double kMaxMs = 50.0;
constexpr int kTimeHeight = 125; // 2,5 px por ms
constexpr int kStepPixels = 4;
constexpr int kMaxSteps = 12; // 3 periodos a 60 Hz
constexpr int kStepsHeight = kMaxSteps * kStepPixels;
constexpr int kGap = 6;
constexpr int kFontSize = 20;

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

int ms_to_pixels(double ms) {
    return static_cast<int>(std::clamp(ms, 0.0, kMaxMs) / kMaxMs * kTimeHeight + 0.5);
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

int draw_frame_graph(const FrameHistory& history, std::int64_t frame_period, std::int64_t debt,
                     bool snap_enabled, int x, int y) {
    const FrameCounts counts = history.count(frame_period);
    const int time_bottom = y + kTimeHeight;
    const int steps_top = time_bottom + kGap;
    const int steps_bottom = steps_top + kStepsHeight;
    const int text_top = steps_bottom + kGap;

    DrawRectangle(x - 4, y - 4, kWidth + 8, steps_bottom - y + 8, kBackground);
    if (frame_period > 0) {
        for (int periods = 1; periods <= 2; ++periods) {
            const int line_y = time_bottom - ms_to_pixels(periods * units_to_ms(frame_period));
            DrawLine(x, line_y, x + kWidth, line_y, kReference);
        }
    }
    if (counts.expected_steps > 0) {
        const int line_y = steps_bottom - counts.expected_steps * kStepPixels;
        DrawLine(x, line_y, x + kWidth, line_y, kReference);
    }

    for (int i = 0; i < history.size(); ++i) {
        const FrameRecord& record = history.at(i);
        const Color color = frame_color(record, frame_period, counts.expected_steps);
        const int bar_x = x + i * kBarWidth;
        const int height = ms_to_pixels(static_cast<double>(record.measured_ns) / 1e6);
        DrawRectangle(bar_x, time_bottom - height, kBarWidth, height, color);
        const int used_y = time_bottom - ms_to_pixels(units_to_ms(record.used));
        DrawRectangle(bar_x, used_y, kBarWidth, 1, kUsed);
        const int steps = std::min(record.steps, kMaxSteps);
        DrawRectangle(bar_x, steps_bottom - steps * kStepPixels, kBarWidth, steps * kStepPixels,
                      color);
    }

    const char* snap = snap_enabled ? "sí" : "no";
    if (counts.expected_steps > 0) {
        DrawText(TextFormat("últimos %d: %d con pasos distintos de %d (perdidos %d, ruido %d)",
                            counts.frames, counts.off_steps, counts.expected_steps, counts.dropped,
                            counts.noise),
                 x, text_top, kFontSize, kText);
    } else {
        DrawText(TextFormat("últimos %d: sin pasos previstos por fotograma", counts.frames), x,
                 text_top, kFontSize, kText);
    }
    DrawText(TextFormat("ajuste al refresco: %s   deuda: %+.3f ms", snap, units_to_ms(debt)), x,
             text_top + kFontSize + 6, kFontSize, kText);
    return text_top + 2 * kFontSize + 6 - y;
}

} // namespace rally::platform
