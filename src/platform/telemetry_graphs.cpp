#include "platform/telemetry_graphs.h"

#include "platform/overlay.h"
#include "platform/screen_layout.h"
#include "platform/telemetry_history.h"

#include <raylib.h>
#include <rlgl.h>

#include <algorithm>
#include <array>
#include <span>

namespace rally::platform {

namespace {

constexpr Color kBackground{20, 22, 26, 235}; // casi opaco: va encima del texto del overlay
constexpr Color kZero{90, 96, 108, 255};
constexpr Color kGrid{50, 54, 62, 255};
constexpr Color kText{220, 223, 228, 255};
constexpr Color kPageText{250, 200, 90, 255};

// Columnas visibles del canal que se dibuja y las mismas juntadas al ancho del panel (sin memoria
// dinámica). Con la página 1 (7 canales), medido a 1280×720 en el PC de desarrollo antes de la
// 2.4b: ~0,12 ms el dibujo y ~0,15 ms el volcado del lote.
std::array<TelemetryHistory::Range, kGraphColumns> g_columns;
std::array<TelemetryHistory::Range, kGraphColumns> g_merged;

Color to_color(std::uint32_t rgba) {
    return Color{static_cast<unsigned char>(rgba >> 24), static_cast<unsigned char>(rgba >> 16),
                 static_cast<unsigned char>(rgba >> 8), static_cast<unsigned char>(rgba)};
}

int value_to_y(float value, const GraphPanel& panel, int bottom, int plot_height) {
    const float t = std::clamp((value - panel.min) / (panel.max - panel.min), 0.0f, 1.0f);
    return bottom - static_cast<int>(t * static_cast<float>(plot_height - 1) + 0.5f);
}

void line(float x0, float y0, float x1, float y1) {
    rlVertex2f(x0, y0);
    rlVertex2f(x1, y1);
}

// Rejilla (una línea por segundo), cero y curvas de un panel, dentro del lote de líneas que
// abre draw_telemetry_graphs. `first_channel`: índice en la historia de su primer canal; `top`:
// borde de arriba de la curva.
void draw_lines(const TelemetryHistory& history, const GraphPanel& panel, int first_channel, int x,
                int top, const GraphGeometry& g) {
    const int width = g.plot_width;
    const int bottom = top + g.plot_height - 1;
    const std::span<TelemetryHistory::Range> columns(g_merged.data(),
                                                     static_cast<std::size_t>(width));
    rlColor4ub(kGrid.r, kGrid.g, kGrid.b, kGrid.a);
    for (int s = 1; s < kTelemetryHistorySeconds; ++s) {
        const float line_x = static_cast<float>(x + s * width / kTelemetryHistorySeconds) + 0.5f;
        line(line_x, static_cast<float>(top), line_x, static_cast<float>(bottom));
    }
    if (panel.min < 0.0f && panel.max > 0.0f) {
        const float zero_y =
            static_cast<float>(value_to_y(0.0f, panel, bottom, g.plot_height)) + 0.5f;
        rlColor4ub(kZero.r, kZero.g, kZero.b, kZero.a);
        line(static_cast<float>(x), zero_y, static_cast<float>(x + width), zero_y);
    }
    int k = first_channel;
    for (const GraphChannel& channel : panel.channels) {
        const Color color = to_color(channel.rgba);
        rlColor4ub(color.r, color.g, color.b, color.a);
        history.visible_columns(k++, g_columns);
        // Más estrecho que la historia: cada píxel, la envolvente de sus columnas.
        merge_columns(g_columns, columns);
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
        for (int c = 0; c < width; ++c) {
            const TelemetryHistory::Range& r = columns[static_cast<std::size_t>(c)];
            if (!r.valid) {
                end_run(c);
                continue;
            }
            const int top_y = value_to_y(r.max, panel, bottom, g.plot_height);
            const int low_y = value_to_y(r.min, panel, bottom, g.plot_height);
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
        end_run(width);
    }
}

// Título y valor actual de cada canal, en los huecos fijos de graph_header.
void draw_texts(Overlay& overlay, const TelemetryHistory& history, const GraphPanel& panel,
                int first_channel, int x, int y) {
    const UiMetrics& m = overlay.metrics();
    const GraphHeader header = graph_header(panel, m, overlay.layout().graphs.plot_width);
    overlay.text(Layer::Graphs, x, y, graph_panel_title(panel), kText);
    int k = first_channel;
    for (int i = 1; i < header.count; ++i) {
        const GraphChannel& channel = panel.channels[static_cast<std::size_t>(i - 1)];
        const float current = history.latest(k++);
        const char* label = panel.channels.size() > 1 ? TextFormat("%s %.2f", channel.name, current)
                                                      : TextFormat("%.1f", current);
        const GraphHeader::Item& item = header.items[static_cast<std::size_t>(i)];
        overlay.text(Layer::Graphs, x + item.x, y + item.line * m.line, label,
                     to_color(channel.rgba));
    }
}

} // namespace

void draw_telemetry_graphs(const TelemetryHistory& history, int page, Overlay& overlay) {
    const std::span<const GraphPage> pages = default_graph_pages();
    if (page < 0 || page >= static_cast<int>(pages.size())) {
        return;
    }
    const UiMetrics& m = overlay.metrics();
    const GraphGeometry& g = overlay.layout().graphs;
    const std::span<const GraphPanel> panels = pages[static_cast<std::size_t>(page)].panels;
    const int x = overlay.content_x(Layer::Graphs);
    const int top = overlay.content_y(Layer::Graphs);

    // Por pasadas (fondo, líneas, textos) y no panel a panel: cada cambio entre rectángulos,
    // líneas y texto cierra un lote de rlgl (un draw call).
    overlay.panel(Layer::Graphs, kBackground);
    rlBegin(RL_LINES);
    int y = top + g.page_title_height;
    int first_channel = first_graph_channel(page);
    for (const GraphPanel& panel : panels) {
        const int header_lines = graph_header(panel, m, g.plot_width).lines;
        draw_lines(history, panel, first_channel, x, y + header_lines * m.line, g);
        first_channel += static_cast<int>(panel.channels.size());
        y += graph_panel_height(panel, m, g);
    }
    rlEnd();
    overlay.text(Layer::Graphs, x, top,
                 TextFormat("gráficas %d · %s   (T: siguiente)", page + 1,
                            pages[static_cast<std::size_t>(page)].title),
                 kPageText);
    y = top + g.page_title_height;
    first_channel = first_graph_channel(page);
    for (const GraphPanel& panel : panels) {
        draw_texts(overlay, history, panel, first_channel, x, y);
        first_channel += static_cast<int>(panel.channels.size());
        y += graph_panel_height(panel, m, g);
    }
}

} // namespace rally::platform
