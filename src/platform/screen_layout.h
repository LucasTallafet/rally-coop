#pragma once

#include "platform/keys.h"
#include "platform/telemetry_history.h"

#include <array>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rally::platform {

// Sitio fijo de cada capa del texto en pantalla, calculado sin raylib para probar que no se
// pisan (tests/test_screen_layout.cpp). Cada capa tiene su rectángulo aunque esté oculta, así
// que nada se mueve al enseñar u ocultar otra.
//
// Texto: la fuente por defecto de raylib solo se ve limpia en múltiplos de 10 px. base = 10 ×
// max(1, alto / 540), en división entera (10 hasta 1079 de alto, 20 a 1080 y 1440, 40 a 2160);
// el indicador mínimo usa base + 10; la línea, base + base / 10. Geometría (márgenes, paneles,
// gráficas): escala continua s = alto / 1080. Los anchos de texto salen de platform/ui_text.

struct UiRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    int right() const { return x + w; }
    int bottom() const { return y + h; }
};
bool overlaps(const UiRect& a, const UiRect& b);
bool contains(const UiRect& outer, const UiRect& inner);

struct UiMetrics {
    int font = 20;     // texto normal
    int line = 22;     // salto de línea del texto normal
    int big_font = 30; // indicador mínimo
    int big_line = 33;
    double scale = 1.0; // geometría: alto / 1080
    int margin = 16;    // al borde de la pantalla
    int gap = 16;       // entre capas
    int padding = 10;   // dentro del fondo de un panel
    // v px de 1080 a esta escala, redondeado; al menos 1 si v > 0.
    int px(double v) const;
};
UiMetrics ui_metrics(int screen_height);

enum class Layer : std::uint8_t {
    Indicator, // velocidad, selector, vueltas y prueba a ciegas: siempre visible
    HelpHint,  // «H: ayuda»: siempre visible
    Messages,  // avisos: error de F5, ajustes.txt, grabación, pausa, texto fuera de su sitio
    Help,      // H
    Debug,     // F3
    Perf,      // F1
    Graphs,    // T
    Count,
};
inline constexpr int kLayerCount = static_cast<int>(Layer::Count);
std::string_view layer_name(Layer layer);
// Capas que nunca se dibujan a la vez (la ayuda tapa la depuración, el rendimiento y las gráficas
// mientras está abierta, tarea 2.5b): pueden compartir sitio. Todas las demás, no.
bool layers_exclusive(Layer a, Layer b);

inline constexpr int kMessageLines = 2;

// Contador de rendimiento (F1): filas de texto con columnas alineadas a la derecha y, debajo,
// la gráfica de fotogramas (platform/frame_graph).
struct PerfGeometry {
    int rows = 17;         // líneas de texto antes de la gráfica (con la vacía)
    int average_right = 0; // borde derecho de la columna de medias, desde el texto
    int max_right = 0;     // el de los máximos
    int steps_gap = 0;     // de los máximos a «N pasos»
    int graph_top = 0;     // desde el borde de arriba del texto
    int bar_width = 2;     // por fotograma
    int time_height = 125; // 0–50 ms
    int step_pixels = 4;   // por paso
    int graph_gap = 6;     // entre las partes de la gráfica
    int graph_width = 480; // FrameHistory::kCapacity × bar_width
    int graph_height = 0;  // con sus dos líneas de texto
    int graph_border = 4;  // fondo de la gráfica alrededor de las barras
};

// Gráficas de la telemetría (T): una página, con sus paneles uno debajo de otro. Encima de cada
// panel, su título y el valor de cada canal en huecos fijos (el ancho del peor caso), en las
// líneas que hagan falta.
struct GraphGeometry {
    int plot_width = kGraphColumns; // ≤ kGraphColumns: merge_columns junta las que sobran
    int plot_height = 90;
    int panel_gap = 10;
    int page_title_height = 26;
    int label_gap = 12;
};
inline constexpr int kMaxGraphHeaderItems = 8; // título y canales de un panel
struct GraphHeader {
    struct Item {
        int x = 0; // desde el borde izquierdo del panel
        int line = 0;
    };
    std::array<Item, kMaxGraphHeaderItems> items{}; // 0: título; 1..: canales
    int count = 0;
    int lines = 1;
};
// Texto de peor caso de un canal (lo que mide su hueco): "nombre -888.88" o "-888.8".
std::string graph_channel_sample(const GraphPanel& panel, const GraphChannel& channel);
std::string graph_panel_title(const GraphPanel& panel); // "título (unidad)"
GraphHeader graph_header(const GraphPanel& panel, const UiMetrics& m, int width);
// Alto de un panel (cabecera, curva y separación) y de una página entera.
int graph_panel_height(const GraphPanel& panel, const UiMetrics& m, const GraphGeometry& g);
int graph_page_height(const GraphPage& page, const UiMetrics& m, const GraphGeometry& g);

// Ayuda (H): bloques de la tabla de teclas en columnas. Cada fila es un encabezado de bloque o
// una entrada; cada entrada, la etiqueta corta de sus teclas y su mando y, alineado, el texto.
// Desde la 2.7 no cabe en una pantalla: las columnas que no caben a lo ancho pasan a la página
// siguiente (H: página siguiente y, tras la última, oculta). La fila 0 de cada página es su
// título (help_title); las celdas empiezan en la fila 1.
struct HelpCell {
    int page = 0;
    int column = 0; // dentro de su página
    int row = 0;    // desde 1: la 0 es el título
    KeyBlock block = KeyBlock::General;
    int binding = -1; // índice en key_table(); -1 en el encabezado
};
struct HelpGrid {
    int pages = 0;
    int columns = 0;      // columnas por página (las de la página más ancha)
    int rows = 0;         // filas de la columna más alta, con la del título
    int key_width = 0;    // ancho de la columna de las teclas
    int text_x = 0;       // del borde de la columna al texto
    int column_width = 0; // de la columna entera
    int column_gap = 0;
    std::vector<HelpCell> cells;
};
// Columnas de max_rows filas como mucho (con la del título) y páginas de max_width de ancho. Un
// encabezado nunca queda solo al final de una columna.
HelpGrid help_grid(std::span<const KeyBinding> table, const UiMetrics& m, int max_rows,
                   int max_width);
// Título de la página `page` (desde 0) de `pages`: "Ayuda 1/2 · H: página siguiente".
std::string help_title(int page, int pages);

struct ScreenLayout {
    int width = 0;
    int height = 0;
    UiMetrics m;
    PerfGeometry perf;
    GraphGeometry graphs;
    HelpGrid help;
    std::array<UiRect, kLayerCount> rects{};
    const UiRect& rect(Layer layer) const { return rects[static_cast<std::size_t>(layer)]; }
};
ScreenLayout layout_screen(int width, int height);

// Líneas de peor caso que cada capa tiene que poder mostrar dentro de su rectángulo (menos el
// relleno). La ayuda y las gráficas se miden con help_grid y graph_header; los mensajes se
// recortan con "..." al dibujar. Las mismas cifras que dibujan overlay.cpp, perf_counter.cpp y
// frame_graph.cpp, con 8 en cada dígito; si un texto real se sale, el overlay lo avisa.
std::span<const std::string_view> layer_samples(Layer layer);
// Ancho del contenido de la capa sin el relleno (lo que mide el test).
int layer_content_width(Layer layer, const ScreenLayout& layout);
// Ancho de una fila del panel de depuración con las barras de los ejes.
int axis_bars_width(const UiMetrics& m);
int axis_bar_width(const UiMetrics& m);

} // namespace rally::platform
