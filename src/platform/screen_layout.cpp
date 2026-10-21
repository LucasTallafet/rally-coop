#include "platform/screen_layout.h"

#include "platform/frame_history.h"
#include "platform/ui_text.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace rally::platform {

namespace {

// Peor caso de cada capa. Si cambia un texto en overlay.cpp, perf_counter.cpp o frame_graph.cpp,
// su muestra cambia aquí.
constexpr std::string_view kIndicatorSamples[] = {
    "888 km/h   adelante   vuelta 888.88 s   mejor 888.88 s   A CIEGAS · Prueba 15/15   "
    "volante [---------O---------]",
};
constexpr std::string_view kHelpHintSamples[] = {"H: ayuda"};
// Anillo de las acciones: la línea de debajo («acción · fase»); un nombre más largo se recorta.
constexpr std::string_view kRingSamples[] = {"guardar_tablet · preparar"};
// Panel de depuración: estas líneas y, entre la tercera y la cuarta, la fila de las barras de los
// ejes (kDebugBarsRow).
constexpr std::string_view kDebugSamples[] = {
    "DEPURACIÓN (F3)",
    "mando: Nintendo Switch Pro Controller (Switch Pro)",
    "gatillos: RT analógico, LT analógico   conectados: 8",
    "barras: dirección analógico, acelerador analógico, freno analógico",
    "freno de mano: 100 %   subir 888, bajar 888, recolocar 888",
    "impacto: 88.88 m/s   último que vibró: 88.88 m/s (umbral 8.8)",
    "grabación: 20261004-235959.rrec   pasos: 88888888",
    "coche: mid_awd_turbo.car (8888 kg)",
    "zona: baches de asfalto (asfalto)   pista ciega: asfalto",
    "vueltas: 888   última: 888.88 s",
    "suelo (CPU): 88.88 ms   media 88.88 ms   máx. 88.88 ms",
    "cámara: ojo del conductor   mundo: recinto a 20 km del origen",
    "render: FLOAT DEL MUNDO (depuración)",
    "bosque: DrawMeshInstanced de raylib, 8888 árboles de 888 triáng.",
    "ojo: -88888.888, -88888.888, -88888.888 m",
    "asiento: alto -15 cm, adelante -15 cm, derecha -15 cm",
    "ayudas: dirección sí, ABS sí, control alto, suspensión blanda",
    "ritmo: frecuencia del monitor   sincronización vertical: sí",
    "monitor: 888.88 Hz   intervalo pedido 8, real (SDL) desconocido",
    "temporizador: de alta resolución",
    "interpolación: sí   pantalla: completa sin bordes",
    "registro: fotogramas_888.csv",
    "pasos en el fotograma: 88   alpha: 8.88",
};
constexpr std::string_view kPerfSamples[] = {
    "RENDIMIENTO (F1)   8888.8 fps",
    "GPU: no disponible (sin consultas de tiempo)",
    "  instanciadas 8888.8 con 88888 instancias",
    "memoria: working set 8888.8 MB   privados 8888.8 MB",
    "render: 8888×8888   pantalla: 8888×8888",
    "GPU: muestras perdidas 8888",
    "audio: CPU 88.88 %   llamada máx. 88.88 ms   cortes 88888",
    // Las dos líneas de la gráfica de fotogramas.
    "últimos 240: 240 fuera de 88 pasos (perdidos 240, ruido 240)",
    "ajuste: sí   sin ajustar: 240   deuda: +888.888 ms",
};
// Columnas de las filas en ms: etiqueta, media, máximo y pasos por fotograma.
constexpr std::string_view kPerfRowValue = "88.88 ms";
constexpr std::string_view kPerfRowLabel = "CPU simulación";
constexpr std::string_view kPerfSteps = "88.88 pasos";

constexpr int kPerfGraphFrames = FrameHistory::kCapacity;
constexpr int kPerfMaxSteps = 12; // como frame_graph.cpp: 3 periodos a 60 Hz

// La ayuda deja entre columnas y entre la etiqueta de las teclas y su texto.
int help_text_gap(const UiMetrics& m) { return m.font; }

PerfGeometry perf_geometry(const UiMetrics& m) {
    PerfGeometry p;
    // A 20 px, las columnas en 290 y 430 (como antes de la 2.4b); escalan con la fuente.
    p.average_right =
        std::max(text_width(kPerfRowLabel, m.font) + m.font / 2 + text_width(kPerfRowValue, m.font),
                 29 * m.font / 2);
    p.max_right =
        p.average_right + std::max(text_width(kPerfRowValue, m.font) + m.font, 7 * m.font);
    p.steps_gap = m.font * 3 / 5;
    p.graph_top = p.rows * m.line + m.px(8);
    p.bar_width = std::max(1, m.px(2));
    p.time_height = m.px(125);
    p.step_pixels = std::max(1, m.px(4));
    p.graph_gap = m.px(6);
    p.graph_border = std::max(2, m.px(4));
    p.graph_width = kPerfGraphFrames * p.bar_width;
    p.graph_height = p.time_height + p.graph_gap + kPerfMaxSteps * p.step_pixels + p.graph_gap +
                     2 * m.line + p.graph_border;
    return p;
}

GraphGeometry graph_geometry(const UiMetrics& m) {
    GraphGeometry g;
    g.plot_width = std::min(kGraphColumns, m.px(kGraphColumns));
    g.plot_height = m.px(90);
    g.panel_gap = m.px(10);
    g.page_title_height = m.line + m.px(4);
    g.label_gap = m.font * 3 / 5;
    return g;
}

int samples_width(std::span<const std::string_view> samples, int font) {
    int width = 0;
    for (const std::string_view s : samples) {
        width = std::max(width, text_width(s, font));
    }
    return width;
}

int perf_content_width(const PerfGeometry& p, const UiMetrics& m) {
    return std::max({samples_width(kPerfSamples, m.font),
                     p.max_right + p.steps_gap + text_width(kPerfSteps, m.font),
                     p.graph_width + 2 * p.graph_border});
}

int perf_content_height(const PerfGeometry& p) { return p.graph_top + p.graph_height; }

int debug_content_height(const UiMetrics& m) {
    // Las líneas de muestra y la fila de las barras.
    return (static_cast<int>(std::size(kDebugSamples)) + 1) * m.line;
}

int graphs_content_height(const UiMetrics& m, const GraphGeometry& g) {
    int height = 0;
    for (const GraphPage& page : default_graph_pages()) {
        height = std::max(height, graph_page_height(page, m, g));
    }
    return height;
}

} // namespace

bool overlaps(const UiRect& a, const UiRect& b) {
    return a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom();
}

bool contains(const UiRect& outer, const UiRect& inner) {
    return inner.x >= outer.x && inner.y >= outer.y && inner.right() <= outer.right() &&
           inner.bottom() <= outer.bottom();
}

int UiMetrics::px(double v) const {
    const int value = static_cast<int>(std::lround(v * scale));
    return v > 0.0 ? std::max(1, value) : value;
}

UiMetrics ui_metrics(int screen_height) {
    UiMetrics m;
    m.font = 10 * std::max(1, screen_height / 540);
    m.line = m.font + m.font / 10;
    m.big_font = m.font + 10;
    m.big_line = m.big_font + m.big_font / 10;
    m.scale = static_cast<double>(screen_height) / 1080.0;
    m.margin = m.px(16);
    m.gap = m.px(16);
    m.padding = m.px(10);
    return m;
}

bool layers_exclusive(Layer a, Layer b) {
    const auto top_panel = [](Layer l) {
        return l == Layer::Debug || l == Layer::Perf || l == Layer::Graphs;
    };
    return (a == Layer::Help && top_panel(b)) || (b == Layer::Help && top_panel(a));
}

bool layers_overdraw(Layer a, Layer b) {
    return (a == Layer::Help && b == Layer::ActionRing) ||
           (b == Layer::Help && a == Layer::ActionRing);
}

bool layers_may_overlap(Layer a, Layer b) {
    return layers_exclusive(a, b) || layers_overdraw(a, b);
}

std::string_view layer_name(Layer layer) {
    switch (layer) {
    case Layer::Indicator:
        return "indicador";
    case Layer::HelpHint:
        return "aviso de la ayuda";
    case Layer::Messages:
        return "mensajes";
    case Layer::Help:
        return "ayuda";
    case Layer::Debug:
        return "depuración";
    case Layer::Perf:
        return "rendimiento";
    case Layer::Graphs:
        return "gráficas";
    case Layer::ActionRing:
        return "anillo de la acción";
    case Layer::Count:
        break;
    }
    return "";
}

std::string graph_panel_title(const GraphPanel& panel) {
    std::string title = panel.title;
    if (panel.unit[0] != '\0') {
        title += " (";
        title += panel.unit;
        title += ')';
    }
    return title;
}

std::string graph_channel_sample(const GraphPanel& panel, const GraphChannel& channel) {
    // Como telemetry_graphs.cpp: "%s %.2f" con varios canales y "%.1f" con uno.
    if (panel.channels.size() > 1) {
        return std::string(channel.name) + " -888.88";
    }
    return "-888.8";
}

GraphHeader graph_header(const GraphPanel& panel, const UiMetrics& m, int width) {
    GraphHeader header;
    const int gap = m.font * 3 / 5;
    header.items[0] = {0, 0};
    header.count = 1;
    int x = text_width(graph_panel_title(panel), m.font) + gap;
    int line = 0;
    for (const GraphChannel& channel : panel.channels) {
        if (header.count == kMaxGraphHeaderItems) {
            break;
        }
        const int slot = text_width(graph_channel_sample(panel, channel), m.font);
        if (x + slot > width && x > 0) {
            ++line;
            x = 0;
        }
        header.items[static_cast<std::size_t>(header.count++)] = {x, line};
        x += slot + gap;
    }
    header.lines = line + 1;
    return header;
}

int graph_panel_height(const GraphPanel& panel, const UiMetrics& m, const GraphGeometry& g) {
    return graph_header(panel, m, g.plot_width).lines * m.line + g.plot_height + g.panel_gap;
}

int graph_page_height(const GraphPage& page, const UiMetrics& m, const GraphGeometry& g) {
    int height = g.page_title_height;
    for (const GraphPanel& panel : page.panels) {
        height += graph_panel_height(panel, m, g);
    }
    return page.panels.empty() ? 0 : height - g.panel_gap;
}

int axis_bar_width(const UiMetrics& m) { return 10 * m.font; }
int axis_bars_width(const UiMetrics& m) { return 3 * axis_bar_width(m) + 2 * m.font; }

std::string help_title(int page, int pages) {
    return "Ayuda " + std::to_string(page + 1) + "/" + std::to_string(pages) +
           (page + 1 < pages ? " · H: página siguiente" : " · H: ocultar");
}

HelpGrid help_grid(std::span<const KeyBinding> table, const UiMetrics& m, int max_rows,
                   int max_width) {
    HelpGrid grid;
    grid.column_gap = m.font * 3 / 2;
    int text_width_max = 0;
    for (const KeyBinding& b : table) {
        grid.key_width = std::max(grid.key_width, text_width(binding_label_short(b), m.font));
        text_width_max = std::max(text_width_max, text_width(b.text, m.font));
    }
    grid.text_x = grid.key_width + help_text_gap(m);
    for (int block = 0; block < kKeyBlockCount; ++block) {
        text_width_max = std::max(text_width_max,
                                  text_width(key_block_name(static_cast<KeyBlock>(block)), m.font) -
                                      grid.text_x);
    }
    grid.column_width = grid.text_x + text_width_max;
    // La fila 0 es el título.
    if (max_rows < 3) {
        return grid;
    }
    int column = 0;
    int row = 1;
    for (int block = 0; block < kKeyBlockCount; ++block) {
        bool header = false;
        for (std::size_t i = 0; i < table.size(); ++i) {
            const KeyBinding& b = table[i];
            if (static_cast<int>(b.block) != block) {
                continue;
            }
            if (!header) {
                // Un encabezado necesita debajo al menos una entrada en la misma columna.
                if (row + 2 > max_rows) {
                    ++column;
                    row = 1;
                }
                grid.cells.push_back({0, column, row++, b.block, -1});
                header = true;
            }
            if (row >= max_rows) {
                ++column;
                row = 1;
            }
            grid.cells.push_back({0, column, row++, b.block, static_cast<int>(i)});
            grid.rows = std::max(grid.rows, row);
        }
    }
    if (grid.cells.empty()) {
        return grid;
    }
    // Páginas: las columnas que caben a lo ancho (al menos una).
    const int per_page =
        std::max(1, (max_width + grid.column_gap) / (grid.column_width + grid.column_gap));
    const int total = column + 1;
    grid.pages = (total + per_page - 1) / per_page;
    grid.columns = std::min(total, per_page);
    for (HelpCell& cell : grid.cells) {
        cell.page = cell.column / per_page;
        cell.column %= per_page;
    }
    return grid;
}

ScreenLayout layout_screen(int width, int height) {
    ScreenLayout layout;
    layout.width = width;
    layout.height = height;
    layout.m = ui_metrics(height);
    const UiMetrics& m = layout.m;
    layout.perf = perf_geometry(m);
    layout.graphs = graph_geometry(m);
    auto& r = layout.rects;
    const auto at = [&r](Layer layer) -> UiRect& { return r[static_cast<std::size_t>(layer)]; };

    // Arriba, de derecha a izquierda: F1, T y el panel de depuración con el resto del ancho.
    UiRect& perf = at(Layer::Perf);
    perf.w = perf_content_width(layout.perf, m) + 2 * m.padding;
    perf.h = perf_content_height(layout.perf) + 2 * m.padding;
    perf.x = width - m.margin - perf.w;
    perf.y = m.margin;

    UiRect& graphs = at(Layer::Graphs);
    graphs.w = layout.graphs.plot_width + 2 * m.padding;
    graphs.h = graphs_content_height(m, layout.graphs) + 2 * m.padding;
    graphs.x = perf.x - m.gap - graphs.w;
    graphs.y = m.margin;

    UiRect& debug = at(Layer::Debug);
    debug.x = m.margin;
    debug.y = m.margin;
    debug.w = graphs.x - m.gap - debug.x;
    debug.h = debug_content_height(m) + 2 * m.padding;

    // Abajo: el indicador a la izquierda y el aviso de la ayuda a la derecha; encima, los
    // mensajes.
    UiRect& indicator = at(Layer::Indicator);
    indicator.w = samples_width(kIndicatorSamples, m.big_font);
    indicator.h = m.big_line;
    indicator.x = m.margin;
    indicator.y = height - m.margin - indicator.h;

    UiRect& hint = at(Layer::HelpHint);
    hint.w = samples_width(kHelpHintSamples, m.font);
    hint.h = m.line;
    hint.x = width - m.margin - hint.w;
    hint.y = indicator.bottom() - hint.h;

    UiRect& messages = at(Layer::Messages);
    messages.x = m.margin;
    messages.w = width - 2 * m.margin;
    messages.h = kMessageLines * m.line;
    messages.y = indicator.y - m.gap / 2 - messages.h;

    // La ayuda, en tantas columnas como pida su alto, desde arriba hasta los mensajes: mientras
    // se ve, no se dibujan la depuración, el rendimiento ni las gráficas (layers_exclusive).
    UiRect& help = at(Layer::Help);
    help.x = m.margin;
    help.y = m.margin;
    help.w = width - 2 * m.margin;
    help.h = messages.y - m.gap / 2 - help.y;
    layout.help =
        help_grid(key_table(), m, (help.h - 2 * m.padding) / m.line, help.w - 2 * m.padding);

    // El anillo, centrado, con su centro al 62 % del alto: la línea de debajo y el anillo caben
    // sobre los mensajes y bajo los paneles de arriba (lo comprueban los tests de este archivo).
    RingGeometry& ring = layout.ring;
    ring.diameter = std::max(8, height * 11 / 100);
    ring.thickness = std::max(2, ring.diameter / 8);
    ring.label_y = ring.diameter + m.px(6);
    UiRect& ring_rect = at(Layer::ActionRing);
    ring_rect.w = std::max(ring.diameter, samples_width(kRingSamples, m.font));
    ring_rect.h = ring.label_y + m.line;
    ring_rect.x = (width - ring_rect.w) / 2;
    ring_rect.y = height * 62 / 100 - ring.diameter / 2;
    return layout;
}

std::span<const std::string_view> layer_samples(Layer layer) {
    switch (layer) {
    case Layer::Indicator:
        return kIndicatorSamples;
    case Layer::HelpHint:
        return kHelpHintSamples;
    case Layer::Debug:
        return kDebugSamples;
    case Layer::Perf:
        return kPerfSamples;
    case Layer::ActionRing:
        return kRingSamples;
    case Layer::Messages:
    case Layer::Help:
    case Layer::Graphs:
    case Layer::Count:
        break;
    }
    return {};
}

int layer_content_width(Layer layer, const ScreenLayout& layout) {
    const UiMetrics& m = layout.m;
    switch (layer) {
    case Layer::Indicator:
        return samples_width(kIndicatorSamples, m.big_font);
    case Layer::HelpHint:
        return samples_width(kHelpHintSamples, m.font);
    case Layer::Debug:
        return std::max(samples_width(kDebugSamples, m.font), axis_bars_width(m));
    case Layer::Perf:
        return perf_content_width(layout.perf, m);
    case Layer::ActionRing:
        return std::max(layout.ring.diameter, samples_width(kRingSamples, m.font));
    case Layer::Help:
        return layout.help.columns * layout.help.column_width +
               std::max(0, layout.help.columns - 1) * layout.help.column_gap;
    case Layer::Graphs: {
        int width = layout.graphs.plot_width;
        for (const GraphPage& page : default_graph_pages()) {
            width = std::max(
                width, text_width(std::string("gráficas 8 · ") + page.title + "   (T: siguiente)",
                                  m.font));
        }
        return width;
    }
    case Layer::Messages:
    case Layer::Count:
        break;
    }
    return 0;
}

} // namespace rally::platform
