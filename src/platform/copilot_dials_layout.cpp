#include "platform/copilot_dials_layout.h"

#include <algorithm>
#include <cmath>

namespace rally::platform {

namespace {

constexpr float kPi = 3.14159265358979323846f;

UiRect inset(const UiRect& r, int by) {
    return UiRect{r.x + by, r.y + by, std::max(0, r.w - 2 * by), std::max(0, r.h - 2 * by)};
}

DialCircle circle_in(const UiRect& area) {
    DialCircle c;
    c.r = std::max(1, std::min(area.w, area.h) / 2);
    c.cx = area.x + area.w / 2;
    c.cy = area.y + area.h / 2;
    return c;
}

} // namespace

bool inside(const UiRect& r, double x, double y) {
    return x >= r.x && x < r.right() && y >= r.y && y < r.bottom();
}

bool inside(const DialCircle& c, double x, double y) {
    const double dx = x - c.cx;
    const double dy = y - c.cy;
    return dx * dx + dy * dy <= static_cast<double>(c.r) * c.r;
}

DialsGeometry dials_geometry(const UiRect& left, const UiRect& right, const UiMetrics& m) {
    DialsGeometry g;
    const int pad = std::max(2, m.padding / 2);
    const int line = m.line;
    // --- Columna izquierda, de arriba abajo: el lado, la rueda, borrar y las dos líneas.
    const UiRect l = inset(left, m.padding);
    g.side_label = UiRect{l.x, l.y, l.w, m.big_line};
    g.pending_line = UiRect{l.x, l.bottom() - line, l.w, line};
    g.last_line = UiRect{l.x, g.pending_line.y - line, l.w, line};
    g.erase = UiRect{l.x, g.last_line.y - pad - (line + pad), l.w, line + pad};
    const int wheel_top = g.side_label.bottom() + pad;
    const int wheel_bottom = g.erase.y - pad;
    g.main = circle_in(UiRect{l.x, wheel_top, l.w, std::max(2, wheel_bottom - wheel_top)});
    // --- Columna derecha: la forma, y dos mitades iguales con su título y su rueda.
    const UiRect r = inset(right, m.padding);
    g.form_button = UiRect{r.x, r.y, r.w, line + pad};
    const int below = g.form_button.bottom() + pad;
    const int half = std::max(2, (r.bottom() - below - pad) / 2);
    g.secondary_title = UiRect{r.x, below, r.w, line};
    g.secondary = circle_in(UiRect{r.x, below + line, r.w, std::max(2, half - line)});
    const int second = below + half + pad;
    g.distance_title = UiRect{r.x, second, r.w, line};
    const UiRect lower{r.x, second + line, r.w, std::max(2, r.bottom() - second - line)};
    g.distance = circle_in(lower);
    // El contador ocupa lo mismo que la rueda: los metros arriba, [−] y [+] en una fila y [cantar]
    // debajo.
    const int row = std::max(line + pad, lower.h / 3);
    g.counter.display = UiRect{lower.x, lower.y, lower.w, row};
    const int button_w = std::max(2, (lower.w - pad) / 2);
    g.counter.minus = UiRect{lower.x, lower.y + row + pad, button_w, row};
    g.counter.plus = UiRect{lower.x + lower.w - button_w, g.counter.minus.y, button_w, row};
    g.counter.sing = UiRect{lower.x, g.counter.minus.bottom() + pad, lower.w, row};
    return g;
}

DialPoint dial_item_point(const DialCircle& circle, const WheelSpec& spec, int index, int side,
                          float radial) {
    const float deg = wheel_center_deg(spec, index);
    const float rad = deg * kPi / 180.0f;
    float x = std::sin(rad);
    const float y = std::cos(rad);
    if (spec.shape == WheelShape::HalfCircle) {
        x *= static_cast<float>(side < 0 ? -1 : 1);
    }
    const float reach = radial * static_cast<float>(circle.r);
    return DialPoint{circle.cx + static_cast<int>(std::lround(x * reach)),
                     circle.cy - static_cast<int>(std::lround(y * reach))};
}

void dial_vector(const DialCircle& circle, double px, double py, float& x, float& y) {
    const double r = std::max(1, circle.r);
    x = static_cast<float>((px - circle.cx) / r);
    y = static_cast<float>((circle.cy - py) / r);
}

} // namespace rally::platform
