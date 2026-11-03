#include "sim/stage_ground.h"

#include "sim/check.h"
#include "sim/fnv1a.h"
#include "sim/rmath.h"
#include "sim/test_zone.h"

#include <algorithm>
#include <bit>
#include <utility>

namespace rally::sim {

namespace {

// Dos marcas del eje a menos de esto se funden (m).
constexpr double kMergeGap = 1.0e-3;
// d mínima en el peso 1/d⁶ (m): junto al borde, el peso es finito.
constexpr double kMinEdgeDistance = 0.01;
// Holgura al cortar una fila de la rejilla con los lados de un abanico (m).
constexpr double kRowSlack = 1.0e-6;
// La curvatura «justo antes» de una junta se lee tanto antes (m).
constexpr double kCurvatureProbe = 1.0e-7;

// Peso de un borde a una distancia d de su núcleo: 1/d⁸. Con 1/d⁴, a una celda del brazo alto
// de las lazadas (d = 0,75 m frente a 5,4 m del otro, 13 m de desnivel) el otro brazo apartaría
// 5 mm la altura del borde; con 1/d⁶, a la diagonal de la celda (1,06 m), 1,3 mm, que con los
// 2 cm de bajada pasan de los 2 cm del requisito 3; con 1/d⁸, 0,05 mm.
double edge_weight(double d) {
    const double d2 = d * d;
    const double d4 = d2 * d2;
    return 1.0 / (d4 * d4);
}

// Un borde que está más de kBlendNear más lejos que el más cercano pesa menos, y nada a partir de
// kBlendFar (paso suave): lejos de otras pasadas (a más de 30 m del eje, así que su borde queda a
// ≥ 26 m), el terreno a ≤ 10 m del borde propio no nota la otra pasada (requisito 4). Entre dos
// pasadas el peso relativo del borde lejano solo baja más al acercarse al cercano: sigue sin
// crestas ni hoyos (requisito 5). El hueco de las lazadas (6,15 m) queda por debajo de kBlendNear.
constexpr double kBlendNear = 8.0;
constexpr double kBlendFar = 16.0;

double blend_fade(double extra) {
    const double x = rmath::clamp((extra - kBlendNear) / (kBlendFar - kBlendNear), 0.0, 1.0);
    return 1.0 - x * x * (3.0 - 2.0 * x);
}

// Pies que una muestra guarda fuera del núcleo (los más cercanos a su borde).
constexpr int kMaxCandidates = 6;

std::int64_t floor_div(std::int64_t a, std::int64_t b) {
    std::int64_t q = a / b;
    if (a % b != 0 && a < 0) {
        --q;
    }
    return q;
}

// Clave de una baldosa: ordena por tz y después por tx.
std::int64_t tile_key(std::int32_t tx, std::int32_t tz) {
    return static_cast<std::int64_t>(tz) * (std::int64_t{1} << 32) +
           (static_cast<std::int64_t>(tx) + (std::int64_t{1} << 31));
}

// Punto del eje para los abanicos, respecto al origen del suelo.
struct FanPoint {
    double s = 0.0;
    double x = 0.0;
    double z = 0.0;
    double tx = 0.0; // tangente (sin θ, −cos θ)
    double tz = 0.0;
    double y = 0.0;
    double bank = 0.0;
    double left = 0.0; // semianchos de la calzada
    double right = 0.0;
    double curvature = 0.0;    // la de la pose en s (la del tramo que empieza)
    double curvature_in = 0.0; // justo antes de s: distinta si la curvatura salta en una junta
};

struct Origin {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

FanPoint fan_point(const StageTrack& track, double s, const Origin& o) {
    const StagePose p = track.pose(s);
    FanPoint f;
    f.s = s;
    f.x = p.x - o.x;
    f.z = p.z - o.z;
    f.tx = rmath::sin(p.heading);
    f.tz = -rmath::cos(p.heading);
    f.y = p.y - o.y;
    f.bank = p.bank;
    f.left = p.half_width_left;
    f.right = p.half_width_right;
    f.curvature = p.curvature;
    f.curvature_in = s > track.s_begin() ? track.pose(s - kCurvatureProbe).curvature : p.curvature;
    return f;
}

// 1 − κ·t en un extremo de un abanico, con la curvatura de los dos lados de la junta.
double strength(const FanPoint& p, double t) {
    return rmath::min(1.0 - p.curvature * t, 1.0 - p.curvature_in * t);
}

// Un abanico: el segmento del eje entre dos puntos y lo que barren sus normales, hasta `reach`
// a cada lado (menos en el lado interior de una curva cerrada, hasta su centro de curvatura).
struct Fan {
    std::uint32_t first = 0; // índice del primer punto
    double reach_left = 0.0;
    double reach_right = 0.0;
    Surface surface = Surface::Gravel; // de la calzada en el segmento
};

// Un pie fuera del núcleo: distancia a su borde, peso (sin la caída por distancia relativa) y
// altura.
struct Candidate {
    double d = 0.0;
    double w = 0.0;
    double h = 0.0;
};

// Lo que una muestra junta de los pies que tiene en el eje.
struct Acc {
    Candidate candidates[kMaxCandidates];
    int count = 0;
    bool overflow = false; // tenía más de kMaxCandidates pies con peso
    double min_dist = 0.0;
    double core_h = 0.0;
    double core_abs_t = 0.0;
    double nearest_dist = 0.0; // el pie más cercano fuera del núcleo y su altura (si Σw = 0)
    double nearest_h = 0.0;
    // El borde de núcleo más cercano (fuera del núcleo): su distancia y la altura del plano del
    // arcén prolongado hasta la muestra, menos la bajada (la regla de las celdas del borde).
    double edge_d = 0.0;
    double edge_plane_h = 0.0;
    bool core = false;
    Surface core_surface = Surface::Dirt;

    void reset() {
        count = 0;
        overflow = false;
        min_dist = std::numeric_limits<double>::infinity();
        nearest_dist = std::numeric_limits<double>::infinity();
        edge_d = std::numeric_limits<double>::infinity();
        core = false;
    }
};

// Rejilla de un rasterizado: n × n puntos desde (base_x, base_z), separados `cell`.
struct Grid {
    double base_x = 0.0;
    double base_z = 0.0;
    double cell = 0.0;
    int n = 0;
};

class Rasterizer {
public:
    Rasterizer(const std::vector<FanPoint>& points, const std::vector<Fan>& fans, double drop)
        : points_(points), fans_(fans), drop_(drop) {}

    // Índices de los dos extremos (semicircunferencias) tras los abanicos.
    std::uint32_t start_cap() const { return static_cast<std::uint32_t>(fans_.size()); }
    std::uint32_t end_cap() const { return static_cast<std::uint32_t>(fans_.size() + 1); }

    // Caja en planta de un abanico o un extremo.
    void bounds(std::uint32_t index, double& x0, double& x1, double& z0, double& z1) const {
        if (index >= fans_.size()) {
            const FanPoint& p = index == start_cap() ? points_.front() : points_.back();
            x0 = p.x - kStageFanReach;
            x1 = p.x + kStageFanReach;
            z0 = p.z - kStageFanReach;
            z1 = p.z + kStageFanReach;
            return;
        }
        double cx[4];
        double cz[4];
        corners(fans_[index], cx, cz);
        x0 = rmath::min(rmath::min(cx[0], cx[1]), rmath::min(cx[2], cx[3]));
        x1 = rmath::max(rmath::max(cx[0], cx[1]), rmath::max(cx[2], cx[3]));
        z0 = rmath::min(rmath::min(cz[0], cz[1]), rmath::min(cz[2], cz[3]));
        z1 = rmath::max(rmath::max(cz[0], cz[1]), rmath::max(cz[2], cz[3]));
    }

    // Suma el abanico o el extremo `index` a los puntos de `grid` que cubre.
    void add(std::uint32_t index, const Grid& grid, std::vector<Acc>& acc) const {
        double x0 = 0.0;
        double x1 = 0.0;
        double z0 = 0.0;
        double z1 = 0.0;
        bounds(index, x0, x1, z0, z1);
        const int k_lo = clamp_index(rmath::floor((z0 - grid.base_z) / grid.cell) - 1.0, grid.n);
        const int k_hi = clamp_index(rmath::ceil((z1 - grid.base_z) / grid.cell) + 1.0, grid.n);
        if (index >= fans_.size()) {
            const bool start = index == start_cap();
            const int i_lo =
                clamp_index(rmath::floor((x0 - grid.base_x) / grid.cell) - 1.0, grid.n);
            const int i_hi = clamp_index(rmath::ceil((x1 - grid.base_x) / grid.cell) + 1.0, grid.n);
            for (int k = k_lo; k <= k_hi; ++k) {
                for (int i = i_lo; i <= i_hi; ++i) {
                    add_cap(start, grid.base_x + i * grid.cell, grid.base_z + k * grid.cell,
                            acc[static_cast<std::size_t>(k * grid.n + i)]);
                }
            }
            return;
        }
        const Fan& fan = fans_[index];
        double cx[4];
        double cz[4];
        corners(fan, cx, cz);
        for (int k = k_lo; k <= k_hi; ++k) {
            const double z = grid.base_z + k * grid.cell;
            // Intervalo de x del cuadrilátero en esta fila, con holgura: un lado casi horizontal
            // (un rumbo de 180° da sin θ ≈ 1e-16) no puede dejar fuera la fila que lo toca. Las
            // pruebas exactas de add_fan deciden; esto solo acota el recorrido.
            double lo = 0.0;
            double hi = 0.0;
            bool any = false;
            for (int e = 0; e < 4; ++e) {
                const int f = (e + 1) % 4;
                const double za = cz[e];
                const double zb = cz[f];
                if (z < rmath::min(za, zb) - kRowSlack || z > rmath::max(za, zb) + kRowSlack) {
                    continue;
                }
                double xa = cx[e];
                double xb = cx[f];
                if (rmath::abs(zb - za) > kRowSlack) {
                    const double u = rmath::clamp((z - za) / (zb - za), 0.0, 1.0);
                    const double x = cx[e] + (cx[f] - cx[e]) * u;
                    xa = x;
                    xb = x;
                }
                lo = any ? rmath::min(lo, rmath::min(xa, xb)) : rmath::min(xa, xb);
                hi = any ? rmath::max(hi, rmath::max(xa, xb)) : rmath::max(xa, xb);
                any = true;
            }
            if (!any) {
                continue;
            }
            const int i_lo =
                clamp_index(rmath::floor((lo - grid.base_x) / grid.cell) - 1.0, grid.n);
            const int i_hi = clamp_index(rmath::ceil((hi - grid.base_x) / grid.cell) + 1.0, grid.n);
            for (int i = i_lo; i <= i_hi; ++i) {
                add_fan(fan, grid.base_x + i * grid.cell, z,
                        acc[static_cast<std::size_t>(k * grid.n + i)]);
            }
        }
    }

private:
    static int clamp_index(double v, int n) {
        return static_cast<int>(rmath::clamp(v, 0.0, static_cast<double>(n - 1)));
    }

    void corners(const Fan& fan, double* cx, double* cz) const {
        const FanPoint& a = points_[fan.first];
        const FanPoint& b = points_[fan.first + 1];
        // Normal derecha N = (−T_z, T_x).
        cx[0] = a.x + fan.reach_left * a.tz;
        cz[0] = a.z - fan.reach_left * a.tx;
        cx[1] = a.x - fan.reach_right * a.tz;
        cz[1] = a.z + fan.reach_right * a.tx;
        cx[2] = b.x - fan.reach_right * b.tz;
        cz[2] = b.z + fan.reach_right * b.tx;
        cx[3] = b.x + fan.reach_left * b.tz;
        cz[3] = b.z - fan.reach_left * b.tx;
    }

    static double fade(double dist) {
        return rmath::clamp((kStageFanReach - dist) / kStageFanFade, 0.0, 1.0);
    }

    static void add_outside(double weight, double dist, double d, double height, Acc& acc) {
        if (dist < acc.nearest_dist) {
            acc.nearest_dist = dist;
            acc.nearest_h = height;
        }
        if (!(weight > 0.0)) {
            return;
        }
        if (acc.count < kMaxCandidates) {
            acc.candidates[acc.count++] = Candidate{d, weight, height};
            return;
        }
        // Lleno: sustituye al más lejano si este está más cerca.
        acc.overflow = true;
        int far = 0;
        for (int k = 1; k < kMaxCandidates; ++k) {
            if (acc.candidates[k].d > acc.candidates[far].d) {
                far = k;
            }
        }
        if (d < acc.candidates[far].d) {
            acc.candidates[far] = Candidate{d, weight, height};
        }
    }

    // Pie en el abanico: s en [s_a, s_b), por la normal interpolada.
    void add_fan(const Fan& fan, double px, double pz, Acc& acc) const {
        const FanPoint& a = points_[fan.first];
        const FanPoint& b = points_[fan.first + 1];
        const double a0 = (px - a.x) * a.tx + (pz - a.z) * a.tz;
        if (a0 < 0.0) {
            return;
        }
        const double a1 = (px - b.x) * b.tx + (pz - b.z) * b.tz;
        if (a1 >= 0.0) {
            return;
        }
        // f(u) = (p − P(u))·T(u), con P y T lineales: cuadrática, f(0) = a0 ≥ 0 > f(1) = a1.
        const double dx = b.x - a.x;
        const double dz = b.z - a.z;
        const double dtx = b.tx - a.tx;
        const double dtz = b.tz - a.tz;
        const double qa = -(dx * dtx + dz * dtz);
        const double qb = (px - a.x) * dtx + (pz - a.z) * dtz - (dx * a.tx + dz * a.tz);
        double u = a0 / (a0 - a1);
        for (int it = 0; it < 3; ++it) {
            const double f = a0 + qb * u + qa * u * u;
            const double slope = qb + 2.0 * qa * u;
            if (slope == 0.0) {
                break;
            }
            u = rmath::clamp(u - f / slope, 0.0, 1.0);
        }
        const double foot_x = a.x + u * dx;
        const double foot_z = a.z + u * dz;
        const double tux = a.tx + u * dtx;
        const double tuz = a.tz + u * dtz;
        const double len = rmath::sqrt(tux * tux + tuz * tuz);
        const double ex = px - foot_x;
        const double ez = pz - foot_z;
        const double t = (-ex * tuz + ez * tux) / len;
        const double reach = t >= 0.0 ? fan.reach_right : fan.reach_left;
        if (rmath::abs(t) > reach) {
            return;
        }
        const double dist = rmath::sqrt(ex * ex + ez * ez);
        const double y = a.y + u * (b.y - a.y);
        const double bank = a.bank + u * (b.bank - a.bank);
        const double half =
            t >= 0.0 ? a.right + u * (b.right - a.right) : a.left + u * (b.left - a.left);
        const double edge = half + kStageShoulderWidth;
        acc.min_dist = rmath::min(acc.min_dist, dist);
        const double abs_t = rmath::abs(t);
        if (abs_t <= edge) {
            if (!acc.core || abs_t < acc.core_abs_t) {
                acc.core = true;
                acc.core_abs_t = abs_t;
                acc.core_h = y + bank * t - drop_;
                acc.core_surface = abs_t <= half ? fan.surface : Surface::Sand;
            }
            return;
        }
        // c: 1 − κ·t, lineal en el tramo, con las dos curvaturas en las juntas (si la curvatura
        // salta, un pie muere con c = 0 en la normal de la junta y no con un salto de peso).
        const double ca = strength(a, t);
        const double c = rmath::clamp(ca + u * (strength(b, t) - ca), 0.0, 1.0);
        const double d = rmath::max(abs_t - edge, kMinEdgeDistance);
        const double side = t >= 0.0 ? 1.0 : -1.0;
        if (abs_t - edge < acc.edge_d) {
            acc.edge_d = abs_t - edge;
            acc.edge_plane_h = y + bank * t - drop_;
        }
        add_outside(c * fade(dist) * edge_weight(d), dist, d, y + bank * side * edge - drop_, acc);
    }

    // Más allá de un extremo: el pie es el extremo.
    void add_cap(bool start, double px, double pz, Acc& acc) const {
        const FanPoint& p = start ? points_.front() : points_.back();
        const double along = (px - p.x) * p.tx + (pz - p.z) * p.tz;
        if (start ? along >= 0.0 : along < 0.0) {
            return;
        }
        const double ex = px - p.x;
        const double ez = pz - p.z;
        const double dist = rmath::sqrt(ex * ex + ez * ez);
        if (dist > kStageFanReach) {
            return;
        }
        const double t = -ex * p.tz + ez * p.tx;
        const double tc =
            rmath::clamp(t, -(p.left + kStageShoulderWidth), p.right + kStageShoulderWidth);
        const double qx = ex - tc * (-p.tz);
        const double qz = ez - tc * p.tx;
        const double d = rmath::max(rmath::sqrt(qx * qx + qz * qz), kMinEdgeDistance);
        acc.min_dist = rmath::min(acc.min_dist, dist);
        if (d < acc.edge_d) {
            // El plano del núcleo prolongado más allá del extremo: con su peralte y su pendiente
            // (la del último segmento del abanico), para que la celda que cruza el extremo no
            // asome.
            const FanPoint& q = start ? points_[1] : points_[points_.size() - 2];
            const double grade = (p.y - q.y) / (p.s - q.s);
            acc.edge_d = d;
            acc.edge_plane_h = p.y + grade * along + p.bank * t - drop_;
        }
        add_outside(fade(dist) * edge_weight(d), dist, d, p.y + p.bank * tc - drop_, acc);
    }

    const std::vector<FanPoint>& points_;
    const std::vector<Fan>& fans_;
    double drop_;
};

// Media de las alturas de los bordes con su peso y la caída por distancia relativa al más
// cercano. Falso si no hay peso.
bool blend(const Acc& a, double& h) {
    if (a.count == 0) {
        return false;
    }
    double nearest = a.candidates[0].d;
    for (int k = 1; k < a.count; ++k) {
        nearest = rmath::min(nearest, a.candidates[k].d);
    }
    double sum_w = 0.0;
    double sum_wh = 0.0;
    for (int k = 0; k < a.count; ++k) {
        const Candidate& c = a.candidates[k];
        const double w = c.w * blend_fade(c.d - nearest);
        sum_w += w;
        sum_wh += w * c.h;
    }
    if (!(sum_w > 0.0)) {
        return false;
    }
    h = sum_wh / sum_w;
    return true;
}

// s de las secciones del núcleo: en cada marca del eje y, entre dos, n partes iguales con el
// paso más largo que cumple los errores de cuerda, el alabeo y el paso máximo.
std::vector<double> section_positions(const StageTrack& track, const StageMeshDensity& density) {
    std::vector<double> marks;
    marks.push_back(track.s_begin());
    for (const double b : track.axis().breaks()) {
        marks.push_back(b);
    }
    marks.push_back(track.s_end());
    std::sort(marks.begin(), marks.end());
    std::vector<double> kept;
    for (const double m : marks) {
        if (kept.empty() || m - kept.back() >= kMergeGap) {
            kept.push_back(m);
        } else if (m == track.s_end()) {
            kept.back() = m;
        }
    }
    std::vector<double> out;
    for (std::size_t i = 0; i + 1 < kept.size(); ++i) {
        const double a = kept[i];
        const double b = kept[i + 1];
        const double span = b - a;
        const StagePose pa = track.pose(a);
        const StagePose pm = track.pose(a + 0.5 * span);
        // La pose en b es ya la del tramo siguiente si hay junta: casi en b.
        const StagePose pb = track.pose(a + span * (1.0 - 1.0e-9));
        const double curvature =
            rmath::max(rmath::abs(pa.curvature),
                       rmath::max(rmath::abs(pm.curvature), rmath::abs(pb.curvature)));
        const double half = rmath::max(rmath::max(pa.half_width_left, pa.half_width_right),
                                       rmath::max(pb.half_width_left, pb.half_width_right));
        const double edge = half + kStageShoulderWidth;
        const double bank_rate = rmath::abs(pb.bank - pa.bank) / span;
        const double width_rate =
            rmath::max(rmath::abs(pb.half_width_left - pa.half_width_left),
                       rmath::abs(pb.half_width_right - pa.half_width_right)) /
            span;
        double step = density.max_step;
        if (curvature > 0.0) {
            // Sagita de la línea de arcén exterior: Δs²·κ·(1 + κ·e)/8.
            step = rmath::min(step, rmath::sqrt(8.0 * density.chord_plan /
                                                (curvature * (1.0 + curvature * edge))));
        }
        // y'' de cada línea: y'' + 2·peralte'·semiancho' (peralte y semianchos lineales).
        const double line_curvature =
            rmath::abs(pm.vertical_curvature) + 2.0 * bank_rate * width_rate;
        if (line_curvature > 0.0) {
            step = rmath::min(step, rmath::sqrt(8.0 * density.chord_height / line_curvature));
        }
        // Alabeo en el centro de cada cuadrilátero: Δperalte·ancho/4.
        if (bank_rate > 0.0) {
            step =
                rmath::min(step, 4.0 * density.chord_height / (bank_rate * rmath::max(half, 1.0)));
        }
        const auto n = static_cast<int>(rmath::max(1.0, rmath::ceil(span / step)));
        for (int k = 0; k < n; ++k) {
            out.push_back(a + span * static_cast<double>(k) / static_cast<double>(n));
        }
    }
    out.push_back(kept.back());
    return out;
}

} // namespace

StageGround StageGround::build(const StageTrack& track, double cell, StageGroundKind kind,
                               const StageMeshDensity& density) {
    RALLY_CHECK(cell > 0.0, "StageGround::build: celda no válida");
    StageGround g;
    g.cell_ = cell;
    g.kind_ = kind;
    const std::vector<double> sections = section_positions(track, density);

    // Origen: centro de la caja del eje con las prolongaciones, redondeado al metro.
    {
        double x0 = 0.0;
        double x1 = 0.0;
        double y0 = 0.0;
        double y1 = 0.0;
        double z0 = 0.0;
        double z1 = 0.0;
        for (std::size_t i = 0; i < sections.size(); ++i) {
            const StagePose p = track.pose(sections[i]);
            x0 = i == 0 ? p.x : rmath::min(x0, p.x);
            x1 = i == 0 ? p.x : rmath::max(x1, p.x);
            y0 = i == 0 ? p.y : rmath::min(y0, p.y);
            y1 = i == 0 ? p.y : rmath::max(y1, p.y);
            z0 = i == 0 ? p.z : rmath::min(z0, p.z);
            z1 = i == 0 ? p.z : rmath::max(z1, p.z);
        }
        g.origin_x_ = rmath::floor(0.5 * (x0 + x1) + 0.5);
        g.origin_y_ = rmath::floor(0.5 * (y0 + y1) + 0.5);
        g.origin_z_ = rmath::floor(0.5 * (z0 + z1) + 0.5);
    }
    const Origin origin{g.origin_x_, g.origin_y_, g.origin_z_};

    // Núcleo.
    if (kind == StageGroundKind::Design) {
        g.sections_ = sections;
        g.vertices_.reserve(sections.size() * kStageSectionVertices);
        for (const double s : sections) {
            const StagePose p = track.pose(s);
            const double nx = rmath::cos(p.heading);
            const double nz = rmath::sin(p.heading);
            const double ts[kStageSectionVertices] = {-(p.half_width_left + kStageShoulderWidth),
                                                      -p.half_width_left, 0.0, p.half_width_right,
                                                      p.half_width_right + kStageShoulderWidth};
            for (const double t : ts) {
                StageVertex v;
                v.x = static_cast<float>(p.x + t * nx - origin.x);
                v.y = static_cast<float>(p.y + p.bank * t - origin.y);
                v.z = static_cast<float>(p.z + t * nz - origin.z);
                g.vertices_.push_back(v);
            }
        }
        g.triangles_.reserve((sections.size() - 1) * 2 * (kStageSectionVertices - 1));
        for (std::size_t i = 0; i + 1 < sections.size(); ++i) {
            const Surface road = track.pose(0.5 * (sections[i] + sections[i + 1])).surface;
            const auto base = static_cast<std::uint32_t>(i * kStageSectionVertices);
            const auto next = static_cast<std::uint32_t>(base + kStageSectionVertices);
            for (std::uint32_t j = 0; j + 1 < kStageSectionVertices; ++j) {
                const Surface surface =
                    j == 0 || j + 2 == kStageSectionVertices ? Surface::Sand : road;
                // A (izquierda) y B (derecha) en esta sección; C y D en la siguiente.
                const std::uint32_t va = base + j;
                const std::uint32_t vb = base + j + 1;
                const std::uint32_t vc = next + j;
                const std::uint32_t vd = next + j + 1;
                g.triangles_.push_back(StageTriangle{va, vb, vc, surface});
                g.triangles_.push_back(StageTriangle{vb, vd, vc, surface});
            }
        }
    }

    // Puntos de los abanicos: cada tramo entre secciones en partes de ≤ kStageFanStep.
    std::vector<FanPoint> points;
    std::vector<Fan> fans;
    for (std::size_t i = 0; i + 1 < sections.size(); ++i) {
        const double a = sections[i];
        const double span = sections[i + 1] - a;
        const auto n = static_cast<int>(rmath::max(1.0, rmath::ceil(span / kStageFanStep)));
        for (int k = 0; k < n; ++k) {
            points.push_back(fan_point(
                track, a + span * static_cast<double>(k) / static_cast<double>(n), origin));
        }
    }
    points.push_back(fan_point(track, sections.back(), origin));
    fans.reserve(points.size() - 1);
    for (std::size_t j = 0; j + 1 < points.size(); ++j) {
        Fan fan;
        fan.first = static_cast<std::uint32_t>(j);
        fan.surface = track.pose(0.5 * (points[j].s + points[j + 1].s)).surface;
        fan.reach_left = kStageFanReach;
        fan.reach_right = kStageFanReach;
        // Lado interior de una curva: hasta el centro de curvatura (más allá, 1 − κ·t < 0).
        for (const FanPoint* p : {&points[j], &points[j + 1]}) {
            for (const double curvature : {p->curvature, p->curvature_in}) {
                if (curvature > 0.0) {
                    fan.reach_right = rmath::min(fan.reach_right, 1.0 / curvature);
                } else if (curvature < 0.0) {
                    fan.reach_left = rmath::min(fan.reach_left, -1.0 / curvature);
                }
            }
        }
        fans.push_back(fan);
    }

    // Terreno: cada abanico (y extremo) en las baldosas de su caja, baldosa a baldosa.
    const double drop = kind == StageGroundKind::Design ? kStageTerrainDrop : 0.0;
    const Rasterizer raster(points, fans, drop);
    std::vector<std::pair<std::int64_t, std::uint32_t>> entries;
    const auto count = static_cast<std::uint32_t>(fans.size() + 2);
    for (std::uint32_t f = 0; f < count; ++f) {
        double x0 = 0.0;
        double x1 = 0.0;
        double z0 = 0.0;
        double z1 = 0.0;
        raster.bounds(f, x0, x1, z0, z1);
        const auto i0 = static_cast<std::int64_t>(rmath::floor(x0 / cell)) - 2;
        const auto i1 = static_cast<std::int64_t>(rmath::ceil(x1 / cell)) + 2;
        const auto k0 = static_cast<std::int64_t>(rmath::floor(z0 / cell)) - 2;
        const auto k1 = static_cast<std::int64_t>(rmath::ceil(z1 / cell)) + 2;
        // Baldosas cuyo intervalo cerrado de muestras corta [i0, i1] × [k0, k1].
        for (std::int64_t tz = floor_div(k0 - 1, kStageTileCells);
             tz <= floor_div(k1, kStageTileCells); ++tz) {
            for (std::int64_t tx = floor_div(i0 - 1, kStageTileCells);
                 tx <= floor_div(i1, kStageTileCells); ++tx) {
                entries.emplace_back(
                    tile_key(static_cast<std::int32_t>(tx), static_cast<std::int32_t>(tz)), f);
            }
        }
    }
    std::sort(entries.begin(), entries.end());

    constexpr int n = kStageTileSamples;
    // Se calcula una muestra más por cada lado (m × m): las celdas del borde de la baldosa se
    // deciden con las mismas muestras que en la baldosa vecina, así que el borde común coincide.
    constexpr int m = n + 2;
    std::vector<Acc> acc(static_cast<std::size_t>(m * m));
    std::vector<std::uint8_t> edge_cell(static_cast<std::size_t>(m * m));
    std::vector<Acc> centers;
    if (kind == StageGroundKind::GridOnly) {
        centers.resize(static_cast<std::size_t>(kStageTileCells * kStageTileCells));
    }
    for (std::size_t e = 0; e < entries.size();) {
        const std::int64_t key = entries[e].first;
        std::size_t end = e;
        while (end < entries.size() && entries[end].first == key) {
            ++end;
        }
        const auto tz = static_cast<std::int32_t>(floor_div(key, std::int64_t{1} << 32));
        const auto tx = static_cast<std::int32_t>(
            key - static_cast<std::int64_t>(tz) * (std::int64_t{1} << 32) -
            (std::int64_t{1} << 31));
        Grid grid;
        grid.cell = cell;
        grid.n = n;
        grid.base_x = static_cast<double>(static_cast<std::int64_t>(tx) * kStageTileCells) * cell;
        grid.base_z = static_cast<double>(static_cast<std::int64_t>(tz) * kStageTileCells) * cell;
        Grid wide = grid;
        wide.n = m;
        wide.base_x -= cell;
        wide.base_z -= cell;
        for (Acc& a : acc) {
            a.reset();
        }
        for (std::size_t k = e; k < end; ++k) {
            raster.add(entries[k].second, wide, acc);
        }
        // Celdas que cruzan el borde de un núcleo (unas esquinas dentro y otras fuera): todas sus
        // muestras toman el plano del arcén prolongado menos la bajada (segunda respuesta del
        // consultor, §1). Interpolar un plano da el mismo plano: la rejilla queda justo esa
        // bajada bajo la malla en el borde, sin asomar ni dejar escalón.
        std::fill(edge_cell.begin(), edge_cell.end(), std::uint8_t{0});
        if (kind == StageGroundKind::Design) {
            for (int b = 0; b + 1 < m; ++b) {
                for (int a = 0; a + 1 < m; ++a) {
                    const auto at = [&](int i, int k) {
                        return static_cast<std::size_t>((b + k) * m + a + i);
                    };
                    const int inside = (acc[at(0, 0)].core ? 1 : 0) + (acc[at(1, 0)].core ? 1 : 0) +
                                       (acc[at(0, 1)].core ? 1 : 0) + (acc[at(1, 1)].core ? 1 : 0);
                    if (inside > 0 && inside < 4) {
                        edge_cell[at(0, 0)] = 1;
                        edge_cell[at(1, 0)] = 1;
                        edge_cell[at(0, 1)] = 1;
                        edge_cell[at(1, 1)] = 1;
                    }
                }
            }
        }
        StageTile tile;
        tile.tx = tx;
        tile.tz = tz;
        tile.heights.resize(static_cast<std::size_t>(n * n));
        bool any = false;
        for (std::size_t k = 0; k < tile.heights.size(); ++k) {
            const std::size_t w = (k / n + 1) * m + (k % n + 1);
            const Acc& a = acc[w];
            if (!(a.min_dist < kStageStripCollision)) {
                tile.heights[k] = kStageNoCollision;
                continue;
            }
            g.candidate_overflows_ += a.overflow ? 1 : 0;
            double h = 0.0;
            if (a.core) {
                h = a.core_h;
            } else if (edge_cell[w] != 0 && a.edge_d < kStageFanReach) {
                h = a.edge_plane_h;
            } else if (!blend(a, h)) {
                // Todos los pesos son 0 (el centro de un arco): el pie más cercano.
                h = a.nearest_h;
                ++g.nearest_fallbacks_;
            }
            tile.heights[k] = static_cast<float>(h);
            any = true;
        }
        if (any && kind == StageGroundKind::GridOnly) {
            Grid inner = grid;
            inner.n = kStageTileCells;
            inner.base_x += 0.5 * cell;
            inner.base_z += 0.5 * cell;
            for (Acc& a : centers) {
                a.reset();
            }
            for (std::size_t k = e; k < end; ++k) {
                raster.add(entries[k].second, inner, centers);
            }
            tile.surfaces.resize(centers.size());
            for (std::size_t k = 0; k < centers.size(); ++k) {
                tile.surfaces[k] = static_cast<std::uint8_t>(
                    centers[k].core ? centers[k].core_surface : Surface::Dirt);
            }
        }
        if (any) {
            g.tiles_.push_back(std::move(tile));
        }
        e = end;
    }
    return g;
}

namespace {

// Proyección local de (x, z) en el eje cerca de la pose `p`: exacta en rectas y arcos (la
// circunferencia osculatriz en `p`) y, en una clotoide, con un error de la curvatura en los
// pocos metros que hay de la rueda al origen del coche. Sin búsquedas: una pose, una raíz y un
// atan2 (la proyección por piezas de StageTrack::project costaba 1,6 µs por rueda).
void local_projection(const StagePose& p, double x, double z, double& s, double& t,
                      double& heading) {
    const double tx = rmath::sin(p.heading);
    const double tz = -rmath::cos(p.heading);
    // Derecha N = (−T_z, T_x).
    const double nx = -tz;
    const double nz = tx;
    const double dx = x - p.x;
    const double dz = z - p.z;
    constexpr double kStraight = 1.0e-9;
    if (rmath::abs(p.curvature) < kStraight) {
        s = p.s + dx * tx + dz * tz;
        t = dx * nx + dz * nz;
        heading = p.heading;
        return;
    }
    const double r = 1.0 / p.curvature; // con signo: + centro a la derecha
    const double cx = p.x + r * nx;     // centro de curvatura
    const double cz = p.z + r * nz;
    const double ux = x - cx;
    const double uz = z - cz;
    const double dist = rmath::sqrt(ux * ux + uz * uz);
    const double sign = r > 0.0 ? 1.0 : -1.0;
    // Del centro al eje en `p`: −sign·N; el ángulo avanza hacia T.
    const double along = ux * tx + uz * tz;
    const double back = -sign * (ux * nx + uz * nz);
    const double phi = rmath::atan2(along, back);
    s = p.s + phi * rmath::abs(r);
    t = r - sign * dist;
    heading = p.heading + phi * sign;
}

} // namespace

StageReliefFrame stage_relief_frame(const StageTrack& track, double s_hint, double x, double z) {
    // Proyección local cerca de la s de la pasada (las ruedas están a menos de 1,5 m del origen
    // del coche, cuya s es s_hint del paso anterior).
    StagePose pose = track.pose(s_hint);
    double s = 0.0;
    double t = 0.0;
    double heading = 0.0;
    local_projection(pose, x, z, s, t, heading);
    s = rmath::clamp(s, track.s_begin(), track.s_end());
    const double edge =
        (t >= 0.0 ? pose.half_width_right : pose.half_width_left) + kStageShoulderWidth;
    if (rmath::abs(t) > edge + 0.5 * kStageReliefBlend) {
        // Fuera de este núcleo y de su franja: en la calzada, el arcén o la franja de mezcla de
        // otra pasada, la suya (sin la franja, h saltaría al entrar en el arcén de la otra).
        StageProjection other;
        if (track.on_any_road(x, z, other, 0.5 * kStageReliefBlend)) {
            pose = track.pose(other.s);
            s = other.s;
            t = other.t;
            heading = pose.heading;
        }
    }
    StageReliefFrame f;
    f.s = s;
    f.t = t;
    f.tx = rmath::sin(heading);
    f.tz = -rmath::cos(heading);
    f.half_left = pose.half_width_left;
    f.half_right = pose.half_width_right;
    track.axis().surface_change_near(s, f.surface_break, f.before, f.after);
    return f;
}

double stage_relief_height(const StageReliefFrame& f, const TestZone& noise, double s, double t,
                           double x, double z) {
    constexpr double kHalf = 0.5 * kStageReliefBlend;
    // Banda de la calzada, mezclada a lo largo de s en el cambio de superficie.
    const auto road = [&]() {
        const double d = s - f.surface_break;
        if (rmath::abs(d) < kHalf) {
            const double u = (d + kHalf) / kStageReliefBlend;
            return (1.0 - u) * noise.surface_noise(f.before, x, z) +
                   u * noise.surface_noise(f.after, x, z);
        }
        return noise.surface_noise(d < 0.0 ? f.before : f.after, x, z);
    };
    const double a = rmath::abs(t);
    const double w = t >= 0.0 ? f.half_right : f.half_left;
    const double e = w + kStageShoulderWidth;
    if (a < w - kHalf) {
        return road();
    }
    if (a < w + kHalf) {
        const double u = (a - (w - kHalf)) / kStageReliefBlend;
        return (1.0 - u) * road() + u * noise.surface_noise(Surface::Sand, x, z);
    }
    if (a < e - kHalf) {
        return noise.surface_noise(Surface::Sand, x, z);
    }
    if (a < e + kHalf) {
        const double u = (a - (e - kHalf)) / kStageReliefBlend;
        return (1.0 - u) * noise.surface_noise(Surface::Sand, x, z) +
               u * noise.surface_noise(Surface::Dirt, x, z);
    }
    return noise.surface_noise(Surface::Dirt, x, z);
}

std::uint64_t StageGround::hash() const {
    Fnv1a64 hash;
    hash.add_f64(origin_x_);
    hash.add_f64(origin_y_);
    hash.add_f64(origin_z_);
    hash.add_f64(cell_);
    hash.add_byte(static_cast<std::uint8_t>(kind_));
    const auto add_f32 = [&hash](float v) { hash.add_u32(std::bit_cast<std::uint32_t>(v)); };
    hash.add_u32(static_cast<std::uint32_t>(vertices_.size()));
    for (const StageVertex& v : vertices_) {
        add_f32(v.x);
        add_f32(v.y);
        add_f32(v.z);
    }
    hash.add_u32(static_cast<std::uint32_t>(triangles_.size()));
    for (const StageTriangle& t : triangles_) {
        hash.add_u32(t.a);
        hash.add_u32(t.b);
        hash.add_u32(t.c);
        hash.add_byte(static_cast<std::uint8_t>(t.surface));
    }
    hash.add_u32(static_cast<std::uint32_t>(tiles_.size()));
    for (const StageTile& t : tiles_) {
        hash.add_u32(static_cast<std::uint32_t>(t.tx));
        hash.add_u32(static_cast<std::uint32_t>(t.tz));
        for (const float h : t.heights) {
            add_f32(h);
        }
        hash.add_u32(static_cast<std::uint32_t>(t.surfaces.size()));
        for (const std::uint8_t s : t.surfaces) {
            hash.add_byte(s);
        }
    }
    return hash.value();
}

} // namespace rally::sim
