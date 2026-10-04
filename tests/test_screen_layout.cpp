#include "platform/keys.h"
#include "platform/screen_layout.h"
#include "platform/ui_text.h"

#include <doctest/doctest.h>

#include <string>

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
    return layer == Layer::Indicator || layer == Layer::HelpHint || layer == Layer::Messages
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
                CHECK_FALSE(rally::platform::overlaps(ra, rb));
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
        // Alto: el panel de depuración con sus líneas y la fila de las barras.
        const UiRect debug = inner(layout.rect(Layer::Debug), layout.m.padding);
        CHECK(static_cast<int>(rally::platform::layer_samples(Layer::Debug).size() + 1) *
                  layout.m.line <=
              debug.h);
        // F1: las filas de texto y la gráfica de fotogramas.
        const UiRect perf = inner(layout.rect(Layer::Perf), layout.m.padding);
        CHECK(layout.perf.graph_top + layout.perf.graph_height <= perf.h);
        // Los mensajes: kMessageLines líneas.
        CHECK(rally::platform::kMessageLines * layout.m.line <= layout.rect(Layer::Messages).h);
    }
}

TEST_CASE("screen_layout: the help shows every key row inside its rectangle") {
    for (const Resolution res : kResolutions) {
        const ScreenLayout layout = layout_screen(res.width, res.height);
        INFO(res.width, "×", res.height);
        const UiRect help = inner(layout.rect(Layer::Help), layout.m.padding);
        int entries = 0;
        for (const rally::platform::HelpCell& cell : layout.help.cells) {
            entries += cell.binding >= 0 ? 1 : 0;
            CHECK(cell.row < help.h / layout.m.line);
        }
        CHECK(entries == static_cast<int>(rally::platform::key_table().size()));
        CHECK(layout.help.rows * layout.m.line <= help.h);
        CHECK(rally::platform::layer_content_width(Layer::Help, layout) <= help.w);
        // Ninguna etiqueta de teclas se sale de su columna.
        for (const rally::platform::KeyBinding& b : rally::platform::key_table()) {
            CHECK(rally::platform::text_width(rally::platform::binding_label_short(b),
                                              layout.m.font) <= layout.help.key_width);
            CHECK(layout.help.text_x + rally::platform::text_width(b.text, layout.m.font) <=
                  layout.help.column_width);
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
