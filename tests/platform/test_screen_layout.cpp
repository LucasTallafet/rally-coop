#include "platform/copilot_keys.h"
#include "platform/keys.h"
#include "platform/screen_layout.h"
#include "platform/ui_text.h"

#include <doctest/doctest.h>

#include <cstdlib>
#include <set>
#include <string>
#include <tuple>
#include <vector>

using rally::platform::kLayerCount;
using rally::platform::Layer;
using rally::platform::layout_screen;
using rally::platform::ScreenLayout;
using rally::platform::UiRect;

namespace {

struct Resolution {
    int width;
    int height;
};
// La ventana por defecto, el monitor de Lucas y el habitual.
constexpr Resolution kResolutions[] = {{1280, 720}, {1360, 768}, {1920, 1080}};

std::string describe(const UiRect& r) {
    return std::to_string(r.x) + "," + std::to_string(r.y) + " " + std::to_string(r.w) + "×" +
           std::to_string(r.h);
}

UiRect inner(const UiRect& r, int padding) {
    return UiRect{r.x + padding, r.y + padding, r.w - 2 * padding, r.h - 2 * padding};
}

// Relleno de cada capa: las de una línea suelta no llevan fondo.
int padding_of(Layer layer, const ScreenLayout& layout) {
    return layer == Layer::Indicator || layer == Layer::HelpHint || layer == Layer::Messages ||
                   layer == Layer::ActionRing
               ? 0
               : layout.m.padding;
}

} // namespace

TEST_CASE("screen_layout: text sizes are multiples of 10") {
    CHECK(rally::platform::ui_metrics(720).font == 10);
    CHECK(rally::platform::ui_metrics(768).font == 10);
    CHECK(rally::platform::ui_metrics(1079).font == 10);
    CHECK(rally::platform::ui_metrics(1080).font == 20);
    CHECK(rally::platform::ui_metrics(1440).font == 20);
    CHECK(rally::platform::ui_metrics(2160).font == 40);
    CHECK(rally::platform::ui_metrics(1080).big_font == 30);
    CHECK(rally::platform::ui_metrics(1080).line == 22);
    CHECK(rally::platform::ui_metrics(720).line == 11);
}

TEST_CASE("screen_layout: with every layer visible no two rectangles overlap") {
    // Solo la ayuda comparte sitio, con los paneles de arriba, que no se dibujan con ella.
    CHECK(rally::platform::layers_exclusive(Layer::Help, Layer::Debug));
    CHECK(rally::platform::layers_exclusive(Layer::Perf, Layer::Help));
    CHECK_FALSE(rally::platform::layers_exclusive(Layer::Help, Layer::Messages));
    CHECK_FALSE(rally::platform::layers_exclusive(Layer::Debug, Layer::Graphs));
    for (const Resolution res : kResolutions) {
        const ScreenLayout layout = layout_screen(res.width, res.height);
        const UiRect screen{0, 0, res.width, res.height};
        for (int a = 0; a < kLayerCount; ++a) {
            const UiRect& ra = layout.rects[static_cast<std::size_t>(a)];
            INFO(res.width, "×", res.height, " ",
                 std::string(rally::platform::layer_name(Layer(a))), " ", describe(ra));
            CHECK(ra.w > 0);
            CHECK(ra.h > 0);
            CHECK(rally::platform::contains(screen, ra));
            for (int b = a + 1; b < kLayerCount; ++b) {
                const UiRect& rb = layout.rects[static_cast<std::size_t>(b)];
                INFO("frente a ", std::string(rally::platform::layer_name(Layer(b))), " ",
                     describe(rb));
                if (!rally::platform::layers_may_overlap(Layer(a), Layer(b))) {
                    CHECK_FALSE(rally::platform::overlaps(ra, rb));
                }
            }
        }
    }
}

TEST_CASE("screen_layout: each layer's worst case text fits in its rectangle") {
    for (const Resolution res : kResolutions) {
        const ScreenLayout layout = layout_screen(res.width, res.height);
        for (int a = 0; a < kLayerCount; ++a) {
            const Layer layer = static_cast<Layer>(a);
            const UiRect content = inner(layout.rect(layer), padding_of(layer, layout));
            INFO(res.width, "×", res.height, " ", std::string(rally::platform::layer_name(layer)),
                 " ", describe(content));
            CHECK(rally::platform::layer_content_width(layer, layout) <= content.w);
        }
        // F3: el área máxima tiene sitio para varias líneas (el reparto en páginas lo prueban
        // los tests de plan_debug).
        CHECK(rally::platform::plan_debug({}, layout).capacity >= 4);
        // F1: las filas de texto y la gráfica de fotogramas.
        const UiRect perf = inner(layout.rect(Layer::Perf), layout.m.padding);
        CHECK(layout.perf.graph_top + layout.perf.graph_height <= perf.h);
        // Los mensajes: kMessageLines líneas.
        CHECK(rally::platform::kMessageLines * layout.m.line <= layout.rect(Layer::Messages).h);
    }
}

TEST_CASE("screen_layout: every help page fits its rectangle and each key shows once") {
    for (const Resolution res : kResolutions) {
        const ScreenLayout layout = layout_screen(res.width, res.height);
        INFO(res.width, "×", res.height);
        const UiRect help = inner(layout.rect(Layer::Help), layout.m.padding);
        const rally::platform::HelpGrid& grid = layout.help;
        REQUIRE(grid.pages >= 1);
        const auto table = rally::platform::help_table(rally::platform::ScreenKind::Game);
        std::vector<int> shown(table.size(), 0);
        std::set<std::tuple<int, int, int>> places;
        for (const rally::platform::HelpCell& cell : grid.cells) {
            if (cell.binding >= 0) {
                ++shown[static_cast<std::size_t>(cell.binding)];
            }
            CHECK(cell.page < grid.pages);
            CHECK(cell.column < grid.columns);
            CHECK(cell.row >= 1); // la fila 0 es el título
            CHECK(cell.row < help.h / layout.m.line);
            // Sin solaparse: una celda por sitio.
            CHECK(places.insert({cell.page, cell.column, cell.row}).second);
        }
        for (const int n : shown) {
            CHECK(n == 1);
        }
        MESSAGE("ayuda a ", res.width, "×", res.height, ": ", grid.pages, " páginas de ",
                grid.columns, " columnas; ", grid.rows, " filas de ", help.h / layout.m.line,
                " que caben");
        CHECK(grid.rows * layout.m.line <= help.h);
        CHECK(rally::platform::layer_content_width(Layer::Help, layout) <= help.w);
        for (int page = 0; page < grid.pages; ++page) {
            CHECK(rally::platform::text_width(rally::platform::help_title(page, grid.pages),
                                              layout.m.font) <= help.w);
        }
        // Ninguna etiqueta de teclas se sale de su columna.
        for (const rally::platform::KeyBinding& b : table) {
            CHECK(rally::platform::text_width(rally::platform::binding_label_short(b),
                                              layout.m.font) <= grid.key_width);
            CHECK(grid.text_x + rally::platform::text_width(b.text, layout.m.font) <=
                  grid.column_width);
        }
    }
}

TEST_CASE("screen_layout: graph headers fit their panel") {
    for (const Resolution res : kResolutions) {
        const ScreenLayout layout = layout_screen(res.width, res.height);
        INFO(res.width, "×", res.height);
        const int width = layout.graphs.plot_width;
        CHECK(width <= rally::platform::kGraphColumns);
        for (const rally::platform::GraphPage& page : rally::platform::default_graph_pages()) {
            for (const rally::platform::GraphPanel& panel : page.panels) {
                const rally::platform::GraphHeader header =
                    rally::platform::graph_header(panel, layout.m, width);
                CHECK(header.count == static_cast<int>(panel.channels.size()) + 1);
                CHECK(rally::platform::text_width(rally::platform::graph_panel_title(panel),
                                                  layout.m.font) <= width);
                for (int i = 1; i < header.count; ++i) {
                    const auto& item = header.items[static_cast<std::size_t>(i)];
                    CHECK(item.x + rally::platform::text_width(
                                       rally::platform::graph_channel_sample(
                                           panel, panel.channels[static_cast<std::size_t>(i - 1)]),
                                       layout.m.font) <=
                          width);
                }
            }
        }
    }
}

TEST_CASE(
    "screen_layout: the action ring is centered below the middle and is drawn over the help") {
    // El anillo se dibuja siempre y encima de la ayuda: solo con ella puede compartir sitio.
    CHECK_FALSE(rally::platform::layers_exclusive(Layer::Help, Layer::ActionRing));
    CHECK(rally::platform::layers_overdraw(Layer::Help, Layer::ActionRing));
    CHECK(rally::platform::layers_overdraw(Layer::ActionRing, Layer::Help));
    for (int l = 0; l < kLayerCount; ++l) {
        if (Layer(l) != Layer::Help && Layer(l) != Layer::ActionRing) {
            CHECK_FALSE(rally::platform::layers_may_overlap(Layer::ActionRing, Layer(l)));
        }
    }
    for (const Resolution res : kResolutions) {
        const ScreenLayout layout = layout_screen(res.width, res.height);
        const UiRect& ring = layout.rect(Layer::ActionRing);
        INFO(res.width, "×", res.height, " ", describe(ring));
        // Centrado en x (± 1 px) y con el centro del anillo por debajo del de la pantalla.
        CHECK(std::abs(ring.x + ring.w / 2 - res.width / 2) <= 1);
        CHECK(ring.y + layout.ring.diameter / 2 > res.height / 2);
        CHECK(layout.ring.diameter >= 2 * layout.ring.thickness);
        CHECK(ring.h >= layout.ring.diameter + layout.m.line);
        // La línea de debajo cabe y no tapa los mensajes ni el indicador.
        CHECK(ring.bottom() <= layout.rect(Layer::Messages).y);
        // Con la ayuda abierta se ve entero y no pisa el indicador mínimo ni el aviso de la ayuda.
        CHECK(rally::platform::contains(UiRect{0, 0, res.width, res.height}, ring));
        CHECK_FALSE(rally::platform::overlaps(ring, layout.rect(Layer::Indicator)));
        CHECK_FALSE(rally::platform::overlaps(ring, layout.rect(Layer::HelpHint)));
        CHECK_FALSE(rally::platform::overlaps(ring, layout.rect(Layer::Messages)));
        CHECK(rally::platform::overlaps(ring, layout.rect(Layer::Help)));
    }
}

TEST_CASE("screen_layout: the game help does not show the copilot's key Z") {
    using namespace rally::platform;
    for (const KeyBinding& b : help_table(ScreenKind::Game)) {
        CHECK((b.contexts & kContextAll) != 0);
        CHECK(b.keys[0].key != Key::Z);
    }
    // Y la ayuda del juego sigue enseñando todo lo que vale en él.
    std::size_t in_game = 0;
    for (const KeyBinding& b : key_table()) {
        in_game += (b.contexts & kContextAll) != 0 ? 1 : 0;
    }
    CHECK(help_table(ScreenKind::Game).size() == in_game);
}

TEST_CASE("screen_layout: the copilot screen at 640x360 and 1920x1080 has no overlaps and fits") {
    using namespace rally::platform;
    constexpr Resolution kCopilotResolutions[] = {{640, 360}, {1920, 1080}};
    for (const Resolution res : kCopilotResolutions) {
        const ScreenLayout layout = layout_screen(res.width, res.height, ScreenKind::Copilot);
        const UiRect screen{0, 0, res.width, res.height};
        INFO(res.width, "x", res.height);
        for (int a = 0; a < kLayerCount; ++a) {
            const Layer layer = static_cast<Layer>(a);
            const UiRect& ra = layout.rects[static_cast<std::size_t>(a)];
            INFO(std::string(layer_name(layer)), " ", describe(ra));
            const bool unused = layer == Layer::Graphs || layer == Layer::ActionRing;
            if (unused) {
                CHECK(ra.w == 0);
                CHECK(ra.h == 0);
                continue;
            }
            CHECK(ra.w > 0);
            CHECK(ra.h > 0);
            CHECK(contains(screen, ra));
            // El texto de peor caso cabe en el rectángulo (sin el relleno).
            const UiRect content = inner(ra, padding_of(layer, layout));
            CHECK(layer_content_width(layer, layout) <= content.w);
            for (int b = a + 1; b < kLayerCount; ++b) {
                const Layer other = static_cast<Layer>(b);
                if (other == Layer::Graphs || other == Layer::ActionRing) {
                    continue;
                }
                INFO("frente a ", std::string(layer_name(other)));
                if (!layers_may_overlap(layer, other)) {
                    CHECK_FALSE(overlaps(ra, layout.rects[static_cast<std::size_t>(b)]));
                }
            }
        }
        // F3 con sus líneas y los mensajes.
        std::vector<DebugBlock> copilot_blocks;
        for (const std::string_view sample : layer_samples(Layer::Debug, ScreenKind::Copilot)) {
            copilot_blocks.push_back({1, text_width(sample, layout.m.font)});
        }
        CHECK(plan_debug(copilot_blocks, layout).pages.size() == 1);
        CHECK(kMessageLines * layout.m.line <= layout.rect(Layer::Messages).h);
        // F1 es el contador de siempre: sus filas y su gráfica caben.
        const UiRect perf = inner(layout.rect(Layer::Perf), layout.m.padding);
        CHECK(layout.perf.graph_top + layout.perf.graph_height <= perf.h);
        // Ayuda: cabe, y enseña cada fila del copiloto una vez.
        const UiRect help = inner(layout.rect(Layer::Help), layout.m.padding);
        const HelpGrid& grid = layout.help;
        REQUIRE(grid.pages >= 1);
        const auto table = help_table(ScreenKind::Copilot);
        std::vector<int> shown(table.size(), 0);
        for (const HelpCell& cell : grid.cells) {
            if (cell.binding >= 0) {
                ++shown[static_cast<std::size_t>(cell.binding)];
            }
        }
        for (const int n : shown) {
            CHECK(n == 1);
        }
        CHECK(grid.rows * layout.m.line <= help.h);
        CHECK(layer_content_width(Layer::Help, layout) <= help.w);
        // F1 y F3 son los de arriba y no se pisan entre sí.
        CHECK_FALSE(overlaps(layout.rect(Layer::Debug), layout.rect(Layer::Perf)));
    }
}

namespace {

using namespace rally::platform;

// Bloques sintéticos de 1 a 4 líneas con `lines` líneas en total y anchos distintos; el más ancho
// mide `widest`.
std::vector<DebugBlock> synthetic_blocks(int lines, int widest) {
    std::vector<DebugBlock> blocks;
    int left = lines;
    for (int i = 0; left > 0; ++i) {
        const int n = std::min(left, 1 + i % 4);
        blocks.push_back({n, 40 + (i * 37) % 120});
        left -= n;
    }
    blocks[blocks.size() / 2].width = widest;
    return blocks;
}

int total_lines(const std::vector<DebugBlock>& blocks) {
    int n = 0;
    for (const DebugBlock& b : blocks) {
        n += b.lines;
    }
    return n;
}

struct DebugCase {
    Resolution res;
    ScreenKind kind;
};
constexpr DebugCase kDebugCases[] = {{{640, 360}, ScreenKind::Copilot},
                                     {{1280, 720}, ScreenKind::Game},
                                     {{1360, 768}, ScreenKind::Game},
                                     {{1920, 1080}, ScreenKind::Game},
                                     {{1920, 1080}, ScreenKind::Copilot}};

} // namespace

TEST_CASE("screen_layout: the F3 panel height is its lines times the line height plus padding") {
    for (const DebugCase& c : kDebugCases) {
        const ScreenLayout layout = layout_screen(c.res.width, c.res.height, c.kind);
        for (const int lines : {10, 24, 40}) {
            const std::vector<DebugBlock> blocks = synthetic_blocks(lines, 300);
            const DebugPlan plan = plan_debug(blocks, layout);
            INFO(c.res.width, "x", c.res.height, " ", lines, " lineas, capacidad ", plan.capacity);
            const bool fits = lines + 1 <= plan.capacity;
            if (fits) {
                REQUIRE(plan.pages.size() == 1);
                const UiRect r = debug_panel_rect(layout, plan.pages[0].lines, plan.pages[0].width);
                CHECK(plan.pages[0].lines == lines + 1);
                CHECK(r.h == (lines + 1) * layout.m.line + 2 * layout.m.padding);
            } else {
                CHECK(plan.pages.size() >= 2);
            }
        }
    }
}

TEST_CASE("screen_layout: 40 lines of F3 take two pages at 1280x720 and 1360x768, one at 1080") {
    const auto pages = [](int w, int h) {
        const ScreenLayout layout = layout_screen(w, h);
        return plan_debug(synthetic_blocks(40, 300), layout).pages.size();
    };
    CHECK(pages(1280, 720) == 2);
    CHECK(pages(1360, 768) == 2);
    CHECK(pages(1920, 1080) == 1);
    // 24 líneas, 27 y 29 (la zona, y con copiloto) caben en una a 1360x768.
    for (const int lines : {24, 27, 29}) {
        CHECK(plan_debug(synthetic_blocks(lines, 300), layout_screen(1360, 768)).pages.size() == 1);
    }
}

TEST_CASE("screen_layout: F3 pages hold whole blocks, show every line once and never overflow") {
    for (const DebugCase& c : kDebugCases) {
        const ScreenLayout layout = layout_screen(c.res.width, c.res.height, c.kind);
        const UiRect screen{0, 0, c.res.width, c.res.height};
        const UiRect& area = layout.rect(Layer::Debug);
        for (const int lines : {10, 24, 40, 90}) {
            const std::vector<DebugBlock> blocks = synthetic_blocks(lines, 300);
            const DebugPlan plan = plan_debug(blocks, layout);
            INFO(c.res.width, "x", c.res.height, " ", lines);
            int next_block = 0;
            int shown = 0;
            for (std::size_t i = 0; i < plan.pages.size(); ++i) {
                const DebugPage& p = plan.pages[i];
                CHECK(p.first_block == next_block);
                CHECK(p.block_count > 0);
                next_block += p.block_count;
                int body = 0;
                int widest = 0;
                for (int b = p.first_block; b < p.first_block + p.block_count; ++b) {
                    body += blocks[static_cast<std::size_t>(b)].lines;
                    widest = std::max(widest, blocks[static_cast<std::size_t>(b)].width);
                }
                shown += body;
                CHECK(p.lines == body + 1);
                CHECK(p.lines <= plan.capacity);
                // El ancho es el de la línea más ancha (o el título, si es más ancho).
                const int title = text_width(
                    debug_title(static_cast<int>(i), static_cast<int>(plan.pages.size())),
                    layout.m.font);
                CHECK(p.width == std::max(widest, title));
                const UiRect r = debug_panel_rect(layout, p.lines, p.width);
                CHECK(r.w == p.width + 2 * layout.m.padding);
                CHECK(contains(area, r));
                CHECK(contains(screen, r));
                // Ni solapa con las demás capas de la pantalla.
                for (int l = 0; l < kLayerCount; ++l) {
                    if (Layer(l) != Layer::Debug && !layers_may_overlap(Layer::Debug, Layer(l))) {
                        CHECK_FALSE(overlaps(r, layout.rects[static_cast<std::size_t>(l)]));
                    }
                }
            }
            CHECK(next_block == static_cast<int>(blocks.size()));
            CHECK(shown == total_lines(blocks));
        }
    }
}

TEST_CASE("screen_layout: the F3 title shows the page only when there are several") {
    CHECK(debug_title(0, 1) == "DEPURACIÓN (F3)");
    CHECK(debug_title(0, 2) == "DEPURACIÓN (F3 1/2)");
    CHECK(debug_title(1, 2) == "DEPURACIÓN (F3 2/2)");
}

TEST_CASE("screen_layout: the F3 width is the widest line, the axis bars row included") {
    const ScreenLayout layout = layout_screen(1920, 1080);
    const int bars = axis_bars_width(layout.m);
    const std::vector<DebugBlock> blocks = {{3, 100}, {1, bars}, {2, 150}};
    const DebugPlan plan = plan_debug(blocks, layout);
    REQUIRE(plan.pages.size() == 1);
    CHECK(plan.pages[0].width == bars);
    CHECK(debug_panel_rect(layout, plan.pages[0].lines, plan.pages[0].width).w ==
          bars + 2 * layout.m.padding);
}

TEST_CASE("screen_layout: F3 area ends above the ring only where the ring is under it") {
    for (const DebugCase& c : kDebugCases) {
        const ScreenLayout layout = layout_screen(c.res.width, c.res.height, c.kind);
        const UiRect& area = layout.rect(Layer::Debug);
        INFO(c.res.width, "x", c.res.height);
        CHECK(area.bottom() <= layout.rect(Layer::Messages).y);
        if (c.kind == ScreenKind::Game) {
            const UiRect& ring = layout.rect(Layer::ActionRing);
            if (ring.x < area.right() && area.x < ring.right()) {
                CHECK(area.bottom() <= ring.y - layout.m.gap);
            }
        }
    }
}

TEST_CASE("screen_layout: the F3 width grows at once and drops at most once per second") {
    WidthHold hold;
    CHECK(hold.update(300, 0.0) == 300);
    CHECK(hold.update(400, 0.1) == 400);
    CHECK(hold.update(200, 0.5) == 400);
    CHECK(hold.update(250, 1.0) == 400);
    CHECK(hold.update(250, 1.2) == 250); // 1.1 s desde el último cambio (0.1)
    CHECK(hold.update(100, 1.6) == 250);
    CHECK(hold.update(100, 2.3) == 100);
    CHECK(hold.update(500, 2.4) == 500);
    CHECK(hold.update(500, 9.0) == 500);
}
