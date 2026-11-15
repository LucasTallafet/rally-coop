#include "platform/dials_draw.h"

#include "platform/demo_common.h"
#include "platform/ui_text.h"

#include <raylib.h>

#include <algorithm>
#include <string>

namespace rally::platform {

using namespace demo;

namespace {

constexpr Color kPanel{20, 22, 26, 235};
constexpr Color kWheelBackground{30, 34, 40, 255};
constexpr Color kWheelRing{70, 76, 88, 255};
constexpr Color kSelected{255, 170, 40, 255}; // el color del cursor de la tablet
constexpr Color kButton{44, 49, 58, 255};

// Etiquetas cortas de las posiciones (caben en la rueda; la ayuda y la tabla de teclas tienen los
// nombres completos).
constexpr const char* kMainLabels[7] = {"6", "5", "4", "3", "2", "1", "H"};
constexpr const char* kSecondaryLabels[8] = {"cresta", "salto",    "cuidado", "cierra",
                                             "abre",   "no cort.", "larga",   "y"};
constexpr const char* kDistanceLabels[8] = {"y", "30", "50", "80", "100", "150", "200", "300+"};

Vector2 vec(int x, int y) { return Vector2{static_cast<float>(x), static_cast<float>(y)}; }

// Texto con su centro en (cx, cy), sin salirse de la capa.
void centered(Overlay& overlay, Layer layer, int cx, int cy, const std::string& text, Color color,
              bool big = false) {
    const UiMetrics& m = overlay.metrics();
    const int font = big ? m.big_font : m.font;
    const int w = text_width(text, font);
    const UiRect& r = overlay.rect(layer);
    const int x = std::clamp(cx - w / 2, r.x + 1, std::max(r.x + 1, r.right() - w - 1));
    const int y = std::clamp(cy - font / 2, r.y + 1, std::max(r.y + 1, r.bottom() - font - 1));
    overlay.text(layer, x, y, text, color, big);
}

void button(Overlay& overlay, Layer layer, const UiRect& r, const std::string& text, Color fill,
            Color color) {
    DrawRectangle(r.x, r.y, r.w, r.h, fill);
    centered(overlay, layer, r.x + r.w / 2, r.y + r.h / 2, text, color);
}

void wheel(Overlay& overlay, Layer layer, const DialCircle& c, const WheelSpec& spec,
           const char* const* labels, int count, int side, bool open, int selected) {
    const float unit = static_cast<float>(std::max(1.0, overlay.metrics().scale));
    DrawCircle(c.cx, c.cy, static_cast<float>(c.r), kWheelBackground);
    DrawRing(vec(c.cx, c.cy), static_cast<float>(c.r) - unit, static_cast<float>(c.r), 0.0f, 360.0f,
             48, kWheelRing);
    // La zona muerta del centro (de donde no se elige nada).
    DrawCircleLines(c.cx, c.cy, kWheelDeadzone * static_cast<float>(c.r), kWheelRing);
    for (int i = 0; i < count; ++i) {
        const bool chosen = open && i == selected;
        const DialPoint p = dial_item_point(c, spec, i, side, chosen ? 0.78f : 0.74f);
        if (chosen) {
            DrawLineEx(vec(c.cx, c.cy), vec(p.x, p.y), 2.0f * unit, kSelected);
        }
        centered(overlay, layer, p.x, p.y, labels[i], chosen ? kSelected : kText, chosen);
    }
}

} // namespace

void DialsDraw::draw(Overlay& overlay, const DialsGeometry& g, const DialsView& view,
                     const net::NotesState& notes, double car_s) {
    const std::int64_t t0 = now_ns();
    const UiMetrics& m = overlay.metrics();
    overlay.panel(Layer::DialsLeft, kPanel);
    overlay.panel(Layer::DialsRight, kPanel);

    // --- Izquierda: el dial principal.
    const bool main_open = view.open == DialKind::Main;
    const bool left = view.side == game::NoteSide::Left;
    centered(overlay, Layer::DialsLeft, g.side_label.x + g.side_label.w / 2,
             g.side_label.y + g.side_label.h / 2,
             main_open ? (left ? "IZQUIERDA" : "DERECHA") : "curvas", main_open ? kSelected : kDim,
             true);
    // Las dos medias ruedas: la del lado elegido, con su elección; la otra, en reposo.
    DrawCircle(g.main.cx, g.main.cy, static_cast<float>(g.main.r), kWheelBackground);
    DrawCircleLines(g.main.cx, g.main.cy, static_cast<float>(g.main.r), kWheelRing);
    DrawLine(g.main.cx, g.main.cy - g.main.r, g.main.cx, g.main.cy + g.main.r, kWheelRing);
    for (int side = -1; side <= 1; side += 2) {
        const bool this_side = main_open && ((side < 0) == left);
        for (int i = 0; i < kMainWheel.count; ++i) {
            const bool chosen = this_side && i == view.selected;
            const DialPoint p =
                dial_item_point(g.main, kMainWheel, i, side, chosen ? 0.80f : 0.70f);
            if (chosen) {
                DrawLineEx(vec(g.main.cx, g.main.cy), vec(p.x, p.y),
                           2.0f * static_cast<float>(std::max(1.0, m.scale)), kSelected);
            }
            centered(overlay, Layer::DialsLeft, p.x, p.y, kMainLabels[i],
                     chosen ? kSelected : (main_open && !this_side ? kDim : kText), chosen);
        }
    }
    button(overlay, Layer::DialsLeft, g.erase, view.erase_progress > 0.0 ? "vaciando..." : "borrar",
           kButton, view.erase_progress > 0.0 ? kSelected : kText);
    if (view.erase_progress > 0.0) {
        DrawRectangle(g.erase.x, g.erase.bottom() - 2,
                      static_cast<int>(g.erase.w * view.erase_progress), 2, kSelected);
    }
    // Lo que el host aplicó: la última nota y las que quedan por delante.
    std::string last = "última: -";
    Color last_color = kDim;
    if (notes.last_result != game::NoteResult::Applied) {
        last = notes.last_result == game::NoteResult::NoNote ? "sin nota previa" : "rechazada";
        last_color = kError;
    } else if (notes.has_last) {
        last = "última: " + game::note_text(notes.last);
        last_color = kText;
    }
    overlay.text_fit(Layer::DialsLeft, g.last_line.x, g.last_line.y, last, last_color);
    overlay.text_fit(Layer::DialsLeft, g.pending_line.x, g.pending_line.y,
                     "pendientes: " + std::to_string(net::notes_ahead(notes, car_s)), kText);

    // --- Derecha: la forma de la distancia, el secundario y la distancia.
    button(overlay, Layer::DialsRight, g.form_button,
           view.form == DistanceForm::Wheel ? "dist.: rueda" : "dist.: contador", kButton, kStrong);
    overlay.text(Layer::DialsRight, g.secondary_title.x, g.secondary_title.y, "peligros y marcas",
                 kDim);
    wheel(overlay, Layer::DialsRight, g.secondary, kRing8Wheel, kSecondaryLabels, 8, 1,
          view.open == DialKind::Secondary, view.selected);
    overlay.text(Layer::DialsRight, g.distance_title.x, g.distance_title.y,
                 view.form == DistanceForm::Wheel ? "distancia (rueda)" : "distancia (contador)",
                 kDim);
    if (view.form == DistanceForm::Wheel) {
        wheel(overlay, Layer::DialsRight, g.distance, kRing8Wheel, kDistanceLabels, 8, 1,
              view.open == DialKind::Distance, view.selected);
    } else {
        const CounterGeometry& c = g.counter;
        const bool open = view.open == DialKind::Distance;
        DrawRectangle(c.display.x, c.display.y, c.display.w, c.display.h, kWheelBackground);
        centered(overlay, Layer::DialsRight, c.display.x + c.display.w / 2,
                 c.display.y + c.display.h / 2, std::to_string(view.counter_m) + " m",
                 open ? kSelected : kText, true);
        button(overlay, Layer::DialsRight, c.minus, "-", kButton, kText);
        button(overlay, Layer::DialsRight, c.plus, "+", kButton, kText);
        button(overlay, Layer::DialsRight, c.sing, "cantar", kButton, kStrong);
    }
    draw_ns_ = now_ns() - t0;
}

} // namespace rally::platform
