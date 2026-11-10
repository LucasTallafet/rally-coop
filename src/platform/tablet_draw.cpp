#include "platform/tablet_draw.h"

#include "platform/demo_common.h"

#include <raylib.h>

#include <algorithm>
#include <cmath>

namespace rally::platform {

using namespace demo;

namespace {

constexpr Color kPanel{20, 22, 26, 235};
constexpr Color kMapBackground{30, 34, 40, 255};
constexpr Color kMarkColor{120, 128, 140, 255};
constexpr Color kArrowColor{250, 250, 250, 255};
constexpr Color kCursorColor{255, 170, 40, 255};
constexpr Color kProfileColor{120, 200, 150, 255};
constexpr Color kRibbonColor{150, 156, 168, 255};

Color to_color(const Rgb& c) { return Color{c.r, c.g, c.b, 255}; }

Vector2 vec(float x, float y) { return Vector2{x, y}; }

// Triángulos que apuntan hacia arriba, a la derecha y a la izquierda, con los vértices en el
// sentido contrario a las agujas del reloj que pide raylib. `r`: tamaño (mitad del lado).
void draw_triangle_up(float x, float y, float r, Color color) {
    DrawTriangle(vec(x, y - r), vec(x - 0.7f * r, y + r), vec(x + 0.7f * r, y + r), color);
}
void draw_triangle_right(float x, float y, float r, Color color) {
    DrawTriangle(vec(x + r, y), vec(x - r, y - 0.7f * r), vec(x - r, y + 0.7f * r), color);
}
void draw_triangle_left(float x, float y, float r, Color color) {
    DrawTriangle(vec(x - r, y), vec(x + r, y + 0.7f * r), vec(x + r, y - 0.7f * r), color);
}

} // namespace

TabletDraw::TabletDraw() {
    map_.points.reserve(1024);
    map_.runs.reserve(16);
    map_.marks.reserve(64);
    profile_.points.reserve(1024);
    ribbon_.marks.reserve(32);
    segments_.reserve(4096);
}

void TabletDraw::draw(Overlay& overlay, const sim::StageTrack& track, const TabletView& view,
                      const TabletCursor& cursor, const std::vector<game::DirectionChange>& changes,
                      MapOrientation orientation) {
    const UiMetrics& m = overlay.metrics();
    layout_ = tablet_layout(overlay.rect(Layer::Tablet), m);
    const std::int64_t t0 = now_ns();
    map_view_ = view.map_view(layout_.map);
    build_map(road_, track, map_view_, map_);
    cursor_on_map_ = cursor.valid() && map_point_at(track, map_view_, cursor.s(), cursor_mark_);
    build_profile(track, layout_.profile, m.padding, view.s(), view.distance(), profile_);
    build_ribbon(changes, layout_.ribbon, m.padding, view.s(), cursor.valid(), cursor.s(), ribbon_);
    const std::int64_t t1 = now_ns();
    geometry_ns_ = t1 - t0;

    overlay.panel(Layer::Tablet, kPanel);
    draw_map(overlay, track, view, cursor, orientation);
    draw_profile(overlay, view);
    draw_ribbon(overlay);
    draw_ns_ = now_ns() - t1;
}

void TabletDraw::draw_map(Overlay& overlay, const sim::StageTrack& /*track*/,
                          const TabletView& view, const TabletCursor& /*cursor*/,
                          MapOrientation orientation) {
    const UiMetrics& m = overlay.metrics();
    const UiRect& r = layout_.map;
    const float unit = static_cast<float>(std::max(1.0, m.scale));
    DrawRectangle(r.x, r.y, r.w, r.h, kMapBackground);

    BeginScissorMode(r.x, r.y, r.w, r.h);
    // La carretera, por superficie: color y trazo.
    for (const MapRun& run : map_.runs) {
        if (run.count < 2) {
            continue;
        }
        const SurfaceStroke stroke = surface_stroke(run.surface, unit);
        segments_.clear();
        const float offset = stroke.lines == 2 ? 2.5f * unit : 0.0f;
        dash_segments(map_.points.data() + run.first, run.count, stroke, offset, segments_);
        const float thickness = stroke.lines == 2 ? 2.0f * unit : 3.0f * unit;
        const Color color = to_color(stroke.color);
        for (const MapSegment& s : segments_) {
            DrawLineEx(vec(s.x0, s.y0), vec(s.x1, s.y1), thickness, color);
        }
    }
    // Marcas cada 50 m, atravesadas a la carretera.
    for (const MapMark& mark : map_.marks) {
        const float half = 5.0f * unit;
        DrawLineEx(vec(mark.x - mark.nx * half, mark.y - mark.ny * half),
                   vec(mark.x + mark.nx * half, mark.y + mark.ny * half), 1.0f * unit, kMarkColor);
    }
    // El cursor (si cae en lo que se ve).
    if (cursor_on_map_) {
        const float rr = 7.0f * unit;
        DrawRing(vec(cursor_mark_.x, cursor_mark_.y), rr - 2.0f * unit, rr, 0.0f, 360.0f, 24,
                 kCursorColor);
        const float half = 11.0f * unit;
        DrawLineEx(
            vec(cursor_mark_.x - cursor_mark_.nx * half, cursor_mark_.y - cursor_mark_.ny * half),
            vec(cursor_mark_.x + cursor_mark_.nx * half, cursor_mark_.y + cursor_mark_.ny * half),
            2.0f * unit, kCursorColor);
    }
    // La flecha fija: el coche, siempre hacia arriba.
    draw_triangle_up(static_cast<float>(map_view_.arrow_x), static_cast<float>(map_view_.arrow_y),
                     7.0f * unit, kArrowColor);
    EndScissorMode();

    // La escala, abajo a la izquierda, y el nivel de zoom y la orientación, arriba.
    const int pad = m.padding;
    const int bar_y = r.bottom() - pad - m.line;
    const int bar_x = r.x + pad;
    const int bar_w = std::max(2, static_cast<int>(map_.scale_px));
    DrawLine(bar_x, bar_y + m.font / 2, bar_x + bar_w, bar_y + m.font / 2, kText);
    DrawLine(bar_x, bar_y + m.font / 2 - 3, bar_x, bar_y + m.font / 2 + 3, kText);
    DrawLine(bar_x + bar_w, bar_y + m.font / 2 - 3, bar_x + bar_w, bar_y + m.font / 2 + 3, kText);
    overlay.text(Layer::Tablet, bar_x + bar_w + m.font / 2, bar_y,
                 TextFormat("%.0f m", map_.scale_m), kText);
    overlay.text(Layer::Tablet, r.x + pad, r.y + pad,
                 TextFormat("%s · %.0f m",
                            orientation == MapOrientation::Axis ? "carretera" : "hacia delante",
                            kZoomDistance[view.zoom_level()]),
                 kDim);
}

void TabletDraw::draw_profile(Overlay& overlay, const TabletView& view) {
    const UiMetrics& m = overlay.metrics();
    const UiRect& r = layout_.profile;
    const float unit = static_cast<float>(std::max(1.0, m.scale));
    DrawRectangle(r.x, r.y, r.w, r.h, kMapBackground);
    if (!profile_.ok) {
        return;
    }
    BeginScissorMode(r.x, r.y, r.w, r.h);
    for (std::size_t i = 0; i + 1 < profile_.points.size(); ++i) {
        const ProfilePoint& a = profile_.points[i];
        const ProfilePoint& b = profile_.points[i + 1];
        DrawLineEx(vec(a.x, a.y), vec(b.x, b.y), 2.0f * unit, kProfileColor);
    }
    // El coche, a la izquierda.
    draw_triangle_up(profile_.points.front().x, profile_.points.front().y, 5.0f * unit,
                     kArrowColor);
    EndScissorMode();
    overlay.text(Layer::Tablet, r.x + m.padding, r.y + 1,
                 TextFormat("alt. %.0f a %.0f m · %.0f m", profile_.min_height, profile_.max_height,
                            view.distance()),
                 kDim);
}

void TabletDraw::draw_ribbon(Overlay& overlay) {
    const UiMetrics& m = overlay.metrics();
    const UiRect& r = layout_.ribbon;
    const float unit = static_cast<float>(std::max(1.0, m.scale));
    DrawRectangle(r.x, r.y, r.w, r.h, kMapBackground);
    DrawLineEx(vec(ribbon_.x, ribbon_.bottom), vec(ribbon_.x, ribbon_.top), 2.0f * unit,
               kRibbonColor);
    for (int i = 0; i < ribbon_.tick_count; ++i) {
        const float y = ribbon_.ticks[static_cast<std::size_t>(i)];
        DrawLineEx(vec(ribbon_.x - 3.0f * unit, y), vec(ribbon_.x + 3.0f * unit, y), 1.0f * unit,
                   kMarkColor);
    }
    // Las marcas de sentido: a la izquierda o a la derecha del eje, sin la forma de la curva.
    for (const RibbonMark& mark : ribbon_.marks) {
        const float size = 5.0f * unit;
        if (mark.direction > 0) {
            draw_triangle_right(ribbon_.x + 4.0f * unit + size, mark.y, size, kText);
        } else {
            draw_triangle_left(ribbon_.x - 4.0f * unit - size, mark.y, size, kText);
        }
    }
    if (ribbon_.cursor_visible) {
        const float half = 0.5f * r.w - 2.0f;
        DrawLineEx(vec(ribbon_.x - half, ribbon_.cursor_y), vec(ribbon_.x + half, ribbon_.cursor_y),
                   2.0f * unit, kCursorColor);
        if (ribbon_.cursor_beyond) {
            draw_triangle_up(ribbon_.x, ribbon_.cursor_y + 6.0f * unit, 4.0f * unit, kCursorColor);
        }
    }
    // El coche, abajo.
    draw_triangle_up(ribbon_.x, ribbon_.bottom, 5.0f * unit, kArrowColor);
    overlay.text(Layer::Tablet, r.x + 1, r.y + 1, "500 m", kDim);
}

} // namespace rally::platform
