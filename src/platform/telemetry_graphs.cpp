#include "platform/telemetry_graphs.h"

#include "platform/telemetry_history.h"

#include <raylib.h>
#include <rlgl.h>

#include <algorithm>
#include <array>
#include <span>

namespace rally::platform {

namespace {

constexpr int kWidth = kGraphColumns; // una columna de píxeles por columna de la historia
constexpr int kPlotHeight = 90;
constexpr int kTitleHeight = 22;
constexpr int kGap = 10;
constexpr int kPanelStride = kTitleHeight + kPlotHeight + kGap;
constexpr int kMargin = 16;
constexpr int kPageTitleHeight = 26;
constexpr int kFontSize = 18;

constexpr Color kBackground{20, 22, 26, 235}; // casi opaco: va encima del texto del overlay
constexpr Color kZero{90, 96, 108, 255};
constexpr Color kGrid{50, 54, 62, 255};
constexpr Color kText{220, 223, 228, 255};
constexpr Color kPageText{250, 200, 90, 255};

// Columnas visibles del canal que se dibuja (sin memoria dinámica). Con la página 1 (7 canales),
// medido a 1280×720 en el PC de desarrollo: ~0,12 ms el dibujo y ~0,15 ms el volcado del lote.
std::array<TelemetryHistory::Range, kWidth> g_columns;

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
// abre draw_telemetry_graphs. `first_channel`: índice en la historia de su primer canal.
void draw_lines(const TelemetryHistory& history, const GraphPanel& panel, int first_channel, int x,
                int y) {
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
        history.visible_columns(k++, g_columns);
        // Las columnas seguidas que ocupan un solo píxel a la misma altura van en una línea
        // horizontal: las señales suaves o planas dan pocos vértices.
        int run_start = -1;
        int run_y = 0;
        const auto end_run = [&](int end) {
            if (run_start >= 0) {
                const float py = static_cast<float>(run_y) + 0.5f;
                line(static_cast<float>(x + run_start), py, static_cast<float>(x + end), py);
                run_start = -1;
            }
        };
        for (int c = 0; c < kWidth; ++c) {
            const TelemetryHistory::Range& r = g_columns[static_cast<std::size_t>(c)];
            if (!r.valid) {
                end_run(c);
                continue;
            }
            const int top_y = value_to_y(r.max, panel, bottom);
            const int low_y = value_to_y(r.min, panel, bottom);
            if (top_y == low_y) {
                if (run_start < 0 || run_y != top_y) {
                    end_run(c);
                    run_start = c;
                    run_y = top_y;
                }
                continue;
            }
            end_run(c);
            const float px = static_cast<float>(x + c) + 0.5f;
            line(px, static_cast<float>(top_y), px, static_cast<float>(low_y + 1));
        }
        end_run(kWidth);
    }
}

// Título y valor actual de cada canal.
void draw_texts(const TelemetryHistory& history, const GraphPanel& panel, int first_channel, int x,
                int y) {
    const char* title =
        panel.unit[0] != '\0' ? TextFormat("%s (%s)", panel.title, panel.unit) : panel.title;
    DrawText(title, x, y, kFontSize, kText);
    int text_x = x + MeasureText(title, kFontSize) + 14;
    int k = first_channel;
    for (const GraphChannel& channel : panel.channels) {
        const float current = history.latest(k++);
        const char* label = panel.channels.size() > 1 ? TextFormat("%s %.2f", channel.name, current)
                                                      : TextFormat("%.1f", current);
        DrawText(label, text_x, y, kFontSize, to_color(channel.rgba));
        text_x += MeasureText(label, kFontSize) + 12;
    }
}

} // namespace

void draw_telemetry_graphs(const TelemetryHistory& history, int page, int right) {
    const std::span<const GraphPage> pages = default_graph_pages();
    if (page < 0 || page >= static_cast<int>(pages.size())) {
        return;
    }
    const std::span<const GraphPanel> panels = pages[static_cast<std::size_t>(page)].panels;
    const int x = right - kWidth - kMargin;
    const int top = kMargin + kPageTitleHeight;
    const int height = static_cast<int>(panels.size()) * kPanelStride - kGap;

    // Por pasadas (fondo, líneas, textos) y no panel a panel: cada cambio entre rectángulos,
    // líneas y texto cierra un lote de rlgl (un draw call).
    DrawRectangle(x - 4, kMargin - 2, kWidth + 8, kPageTitleHeight + height + 4, kBackground);
    rlBegin(RL_LINES);
    int y = top;
    int first_channel = first_graph_channel(page);
    for (const GraphPanel& panel : panels) {
        draw_lines(history, panel, first_channel, x, y);
        first_channel += static_cast<int>(panel.channels.size());
        y += kPanelStride;
    }
    rlEnd();
    DrawText(TextFormat("gráficas %d · %s   (T: siguiente)", page + 1,
                        pages[static_cast<std::size_t>(page)].title),
             x, kMargin, kFontSize, kPageText);
    y = top;
    first_channel = first_graph_channel(page);
    for (const GraphPanel& panel : panels) {
        draw_texts(history, panel, first_channel, x, y);
        first_channel += static_cast<int>(panel.channels.size());
        y += kPanelStride;
    }
}

} // namespace rally::platform
