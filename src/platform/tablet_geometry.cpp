#include "platform/tablet_geometry.h"

#include <algorithm>
#include <cmath>

namespace rally::platform {

namespace {

// Los colores, por superficie (asfalto gris, grava beige, tierra marrón claro, arena amarillo
// pálido, nieve blanca) sobre el fondo oscuro del panel.
constexpr Rgb kColors[sim::kSurfaceCount] = {
    {150, 150, 158}, // asfalto
    {214, 190, 140}, // grava
    {166, 120, 80},  // tierra
    {245, 248, 252}, // nieve
    {240, 225, 150}, // arena
};

// Las rayas (raya, hueco, raya, hueco) de cada superficie, en unidades de `unit` px: asfalto
// continuo, grava rayas largas, tierra puntos, nieve doble línea continua, arena raya y punto.
struct StrokeShape {
    std::array<float, 4> dash;
    int lines;
};
constexpr StrokeShape kShapes[sim::kSurfaceCount] = {
    {{0, 0, 0, 0}, 1},  // asfalto: una línea continua
    {{12, 6, 0, 0}, 1}, // grava: rayas largas
    {{3, 5, 0, 0}, 1},  // tierra: puntos
    {{0, 0, 0, 0}, 2},  // nieve: doble línea continua
    {{12, 4, 3, 4}, 1}, // arena: raya y punto
};

constexpr double kStepChoices[] = {0.5, 1.0, 2.0, 5.0};

MapMark mark_at(const MapView& view, const sim::StagePose& pose) {
    MapMark mark;
    double px = 0.0;
    double py = 0.0;
    view.to_screen(pose.x, pose.z, px, py);
    const double delta = pose.heading - view.heading;
    mark.x = static_cast<float>(px);
    mark.y = static_cast<float>(py);
    mark.nx = static_cast<float>(std::cos(delta));
    mark.ny = static_cast<float>(std::sin(delta));
    mark.s = pose.s;
    return mark;
}

} // namespace

TabletLayout tablet_layout(const UiRect& panel, const UiMetrics& m) {
    TabletLayout layout;
    layout.panel = panel;
    const int pad = m.padding;
    const UiRect inner{panel.x + pad, panel.y + pad, panel.w - 2 * pad, panel.h - 2 * pad};
    const int ribbon_w = std::max(panel.w * 11 / 100, 4 * m.font);
    const int profile_h = std::max(panel.h * 18 / 100, 2 * m.line);
    layout.ribbon = UiRect{inner.right() - ribbon_w, inner.y, ribbon_w, inner.h};
    const int map_w = layout.ribbon.x - pad - inner.x;
    layout.profile = UiRect{inner.x, inner.bottom() - profile_h, map_w, profile_h};
    layout.map = UiRect{inner.x, inner.y, map_w, layout.profile.y - pad - inner.y};
    return layout;
}

SurfaceStroke surface_stroke(sim::Surface surface, float unit) {
    const int i = std::clamp(static_cast<int>(surface), 0, sim::kSurfaceCount - 1);
    SurfaceStroke stroke;
    stroke.color = kColors[i];
    for (std::size_t k = 0; k < 4; ++k) {
        stroke.dash[k] = kShapes[i].dash[k] * unit;
    }
    stroke.lines = kShapes[i].lines;
    return stroke;
}

double map_step(double ppm) {
    double best = kStepChoices[0];
    for (const double step : kStepChoices) {
        if (step * ppm <= 2.0) {
            best = step;
        }
    }
    return best;
}

bool map_point_at(const sim::StageTrack& track, const MapView& view, double s, MapMark& out) {
    if (s < track.s_begin() || s > track.s_end()) {
        return false;
    }
    out = mark_at(view, track.pose(s));
    return true;
}

void TabletRoad::build(const sim::StageTrack& track) {
    s_begin_ = track.s_begin();
    const std::size_t n =
        static_cast<std::size_t>(std::floor((track.s_end() - s_begin_) / kSpacing)) + 1;
    x_.resize(n);
    z_.resize(n);
    surface_.resize(n);
    for (std::size_t i = 0; i < n; ++i) {
        const sim::StagePose p = track.pose(s_at(i));
        x_[i] = p.x;
        z_[i] = p.z;
        surface_[i] = p.surface;
    }
    blocks_.clear();
    for (std::size_t first = 0; first < n; first += kBlock) {
        const std::size_t last = std::min(n, first + kBlock);
        Block b;
        for (std::size_t i = first; i < last; ++i) {
            b.cx += x_[i];
            b.cz += z_[i];
        }
        b.cx /= static_cast<double>(last - first);
        b.cz /= static_cast<double>(last - first);
        for (std::size_t i = first; i < last; ++i) {
            b.radius = std::max(b.radius, std::hypot(x_[i] - b.cx, z_[i] - b.cz));
        }
        blocks_.push_back(b);
    }
}

void build_map(const TabletRoad& road, const sim::StageTrack& track, const MapView& view,
               MapGeometry& out) {
    out.points.clear();
    out.runs.clear();
    out.marks.clear();
    out.scale_m = view.distance > 283.0 ? 100.0 : 50.0;
    out.scale_px = static_cast<float>(out.scale_m * view.ppm);
    out.step = map_step(view.ppm);
    out.s_lo = 0.0;
    out.s_hi = 0.0;
    if (road.empty()) {
        return;
    }
    const double phase_ppm = view.phase_ppm > 0.0 ? view.phase_ppm : view.ppm;
    const UiRect& r = view.rect;
    // Radio, en metros desde el coche, que cubre todo el rectángulo del mapa.
    const double far_x = std::max(view.arrow_x - r.x, r.right() - view.arrow_x);
    const double far_y = std::max(view.arrow_y - r.y, r.bottom() - view.arrow_y);
    const double reach = std::hypot(far_x, far_y) / view.ppm + 1.0;
    const std::int64_t stride =
        std::max<std::int64_t>(1, std::llround(out.step / TabletRoad::kSpacing));
    const std::int64_t count = static_cast<std::int64_t>(road.size());
    const double length = track.axis().length();
    std::int64_t last_k = -2;
    bool any = false;
    for (std::size_t b = 0; b < road.blocks().size(); ++b) {
        const TabletRoad::Block& block = road.blocks()[b];
        if (std::hypot(block.cx - view.cx, block.cz - view.cz) > reach + block.radius) {
            continue;
        }
        const std::int64_t i0 = static_cast<std::int64_t>(b) * TabletRoad::kBlock;
        const std::int64_t i1 = std::min(count, i0 + TabletRoad::kBlock);
        for (std::int64_t k = (i0 + stride - 1) / stride; k * stride < i1; ++k) {
            const std::size_t i = static_cast<std::size_t>(k * stride);
            const double s = road.s_at(i);
            double px = 0.0;
            double py = 0.0;
            view.to_screen(road.x(i), road.z(i), px, py);
            MapPoint point;
            point.x = static_cast<float>(px);
            point.y = static_cast<float>(py);
            point.phase = static_cast<float>(s * phase_ppm);
            const sim::Surface surface = road.surface(i);
            if (out.runs.empty() || k != last_k + 1) {
                // El primer trozo o tras un hueco: una polilínea nueva.
                out.runs.push_back({surface, static_cast<std::uint32_t>(out.points.size()), 0});
            } else if (surface != out.runs.back().surface) {
                // Un tramo nuevo de superficie, que comparte el punto de unión con el anterior.
                out.runs.push_back({surface, static_cast<std::uint32_t>(out.points.size() - 1), 1});
            }
            out.points.push_back(point);
            ++out.runs.back().count;
            last_k = k;
            if (!any) {
                out.s_lo = s;
                any = true;
            }
            out.s_hi = s;
            // Marca de 50 m (los múltiplos de 50 son puntos de la rejilla); solo en el tramo.
            if (s >= 0.0 && s <= length && std::fmod(s, kMarkSpacing) == 0.0) {
                out.marks.push_back(mark_at(view, track.pose(s)));
            }
        }
    }
}

void dash_segments(const MapPoint* points, std::uint32_t count, const SurfaceStroke& stroke,
                   float line_offset, std::vector<MapSegment>& out) {
    const double period =
        static_cast<double>(stroke.dash[0]) + stroke.dash[1] + stroke.dash[2] + stroke.dash[3];
    const bool solid = period < 1.0;
    const int lines = std::clamp(stroke.lines, 1, 2);
    for (std::uint32_t i = 0; i + 1 < count; ++i) {
        const MapPoint& a = points[i];
        const MapPoint& b = points[i + 1];
        const double dx = static_cast<double>(b.x) - a.x;
        const double dy = static_cast<double>(b.y) - a.y;
        const double len = std::sqrt(dx * dx + dy * dy);
        if (len <= 0.0) {
            continue;
        }
        // Desplazamiento de cada línea (normal al segmento).
        const float ox = static_cast<float>(-dy / len * line_offset);
        const float oy = static_cast<float>(dx / len * line_offset);
        const auto emit = [&](double t0, double t1) {
            const float x0 = static_cast<float>(a.x + dx * t0);
            const float y0 = static_cast<float>(a.y + dy * t0);
            const float x1 = static_cast<float>(a.x + dx * t1);
            const float y1 = static_cast<float>(a.y + dy * t1);
            if (lines == 1) {
                out.push_back({x0, y0, x1, y1});
            } else {
                out.push_back({x0 + ox, y0 + oy, x1 + ox, y1 + oy});
                out.push_back({x0 - ox, y0 - oy, x1 - ox, y1 - oy});
            }
        };
        if (solid) {
            emit(0.0, 1.0);
            continue;
        }
        // Recorre las rayas del trazo por la fase anclada a la carretera.
        const double p0 = a.phase;
        const double p1 = b.phase;
        const double span = p1 - p0;
        if (span <= 0.0) {
            continue;
        }
        double u = p0;
        while (u < p1) {
            const double q = std::fmod(u, period);
            double acc = 0.0;
            double remaining = p1 - u;
            bool on = false;
            for (int k = 0; k < 4; ++k) {
                const double d = stroke.dash[static_cast<std::size_t>(k)];
                if (q < acc + d) {
                    on = k % 2 == 0;
                    remaining = std::min(remaining, acc + d - q);
                    break;
                }
                acc += d;
            }
            remaining = std::max(remaining, 1e-3);
            if (on) {
                emit((u - p0) / span, std::min(1.0, (u + remaining - p0) / span));
            }
            u += remaining;
        }
    }
}

bool pick_s(const sim::StageTrack& track, const MapView& view, double s_car, double px, double py,
            double max_px, double& s_out) {
    const UiRect& r = view.rect;
    if (px < r.x || px >= r.right() || py < r.y || py >= r.bottom()) {
        return false;
    }
    double x = 0.0;
    double z = 0.0;
    view.to_world(px, py, x, z);
    const double lo = std::clamp(s_car, track.s_begin(), track.axis().length());
    const double s = track.project(x, z, lo, track.axis().length()).s;
    // El punto del eje elegido tiene que verse en el mapa y estar cerca del clic.
    const sim::StagePose p = track.pose(s);
    double qx = 0.0;
    double qy = 0.0;
    view.to_screen(p.x, p.z, qx, qy);
    if (qx < r.x || qx >= r.right() || qy < r.y || qy >= r.bottom() ||
        std::hypot(qx - px, qy - py) > max_px) {
        return false;
    }
    s_out = s;
    return true;
}

void build_profile(const sim::StageTrack& track, const UiRect& rect, int pad, double s_car,
                   double distance, ProfileGeometry& out) {
    out.points.clear();
    out.ok = false;
    const int w = rect.w - 2 * pad;
    const int h = rect.h - 2 * pad;
    if (w < 8 || h < 4 || distance <= 0.0) {
        return;
    }
    const int n = std::max(16, rect.w / 2);
    const double length = track.axis().length();
    out.min_height = 1e300;
    out.max_height = -1e300;
    // Primero las alturas (en y de metros), guardadas en el propio vector.
    for (int i = 0; i < n; ++i) {
        const double s = std::clamp(s_car + distance * i / (n - 1), track.s_begin(), length);
        const double height = track.pose(s).y;
        out.min_height = std::min(out.min_height, height);
        out.max_height = std::max(out.max_height, height);
        out.points.push_back(
            {static_cast<float>(rect.x + pad + static_cast<double>(w) * i / (n - 1)),
             static_cast<float>(height)});
    }
    const double span = std::max(out.max_height - out.min_height, kProfileMinSpan);
    const double centre = 0.5 * (out.max_height + out.min_height);
    out.y_lo = centre - 0.5 * span;
    out.y_hi = centre + 0.5 * span;
    const double bottom = rect.bottom() - pad;
    for (ProfilePoint& p : out.points) {
        p.y = static_cast<float>(bottom - (static_cast<double>(p.y) - out.y_lo) / span * h);
    }
    out.ok = true;
}

void build_ribbon(const std::vector<game::DirectionChange>& changes, const UiRect& rect, int pad,
                  double s_car, bool cursor_valid, double cursor_s, RibbonGeometry& out) {
    out.marks.clear();
    out.tick_count = 0;
    out.cursor_visible = false;
    out.cursor_beyond = false;
    out.x = static_cast<float>(rect.x + 0.5 * rect.w);
    out.bottom = static_cast<float>(rect.bottom() - pad);
    out.top = static_cast<float>(rect.y + pad);
    const double height = static_cast<double>(out.bottom) - out.top;
    const auto y_of = [&](double s) {
        return static_cast<float>(out.bottom - (s - s_car) / kRibbonLength * height);
    };
    for (const game::DirectionChange& c : changes) {
        if (c.s_start >= s_car && c.s_start <= s_car + kRibbonLength) {
            out.marks.push_back({y_of(c.s_start), c.angle >= 0.0 ? 1 : -1, c.s_start});
        }
    }
    for (int k = 1; k <= 5; ++k) {
        out.ticks[static_cast<std::size_t>(out.tick_count++)] = y_of(s_car + 100.0 * k);
    }
    if (cursor_valid && cursor_s >= s_car) {
        out.cursor_visible = true;
        out.cursor_beyond = cursor_s > s_car + kRibbonLength;
        out.cursor_y = out.cursor_beyond ? out.top : y_of(cursor_s);
    }
}

} // namespace rally::platform
