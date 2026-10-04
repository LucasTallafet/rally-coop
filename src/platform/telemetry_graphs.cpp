#include "platform/telemetry_graphs.h"

#include "platform/telemetry_history.h"

#include <raylib.h>
#include <rlgl.h>

#include <algorithm>
#include <array>
#include <span>

namespace rally::platform {

namespace {

constexpr int kWidth = 540; // 4,4 pasos por columna con 10 s; cabe a la izquierda de F1 a 1280
constexpr int kPlotHeight = 90;
constexpr int kTitleHeight = 22;
constexpr int kGap = 10;
constexpr int kPanelStride = kTitleHeight + kPlotHeight + kGap;
constexpr int kMargin = 16;
constexpr int kFontSize = 18;

constexpr Color kBackground{20, 22, 26, 235}; // casi opaco: va encima del texto del overlay
constexpr Color kZero{90, 96, 108, 255};
constexpr Color kGrid{50, 54, 62, 255};
constexpr Color kText{220, 223, 228, 255};

using Range = TelemetryHistory::Range;

// Mínimo y máximo de cada canal por columna, del fotograma en curso (sin memoria dinámica).
std::array<Range, static_cast<std::size_t>(kWidth) * kMaxGraphChannels> g_ranges;

Color to_color(std::uint32_t rgba) {
    return Color{static_cast<unsigned char>(rgba >> 24), static_cast<unsigned char>(rgba >> 16),
                 static_cast<unsigned char>(rgba >> 8), static_cast<unsigned char>(rgba)};
}

int value_to_y(float value, const GraphPanel& panel, int bottom) {
    const float t = std::clamp((value - panel.min) / (panel.max - panel.min), 0.0f, 1.0f);
    return bottom - static_cast<int>(t * (kPlotHeight - 1) + 0.5f);
}

void line(float x0, float y0, float x1, float y1) {
    rlVertex2f(x0, y0);
    rlVertex2f(x1, y1);
}

// Rejilla (una línea por segundo), cero y curvas de un panel, dentro del lote de líneas que
// abre draw_telemetry_graphs. `first_channel`: índice de su primer canal en g_ranges, de
// `channel_count` canales por columna.
void draw_lines(const GraphPanel& panel, int first_channel, int channel_count, int x, int y) {
    const int plot_top = y + kTitleHeight;
    const int bottom = plot_top + kPlotHeight - 1;
    rlColor4ub(kGrid.r, kGrid.g, kGrid.b, kGrid.a);
    for (int s = 1; s < kTelemetryHistorySeconds; ++s) {
        const float line_x = static_cast<float>(x + s * kWidth / kTelemetryHistorySeconds) + 0.5f;
        line(line_x, static_cast<float>(plot_top), line_x, static_cast<float>(bottom));
    }
    if (panel.min < 0.0f && panel.max > 0.0f) {
        const float zero_y = static_cast<float>(value_to_y(0.0f, panel, bottom)) + 0.5f;
        rlColor4ub(kZero.r, kZero.g, kZero.b, kZero.a);
        line(static_cast<float>(x), zero_y, static_cast<float>(x + kWidth), zero_y);
    }
    int k = first_channel;
    for (const GraphChannel& channel : panel.channels) {
        const Color color = to_color(channel.rgba);
        rlColor4ub(color.r, color.g, color.b, color.a);
        for (int c = 0; c < kWidth; ++c) {
            const Range& r = g_ranges[static_cast<std::size_t>(c * channel_count + k)];
            if (!r.valid) {
                continue;
            }
            const float px = static_cast<float>(x + c) + 0.5f;
            line(px, static_cast<float>(value_to_y(r.max, panel, bottom)), px,
                 static_cast<float>(value_to_y(r.min, panel, bottom) + 1));
        }
        ++k;
    }
}

// Título y valor actual de cada canal.
void draw_texts(const TelemetryHistory& history, const GraphPanel& panel, int x, int y) {
    const char* title =
        panel.unit[0] != '\0' ? TextFormat("%s (%s)", panel.title, panel.unit) : panel.title;
    DrawText(title, x, y, kFontSize, kText);
    int text_x = x + MeasureText(title, kFontSize) + 14;
    for (const GraphChannel& channel : panel.channels) {
        const float current =
            history.size() > 0 ? channel.value(history.at(history.size() - 1)) : 0.0f;
        const char* label = panel.channels.size() > 1 ? TextFormat("%s %.2f", channel.name, current)
                                                      : TextFormat("%.1f", current);
        DrawText(label, text_x, y, kFontSize, to_color(channel.rgba));
        text_x += MeasureText(label, kFontSize) + 12;
    }
}

} // namespace

void draw_telemetry_graphs(const TelemetryHistory& history, int right) {
    const std::span<const GraphPanel> panels = default_graph_panels();
    // Una sola pasada por la historia para todos los canales (canal a canal leía cada paso una
    // vez por canal). Con los 7 canales por defecto, medido en el PC de desarrollo: ~0,3 ms los
    // rangos, ~0,3 ms los vértices y ~0,1 ms el volcado del lote.
    std::array<TelemetryHistory::Channel, kMaxGraphChannels> channels{};
    int channel_count = 0;
    for (const GraphPanel& panel : panels) {
        for (const GraphChannel& channel : panel.channels) {
            if (channel_count < kMaxGraphChannels) {
                channels[static_cast<std::size_t>(channel_count++)] = channel.value;
            }
        }
    }
    history.column_ranges(std::span(channels.data(), static_cast<std::size_t>(channel_count)),
                          kWidth, g_ranges);

    // Por pasadas (fondos, líneas, textos) y no panel a panel: cada cambio entre rectángulos,
    // líneas y texto cierra un lote de rlgl (un draw call).
    const int x = right - kWidth - kMargin;
    int y = kMargin;
    for (std::size_t i = 0; i < panels.size(); ++i) {
        DrawRectangle(x - 4, y - 2, kWidth + 8, kTitleHeight + kPlotHeight + 4, kBackground);
        y += kPanelStride;
    }
    rlBegin(RL_LINES);
    y = kMargin;
    int first_channel = 0;
    for (const GraphPanel& panel : panels) {
        draw_lines(panel, first_channel, channel_count, x, y);
        first_channel += static_cast<int>(panel.channels.size());
        y += kPanelStride;
    }
    rlEnd();
    y = kMargin;
    for (const GraphPanel& panel : panels) {
        draw_texts(history, panel, x, y);
        y += kPanelStride;
    }
}

} // namespace rally::platform
