#include "sim/stage_ground.h"

#include "sim/check.h"
#include "sim/fnv1a.h"
#include "sim/rmath.h"
#include "sim/test_zone.h"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <limits>
#include <memory>
#include <string>
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

// e(s) no mira el lado interior de una curva donde 1 − κ·t baja de esto (ahí no hay final libre).
constexpr double kMinFreeRho = 0.25;

// Bajo la banda, la rejilla queda bajo su menor altura en ±esto en lateral (m): más que la
// diagonal de una celda.
constexpr double kUnderWindow = 1.1;

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

// La banda de un lado en un punto del eje, para la rejilla (interpolada entre secciones), con las
// alturas respecto al origen del suelo y las distancias |t| al eje por su normal.
struct BandEdge {
    double t_edge = 0.0;  // borde del arcén E
    double y_edge = 0.0;  // su altura
    double grade = 0.0;   // pendiente lateral del arcén hacia fuera
    double rim_y = 0.0;   // cabeza de la cuneta
    double t_end = 0.0;   // final F
    double y_end = 0.0;   // su altura
    double slope = 0.0;   // pendiente hacia fuera de la última tira (de la cabeza a F)
    double exposed = 0.0; // 1 si el final es libre (la rejilla queda a la vista)
    double extra = 0.0;   // cota de la rejilla en el final, además de e(s) (las rampas)
};

// Punto del eje para los abanicos, respecto al origen del suelo.
struct FanPoint {
    double s = 0.0;
    double x = 0.0;
    double z = 0.0;
    double tx = 0.0; // tangente (sin θ, −cos θ)
    double tz = 0.0;
    double y = 0.0;
    double bank = 0.0;
    double curvature = 0.0;    // la de la pose en s (la del tramo que empieza)
    double curvature_in = 0.0; // justo antes de s: distinta si la curvatura salta en una junta
    BandEdge band[2];          // izquierda y derecha
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

// Un pie fuera del núcleo en la zona de su pasada (d < kStageEdgeBand + kStageEdgeBlend): su
// distancia al borde y la altura de la banda B = S − bajada − e(s) en la muestra.
struct ZoneFoot {
    double d = 0.0;
    double band = 0.0;
};

// Pies en zona que una muestra guarda (los más cercanos a su borde).
constexpr int kMaxZoneFeet = 8;

// Lo que una muestra junta de los pies que tiene en el eje.
struct Acc {
    Candidate candidates[kMaxCandidates];
    int count = 0;
    bool overflow = false; // tenía más de kMaxCandidates pies con peso
    double min_dist = 0.0;
    // Bajo la banda (el pie con menor |t| relativo, la banda «de la muestra»): la altura bajo
    // toda la banda (core_h), la de junto a un final libre (core_band), su d (≤ 0) y si el final
    // de esa banda es libre.
    double core_h = 0.0;
    double core_abs_t = 0.0;
    double nearest_dist = 0.0; // el pie más cercano fuera de las bandas y su altura (si Σw = 0)
    double nearest_h = 0.0;
    double core_d = 0.0;
    double core_band = 0.0;
    bool core_exposed = false;
    ZoneFoot zone[kMaxZoneFeet];
    int zone_count = 0;
    bool zone_overflow = false;
    bool core = false;
    // Solo diagnóstico (center_overflows): la menor distancia al vértice de las cuñas de un arco
    // cuyo abanico tiene un pie aquí, y la menor y la mayor altura de los pies con peso y de las
    // bandas de los pies en zona.
    double center_dist = 0.0;
    double foot_min = 0.0;
    double foot_max = 0.0;

    void reset() {
        count = 0;
        overflow = false;
        min_dist = std::numeric_limits<double>::infinity();
        nearest_dist = std::numeric_limits<double>::infinity();
        zone_count = 0;
        zone_overflow = false;
        core = false;
        center_dist = std::numeric_limits<double>::infinity();
        foot_min = std::numeric_limits<double>::infinity();
        foot_max = -std::numeric_limits<double>::infinity();
    }

    void add_zone(double d, double band) {
        foot_min = rmath::min(foot_min, band);
        foot_max = rmath::max(foot_max, band);
        if (zone_count < kMaxZoneFeet) {
            zone[zone_count++] = ZoneFoot{d, band};
            return;
        }
        zone_overflow = true;
        int far = 0;
        for (int k = 1; k < kMaxZoneFeet; ++k) {
            if (zone[k].d > zone[far].d) {
                far = k;
            }
        }
        if (d < zone[far].d) {
            zone[far] = ZoneFoot{d, band};
        }
    }
};

double smoothstep(double x) {
    const double u = rmath::clamp(x, 0.0, 1.0);
    return u * u * (3.0 - 2.0 * u);
}

// Rejilla de un rasterizado: n × n puntos desde (base_x, base_z), separados `cell`.
struct Grid {
    double base_x = 0.0;
    double base_z = 0.0;
    double cell = 0.0;
    int n = 0;
};

class Rasterizer {
public:
    // `error`: e(s) en el final libre de la banda.
    Rasterizer(const std::vector<FanPoint>& points, const std::vector<Fan>& fans, double drop,
               const StageCellError* error)
        : points_(points), fans_(fans), drop_(drop), error_(error) {}

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
        acc.foot_min = rmath::min(acc.foot_min, height);
        acc.foot_max = rmath::max(acc.foot_max, height);
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
        if (a.curvature != 0.0 && a.curvature == b.curvature_in) {
            // Abanico de un arco: el vértice de su cuña es el centro de curvatura, a 1/κ del eje
            // por la normal derecha N = (−T_z, T_x) (κ < 0: a la izquierda). Solo diagnóstico.
            const double vx = px - (a.x - a.tz / a.curvature);
            const double vz = pz - (a.z + a.tx / a.curvature);
            acc.center_dist = rmath::min(acc.center_dist, rmath::sqrt(vx * vx + vz * vz));
        }
        const double dist = rmath::sqrt(ex * ex + ez * ez);
        const double y = a.y + u * (b.y - a.y);
        const int lane = t >= 0.0 ? 1 : 0;
        const BandEdge band = mix(a.band[lane], b.band[lane], u);
        acc.min_dist = rmath::min(acc.min_dist, dist);
        const double abs_t = rmath::abs(t);
        const double s = a.s + u * (b.s - a.s);
        if (abs_t <= band.t_end) {
            // Distancia al final de la banda, también hacia los extremos del tramo.
            const double core_d = rmath::max(
                abs_t - band.t_end, rmath::max(points_.front().s - s, s - points_.back().s));
            if (!acc.core || core_d < acc.core_d) {
                acc.core = true;
                acc.core_abs_t = abs_t;
                acc.core_d = core_d;
                // Bajo la banda: su menor altura en ±kUnderWindow, menos lo que cambia a lo
                // largo de s en esa distancia (la mayor de las pendientes del eje, de la cabeza y
                // del final) y menos kStageCoreDrop.
                const double run = rmath::max(b.s - a.s, 1.0e-9);
                const double rate =
                    rmath::max(rmath::abs(b.y - a.y),
                               rmath::max(rmath::abs(b.band[lane].y_end - a.band[lane].y_end),
                                          rmath::abs(b.band[lane].rim_y - a.band[lane].rim_y))) /
                    run;
                acc.core_h =
                    profile_min(band, abs_t, kUnderWindow) - rate * kUnderWindow - kStageCoreDrop;
                acc.core_band = profile(band, abs_t) - drop_ - error_->at(s) - band.extra;
                acc.core_exposed = band.exposed > 0.5;
            }
            (void)y;
            return;
        }
        // c: 1 − κ·t, lineal en el tramo, con las dos curvaturas en las juntas (si la curvatura
        // salta, un pie muere con c = 0 en la normal de la junta y no con un salto de peso).
        const double ca = strength(a, t);
        const double c = rmath::clamp(ca + u * (strength(b, t) - ca), 0.0, 1.0);
        const double past = abs_t - band.t_end;
        const double d = rmath::max(past, kMinEdgeDistance);
        if (past < kStageEdgeBand + kStageEdgeBlend) {
            // Más allá del final, su última tira prolongada (con la pendiente de la tierra como
            // mucho).
            const double slope = rmath::clamp(band.slope, -kStageDirtSlope, kStageDirtSlope);
            acc.add_zone(past, band.y_end + slope * past - drop_ - error_->at(s) - band.extra);
        }
        add_outside(c * fade(dist) * edge_weight(d), dist, d, band.y_end - drop_, acc);
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
        const double tc = rmath::clamp(t, -p.band[0].t_end, p.band[1].t_end);
        const BandEdge& band = p.band[tc >= 0.0 ? 1 : 0];
        const double qx = ex - tc * (-p.tz);
        const double qz = ez - tc * p.tx;
        const double d_edge = rmath::sqrt(qx * qx + qz * qz);
        const double d = rmath::max(d_edge, kMinEdgeDistance);
        acc.min_dist = rmath::min(acc.min_dist, dist);
        const double h = profile(band, rmath::abs(tc));
        if (d_edge < kStageEdgeBand + kStageEdgeBlend) {
            // S más allá del extremo: la banda del extremo con su pendiente a lo largo de s (la
            // del último segmento del abanico).
            const FanPoint& q = start ? points_[1] : points_[points_.size() - 2];
            const double grade = (p.y - q.y) / (p.s - q.s);
            acc.add_zone(d_edge, h + grade * along - drop_ - error_->at(p.s) - band.extra);
        }
        add_outside(fade(dist) * edge_weight(d), dist, d, h - drop_, acc);
    }

    // La banda de un lado entre dos puntos de los abanicos (el final libre, solo si lo es en los
    // dos).
    static BandEdge mix(const BandEdge& a, const BandEdge& b, double u) {
        const auto lerp = [u](double p, double q) { return p + u * (q - p); };
        BandEdge out;
        out.t_edge = lerp(a.t_edge, b.t_edge);
        out.y_edge = lerp(a.y_edge, b.y_edge);
        out.grade = lerp(a.grade, b.grade);
        out.rim_y = lerp(a.rim_y, b.rim_y);
        out.t_end = lerp(a.t_end, b.t_end);
        out.y_end = lerp(a.y_end, b.y_end);
        out.slope = lerp(a.slope, b.slope);
        out.exposed = rmath::min(a.exposed, b.exposed);
        out.extra = rmath::max(a.extra, b.extra);
        return out;
    }

    // Altura de la banda (respecto al origen) a |t| = abs_t: el plano del arcén hasta su borde,
    // la cuneta y la última tira, acotada a su final.
    static double profile(const BandEdge& e, double abs_t) {
        const double u = abs_t - e.t_edge;
        if (u <= 0.0) {
            return e.y_edge + e.grade * u;
        }
        if (u <= kStageDitchWidth) {
            return e.y_edge + e.grade * u -
                   0.5 * kStageDitchDepth *
                       (1.0 - rmath::cos(6.283185307179586 * u / kStageDitchWidth));
        }
        const double end = rmath::max(e.t_end - e.t_edge, kStageDitchWidth);
        return e.rim_y + e.slope * (rmath::min(u, end) - kStageDitchWidth);
    }

    // La menor altura de la banda en |t| ∈ [abs_t − w, abs_t + w] (y como mucho hasta su final):
    // 9 puntos y el fondo de la cuneta si cae dentro.
    static double profile_min(const BandEdge& e, double abs_t, double w) {
        const double lo = abs_t - w;
        const double hi = rmath::min(abs_t + w, e.t_end);
        double best = profile(e, lo);
        for (int k = 1; k <= 8; ++k) {
            best = rmath::min(best, profile(e, lo + (hi - lo) * static_cast<double>(k) / 8.0));
        }
        const double bottom = e.t_edge + 0.5 * kStageDitchWidth;
        if (bottom > lo && bottom < hi) {
            best = rmath::min(best, profile(e, bottom));
        }
        return best;
    }

    const std::vector<FanPoint>& points_;
    const std::vector<Fan>& fans_;
    double drop_;
    const StageCellError* error_;
};

// Pendiente del tope B_q + Φ(d_q) más allá de la banda: tan empinada que solo actúa junto al
// borde (a d₁ + D vale B_q + 400 m, por encima de cualquier terreno).
constexpr double kZoneCapSlope = 100.0;

// Combinación continua de las pasadas en cuya zona está una muestra fuera de todos los núcleos
// (cuarta respuesta del consultor, §1), sobre el terreno natural N:
//   G = mín(G_mezcla, mín_q (B_q + Φ(d_q))),
//   G_mezcla = N + Σ V_p·(G_p − N) / Σ V_p,  V_p = (1 − w_p)/d_p⁸,  G_p = B_p + w_p·(N − B_p),
// con w_p el paso suave del empalme y Φ = 0 hasta d₁ y kZoneCapSlope·(d − d₁) después. Con una
// sola pasada, G = G_p; el peso de una pasada se anula al salir de su zona (sin salto), y junto
// al borde de q, G ≤ B_q (la rejilla no asoma sobre su malla). `skip` (≥ 0) deja fuera un pie
// (el diagnóstico de zone_boundary_jump).
double combine_zone(const Acc& a, double natural, int skip) {
    double sum_v = 0.0;
    double sum_vg = 0.0;
    double cap = 0.0;
    bool any_cap = false;
    for (int k = 0; k < a.zone_count; ++k) {
        if (k == skip) {
            continue;
        }
        const ZoneFoot& f = a.zone[k];
        const double w = smoothstep((f.d - kStageEdgeBand) / kStageEdgeBlend);
        const double g = f.band + w * (natural - f.band);
        const double d = rmath::max(f.d, kMinEdgeDistance);
        const double v = (1.0 - w) * edge_weight(d);
        sum_v += v;
        sum_vg += v * (g - natural);
        const double c = f.band + kZoneCapSlope * rmath::max(0.0, f.d - kStageEdgeBand);
        cap = any_cap ? rmath::min(cap, c) : c;
        any_cap = true;
    }
    const double mixed = sum_v > 0.0 ? natural + sum_vg / sum_v : natural;
    return any_cap ? rmath::min(mixed, cap) : mixed;
}

// Altura de una muestra con colisión: la regla de su banda (junto a un final libre, la de la
// banda B; si no, bajo toda la banda) o, fuera de todas, la combinación de las pasadas en cuya
// zona está, sobre el terreno natural N.
double design_height(const Acc& a, double natural) {
    if (a.core) {
        return a.core_exposed && a.core_d >= -kStageEdgeBand ? a.core_band : a.core_h;
    }
    if (a.zone_count == 0) {
        return natural;
    }
    return combine_zone(a, natural, -1);
}

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
// paso más largo que cumple los errores de cuerda, el alabeo y el paso máximo. La cuerda en planta
// se mide `reach` más allá del borde del arcén (la banda libre del formato 2; 0 en Design).
std::vector<double> section_positions(const StageTrack& track, const StageMeshDensity& density,
                                      double reach = 0.0) {
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
        const double edge = half + kStageShoulderWidth + reach;
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

// Norma espectral de la hessiana en planta de S = y(s) + peralte(s)·t en (s, t), en el marco
// (avance, normal), con ρ = 1 − κ·t (coordenadas ortogonales con h_s = ρ, h_t = 1):
//   H_ττ = (y'' + κ'·t·S_s/ρ − ρ·κ·peralte)/ρ²,  H_τn = (peralte' + κ·S_s/ρ)/ρ,  H_nn = 0,
// con S_s = pendiente + peralte'·t (peralte lineal a trozos: sin peralte'').
double surface_hessian(const StagePose& p, double curvature_rate, double bank_rate, double t) {
    constexpr double kMinRho = 0.05;
    const double rho = rmath::max(1.0 - p.curvature * t, kMinRho);
    const double along = p.grade + bank_rate * t;
    const double a =
        (p.vertical_curvature + curvature_rate * t * along / rho - rho * p.curvature * p.bank) /
        (rho * rho);
    const double c = (bank_rate + p.curvature * along / rho) / rho;
    return 0.5 * (rmath::abs(a) + rmath::sqrt(a * a + 4.0 * c * c));
}

// --- Banda del formato 2 ---------------------------------------------------------------------

// Un final a menos de esto del centro de un arco con abanico es ese centro (m).
constexpr double kFanSnap = 0.02;
// Una raíz del eje medio a menos de esto del centro de curvatura propio es el centro: a ~1 mm
// de él, los extremos del arco ya pasan por pies perpendiculares de «otra parte» (m).
constexpr double kFocalRoot = 0.01;
// La cuerda del final de la banda no parte tramos de menos de 2 veces esto (m), y como mucho en
// tantas pasadas.
constexpr double kStageBandMinRefine = 0.05;
constexpr int kBandRefinePasses = 6;
// Sin eje medio antes del centro de curvatura (no debería pasar fuera de un arco), el rayo para
// esto antes (m).
constexpr double kFocalMargin = 0.05;
// Pies de otras partes del tramo que se miran en cada punto.
constexpr int kMaxOtherFeet = 16;
// Raíz del eje medio: iteraciones y anchura final del intervalo (m).
constexpr int kRootIterations = 200;
constexpr double kRootTolerance = 1.0e-9;
constexpr double kTwoPiBand = 6.283185307179586;

std::string fixed2(double value) {
    std::array<char, 64> buffer{};
    const std::to_chars_result r = std::to_chars(buffer.data(), buffer.data() + buffer.size(),
                                                 value, std::chars_format::fixed, 2);
    return std::string(buffer.data(), r.ptr);
}

struct BandPoint {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
};

// Índices de la sección (K = 2n + 7): F izq, cuneta izq de la cabeza (k = n) al borde (k = 1), E
// izq, R izq, eje, R der, E der, cuneta der del borde (k = 1) a la cabeza (k = n), F der.
std::size_t band_end_index(int n, int side) {
    return side < 0 ? 0 : static_cast<std::size_t>(2 * n + 6);
}
std::size_t band_ditch_index(int n, int side, int k) {
    return side < 0 ? static_cast<std::size_t>(n + 1 - k) : static_cast<std::size_t>(n + 5 + k);
}

// Índice de la primitiva de arco de curvatura `curvature` que contiene s o empieza o acaba a
// menos de 1 µm de s (las secciones de las juntas); −1 si no hay.
int arc_at(const StageTrack& track, double s, double curvature) {
    constexpr double kJoint = 1.0e-6;
    const std::vector<StagePrimitive>& prims = track.axis().primitives();
    if (prims.empty() || s < -kJoint || s > track.axis().length() + kJoint) {
        return -1;
    }
    const auto is_arc = [&](std::size_t k) {
        return prims[k].curvature0 == prims[k].curvature1 && prims[k].curvature0 != 0.0 &&
               rmath::abs(prims[k].curvature0 - curvature) <= 1.0e-12;
    };
    for (std::size_t k = 0; k < prims.size(); ++k) {
        const double s1 = k + 1 < prims.size() ? prims[k + 1].s0 : track.axis().length();
        if (s >= prims[k].s0 - kJoint && s <= s1 + kJoint && is_arc(k)) {
            return static_cast<int>(k);
        }
    }
    return -1;
}

// Una sección: su núcleo (los 5 puntos: borde del arcén izquierdo, de la calzada, eje, …) y, por
// lado (0 izquierda, 1 derecha), la distancia en planta del eje a E, la dirección del arcén, su
// pendiente lateral y la altura de la cabeza de la cuneta.
struct BandSection {
    double s = 0.0;
    double curvature = 0.0;
    BandPoint core[kStageCoreVertices];
    double edge[2] = {0.0, 0.0};
    double ux[2] = {0.0, 0.0};
    double uz[2] = {0.0, 0.0};
    double grade[2] = {0.0, 0.0};
    double rim[2] = {0.0, 0.0};

    const BandPoint& e(int lane) const { return core[lane == 0 ? 0 : 4]; }
};

BandSection band_section(const StageTrack& track, double s) {
    BandSection out;
    out.s = s;
    const StagePose p = track.pose(s);
    out.curvature = p.curvature;
    const double nx = rmath::cos(p.heading);
    const double nz = rmath::sin(p.heading);
    const double ts[kStageCoreVertices] = {-(p.half_width_left + kStageShoulderWidth),
                                           -p.half_width_left, 0.0, p.half_width_right,
                                           p.half_width_right + kStageShoulderWidth};
    for (int j = 0; j < kStageCoreVertices; ++j) {
        out.core[j] = BandPoint{p.x + ts[j] * nx, p.y + p.bank * ts[j], p.z + ts[j] * nz};
    }
    const BandPoint& axis = out.core[2];
    for (int lane = 0; lane < 2; ++lane) {
        const BandPoint& e = out.core[lane == 0 ? 0 : 4];
        const BandPoint& r = out.core[lane == 0 ? 1 : 3];
        const double dx = e.x - r.x;
        const double dz = e.z - r.z;
        const double len = rmath::sqrt(dx * dx + dz * dz);
        out.ux[lane] = dx / len;
        out.uz[lane] = dz / len;
        out.grade[lane] = (e.y - r.y) / len;
        out.edge[lane] =
            rmath::sqrt((e.x - axis.x) * (e.x - axis.x) + (e.z - axis.z) * (e.z - axis.z));
        // La cuneta vuelve al plano del arcén en su cabeza (dip(W) = 0).
        out.rim[lane] = e.y + out.grade[lane] * kStageDitchWidth;
    }
    return out;
}

// El punto k (1..n) de la cuneta de un lado.
BandPoint ditch_point(const BandSection& sec, int lane, int k, int n) {
    const BandPoint& e = sec.e(lane);
    const double u = kStageDitchWidth * static_cast<double>(k) / static_cast<double>(n);
    const double dip =
        0.5 * kStageDitchDepth * (1.0 - rmath::cos(kTwoPiBand * u / kStageDitchWidth));
    return BandPoint{e.x + u * sec.ux[lane], e.y + sec.grade[lane] * u - dip,
                     e.z + u * sec.uz[lane]};
}

// Las raíces del eje medio y el refinado: lo que cuesta de la banda (StageBandCache).
class BandSolver {
public:
    BandSolver(const StageTrack& track, const StageMeshDensity& density)
        : track_(track), density_(density), prims_(track.axis().primitives()) {}

    StageBandCache solve() {
        for (const double s : section_positions(track_, density_, kStageBandFree)) {
            secs_.push_back(band_section(track_, s));
        }
        rebuild_arrays();
        ends_.resize(secs_.size() * 2);
        for (std::size_t i = 0; i < secs_.size(); ++i) {
            for (int lane = 0; lane < 2; ++lane) {
                ends_[i * 2 + static_cast<std::size_t>(lane)] = solve_side(secs_[i], lane);
            }
        }
        refine();
        return finish();
    }

private:
    struct Foot {
        double s = 0.0;
        int lane = 0;
    };
    struct SideEnd {
        StageBandSide band;
        BandPoint end;
        BandPoint medial;
        int apex = -1;
        bool bad = false;
    };
    struct Apex {
        int arc = -1;
        int side = 0;
        BandPoint at;
    };

    void rebuild_arrays() {
        s_.resize(secs_.size());
        for (int lane = 0; lane < 2; ++lane) {
            edge_[lane].resize(secs_.size());
            rim_[lane].resize(secs_.size());
        }
        for (std::size_t i = 0; i < secs_.size(); ++i) {
            s_[i] = secs_[i].s;
            for (int lane = 0; lane < 2; ++lane) {
                edge_[lane][i] = secs_[i].edge[lane];
                rim_[lane][i] = secs_[i].rim[lane];
            }
        }
    }

    // Interpolación lineal en s de un valor por sección.
    double interpolate(const std::vector<double>& values, double s) const {
        const auto it = std::upper_bound(s_.begin(), s_.end(), s);
        if (it == s_.begin()) {
            return values.front();
        }
        if (it == s_.end()) {
            return values.back();
        }
        const auto i = static_cast<std::size_t>(it - s_.begin()) - 1;
        const double f = (s - s_[i]) / (s_[i + 1] - s_[i]);
        return values[i] + f * (values[i + 1] - values[i]);
    }

    // Distancia de (x, z) al núcleo más cercano de otra parte del tramo, y su pie.
    double other_distance(double x, double z, double s_self, Foot& foot) {
        StageProjection feet[kMaxOtherFeet];
        const int m = track_.other_feet(x, z, s_self, kStageBandSelfGap, feet, kMaxOtherFeet);
        double best = std::numeric_limits<double>::infinity();
        for (int k = 0; k < rmath::min(m, kMaxOtherFeet); ++k) {
            const int lane = feet[k].t >= 0.0 ? 1 : 0;
            const double d = rmath::abs(feet[k].t) - interpolate(edge_[lane], feet[k].s);
            if (d < best) {
                best = d;
                foot = Foot{feet[k].s, lane};
            }
        }
        if (m > kMaxOtherFeet) {
            ++feet_overflows_; // pies que no caben: el δ podría estar mal
        }
        // Más allá de un extremo del tramo, el núcleo acaba en su sección: la distancia es a ese
        // segmento (sin él, el eje medio saltaría donde el pie perpendicular de la otra pasada se
        // sale de su extremo, y la banda con él).
        for (const double s_end : {track_.s_begin(), track_.s_end()}) {
            if (!(rmath::abs(s_end - s_self) > kStageBandSelfGap)) {
                continue;
            }
            const StagePose p = track_.pose(s_end);
            const double tx = rmath::sin(p.heading);
            const double tz = -rmath::cos(p.heading);
            const double along = (x - p.x) * tx + (z - p.z) * tz;
            const bool beyond = s_end == track_.s_begin() ? along < 0.0 : along > 0.0;
            if (!beyond) {
                continue;
            }
            const double t = (x - p.x) * rmath::cos(p.heading) + (z - p.z) * rmath::sin(p.heading);
            const int lane = t >= 0.0 ? 1 : 0;
            const double lateral = rmath::max(0.0, rmath::abs(t) - interpolate(edge_[lane], s_end));
            const double d = rmath::sqrt(along * along + lateral * lateral);
            if (d < best) {
                best = d;
                foot = Foot{s_end, lane};
            }
        }
        return best;
    }

    // Altura del centro de un arco: la media de las cabezas de su lado en sus dos extremos.
    double apex_height(int arc, int side) const {
        const StagePrimitive& prim = prims_[static_cast<std::size_t>(arc)];
        const double s1 = static_cast<std::size_t>(arc) + 1 < prims_.size()
                              ? prims_[static_cast<std::size_t>(arc) + 1].s0
                              : track_.axis().length();
        const int lane = side < 0 ? 0 : 1;
        return 0.5 * (interpolate(rim_[lane], prim.s0) + interpolate(rim_[lane], s1));
    }

    // El centro del arco `arc` del lado `side`, calculado una vez.
    std::size_t apex_of(int arc, int side) {
        for (std::size_t k = 0; k < apexes_.size(); ++k) {
            if (apexes_[k].arc == arc && apexes_[k].side == side) {
                return k;
            }
        }
        const StagePrimitive& prim = prims_[static_cast<std::size_t>(arc)];
        const StagePose p = track_.pose(prim.s0);
        const double radius = 1.0 / prim.curvature0; // con signo: + centro a la derecha
        Apex a;
        a.arc = arc;
        a.side = side;
        a.at.x = p.x + radius * rmath::cos(p.heading);
        a.at.z = p.z + radius * rmath::sin(p.heading);
        a.at.y = apex_height(arc, side);
        apexes_.push_back(a);
        return apexes_.size() - 1;
    }

    // El final de la banda de un lado de una sección.
    SideEnd solve_side(const BandSection& sec, int lane) {
        const int side = lane == 0 ? -1 : 1;
        const double s = sec.s;
        SideEnd out_side;
        StageBandSide& band = out_side.band;
        band.s = s;
        band.side = side;
        band.rim_y = sec.rim[lane];
        const BandPoint& e = sec.e(lane);
        const double ux = sec.ux[lane];
        const double uz = sec.uz[lane];
        const double grade = sec.grade[lane];
        const double rim_y = sec.rim[lane];
        // Centro de curvatura propio: en el lado interior, a 1/|κ| − e del borde.
        double focal = std::numeric_limits<double>::infinity();
        if (sec.curvature * static_cast<double>(side) > 0.0) {
            focal = 1.0 / rmath::abs(sec.curvature) - sec.edge[lane];
        }
        const double search = kStageBandMeet + kStageBandRamp;
        const double limit = rmath::min(search, focal);
        const auto f = [&](double u, Foot& foot) {
            return u - other_distance(e.x + u * ux, e.z + u * uz, s, foot);
        };
        Foot foot_hi;
        double hi = limit;
        double f_hi = f(hi, foot_hi);
        bool found = false;
        if (f_hi >= 0.0) {
            Foot foot_lo;
            double lo = 0.0;
            double f_lo = f(lo, foot_lo);
            if (f_lo >= 0.0) {
                // Otro núcleo toca este borde: ni la cuneta cabe.
                out_side.bad = true;
                hi = 0.0;
                foot_hi = foot_lo;
            } else {
                // Illinois: f es no decreciente; la raíz es el primer punto con f ≥ 0.
                int last = 0;
                for (int it = 0; it < kRootIterations && hi - lo > kRootTolerance; ++it) {
                    double u = hi - f_hi * (hi - lo) / (f_hi - f_lo);
                    if (!(u > lo && u < hi)) {
                        u = 0.5 * (lo + hi);
                    }
                    Foot foot;
                    const double fu = f(u, foot);
                    if (fu >= 0.0) {
                        hi = u;
                        f_hi = fu;
                        foot_hi = foot;
                        if (last > 0) {
                            f_lo *= 0.5;
                        }
                        last = 1;
                    } else {
                        lo = u;
                        f_lo = fu;
                        if (last < 0) {
                            f_hi *= 0.5;
                        }
                        last = -1;
                    }
                }
            }
            found = true;
        }
        band.delta = found ? hi : std::numeric_limits<double>::infinity();
        band.other_s = found ? foot_hi.s : 0.0;
        out_side.medial = BandPoint{e.x + hi * ux, 0.0, e.z + hi * uz};

        const auto plane = [&](double u) { return e.y + grade * u; };
        BandPoint& end = out_side.end;
        // El rayo llega a su centro de curvatura (cerca de él, los extremos de un arco ya
        // cuentan como pies de otra parte: de ahí la tolerancia).
        const bool near_focal = focal <= search && (!found || focal - hi <= kFocalRoot);
        const int arc = near_focal ? arc_at(track_, s, sec.curvature) : -1;
        // En un arco, el centro; fuera de un arco, solo si no hay eje medio (recortado).
        const bool at_focal = near_focal && (arc >= 0 || !found);
        if (at_focal && focal < kStageDitchWidth + kStageBandMinTalud) {
            // La cuneta y un talud mínimo tienen que caber antes del centro de curvatura.
            out_side.bad = true;
        }
        if (at_focal && arc >= 0 && focal <= kStageBandMeet) {
            band.kind = StageBandKind::Fan;
            const std::size_t a = apex_of(arc, side);
            out_side.apex = static_cast<int>(a);
            end = apexes_[a].at;
            band.end_u = focal;
        } else if (at_focal && arc < 0) {
            band.kind = StageBandKind::Clamped;
            band.end_u = rmath::max(kStageDitchWidth, focal - kFocalMargin);
            end = BandPoint{e.x + band.end_u * ux, plane(band.end_u), e.z + band.end_u * uz};
        } else if (!found && !at_focal) {
            band.kind = StageBandKind::Free;
            band.end_u = kStageBandFree;
            end = BandPoint{e.x + band.end_u * ux, plane(band.end_u), e.z + band.end_u * uz};
        } else {
            // Eje medio con otra parte del tramo o, en un arco cuyo centro queda más allá de
            // kStageBandMeet, el centro (con la altura del abanico, sin llegar a él).
            const double delta = at_focal ? focal : hi;
            const double other_rim = interpolate(rim_[foot_hi.lane], foot_hi.s);
            const double mid = at_focal ? apex_height(arc, side) : 0.5 * (rim_y + other_rim);
            const double run = delta - kStageDitchWidth;
            if (run < kStageBandMinTalud) {
                out_side.bad = true;
            }
            const double line_run = rmath::max(run, kStageBandMinTalud);
            // La recta de la tira, de la cabeza (W, y_C) al eje medio (δ, y_M): F sigue en ella
            // también en el solape, así que las dos bandas coinciden allí.
            const auto line = [&](double u) {
                return rim_y + (mid - rim_y) * (u - kStageDitchWidth) / line_run;
            };
            if (delta <= kStageBandMeet) {
                band.kind = StageBandKind::Meet;
                band.end_u = rmath::min(delta + kStageBandOverlap, focal);
                end = BandPoint{e.x + band.end_u * ux, line(band.end_u), e.z + band.end_u * uz};
            } else {
                band.kind = StageBandKind::Ramp;
                const double w = smoothstep((search - delta) / kStageBandRamp);
                band.end_u = kStageBandFree + w * (kStageBandMeet - kStageBandFree);
                const double y = plane(band.end_u) + w * (line(band.end_u) - plane(band.end_u));
                end = BandPoint{e.x + band.end_u * ux, y, e.z + band.end_u * uz};
            }
        }
        band.plane_gap = end.y - plane(band.end_u);
        return out_side;
    }

    // ¿Parte la cuerda del final el tramo de a a b, con el lado del medio m?
    bool splits(const SideEnd& a, const SideEnd& b, const SideEnd& m) const {
        if ((a.band.kind == StageBandKind::Free && b.band.kind == StageBandKind::Free) ||
            (a.band.kind == StageBandKind::Fan && b.band.kind == StageBandKind::Fan)) {
            return false;
        }
        const BandPoint& p0 = a.end;
        const BandPoint& p1 = b.end;
        // Distancia en planta de m a la cuerda p0–p1 y diferencia de altura con su punto medio.
        const double cx = p1.x - p0.x;
        const double cz = p1.z - p0.z;
        const double clen = rmath::sqrt(cx * cx + cz * cz);
        const double plan = clen > 0.0
                                ? rmath::abs((m.end.x - p0.x) * cz - (m.end.z - p0.z) * cx) / clen
                                : rmath::sqrt((m.end.x - p0.x) * (m.end.x - p0.x) +
                                              (m.end.z - p0.z) * (m.end.z - p0.z));
        const double height = rmath::abs(m.end.y - 0.5 * (p0.y + p1.y));
        // En un muro, un error en planta es un error en alzado de pendiente veces más: la costura
        // con la banda de enfrente lo ve. La pendiente, aproximada por la de los extremos.
        const double steep = rmath::max(slope_of(a), slope_of(b));
        return plan > density_.chord_plan || height > density_.chord_height ||
               plan * steep > density_.chord_height;
    }

    static double slope_of(const SideEnd& e) {
        const double run = e.band.end_u - kStageDitchWidth;
        return run > 0.0 ? rmath::abs(e.end.y - e.band.rim_y) / run : 0.0;
    }

    // Secciones en los puntos medios de los tramos cuyo final se aparta de la cuerda; solo se
    // calculan los puntos nuevos (el lado del medio que se mide pasa a ser la sección nueva).
    void refine() {
        std::vector<char> check(secs_.empty() ? 0 : secs_.size() - 1, 1);
        for (int pass = 0; pass < kBandRefinePasses; ++pass) {
            struct Insert {
                std::size_t after = 0;
                BandSection sec;
                SideEnd end[2];
            };
            std::vector<Insert> inserts;
            for (std::size_t i = 0; i + 1 < secs_.size(); ++i) {
                if (check[i] == 0 || secs_[i + 1].s - secs_[i].s < 2.0 * kStageBandMinRefine) {
                    continue;
                }
                const bool free_or_fan = [&] {
                    for (int lane = 0; lane < 2; ++lane) {
                        const StageBandKind ka =
                            ends_[i * 2 + static_cast<std::size_t>(lane)].band.kind;
                        const StageBandKind kb =
                            ends_[(i + 1) * 2 + static_cast<std::size_t>(lane)].band.kind;
                        if (!((ka == StageBandKind::Free && kb == StageBandKind::Free) ||
                              (ka == StageBandKind::Fan && kb == StageBandKind::Fan))) {
                            return false;
                        }
                    }
                    return true;
                }();
                if (free_or_fan) {
                    continue;
                }
                Insert in;
                in.after = i;
                in.sec = band_section(track_, 0.5 * (secs_[i].s + secs_[i + 1].s));
                bool split = false;
                for (int lane = 0; lane < 2; ++lane) {
                    in.end[lane] = solve_side(in.sec, lane);
                    split = split || splits(ends_[i * 2 + static_cast<std::size_t>(lane)],
                                            ends_[(i + 1) * 2 + static_cast<std::size_t>(lane)],
                                            in.end[lane]);
                }
                if (split) {
                    inserts.push_back(in);
                }
            }
            refine_passes_ = inserts.empty() ? pass : pass + 1;
            if (inserts.empty()) {
                refine_pending_ = 0;
                return;
            }
            refine_pending_ = inserts.size();
            std::vector<BandSection> secs;
            std::vector<SideEnd> ends;
            std::vector<char> next_check;
            secs.reserve(secs_.size() + inserts.size());
            ends.reserve(ends_.size() + 2 * inserts.size());
            std::size_t k = 0;
            for (std::size_t i = 0; i < secs_.size(); ++i) {
                secs.push_back(secs_[i]);
                ends.push_back(ends_[i * 2]);
                ends.push_back(ends_[i * 2 + 1]);
                if (k < inserts.size() && inserts[k].after == i) {
                    next_check.push_back(1);
                    secs.push_back(inserts[k].sec);
                    ends.push_back(inserts[k].end[0]);
                    ends.push_back(inserts[k].end[1]);
                    next_check.push_back(1);
                    ++k;
                } else if (i + 1 < secs_.size()) {
                    next_check.push_back(0);
                }
            }
            secs_ = std::move(secs);
            ends_ = std::move(ends);
            check = std::move(next_check);
            rebuild_arrays();
        }
        // Tras la última pasada, lo que aún pediría sección queda en refine_pending_.
    }

    StageBandCache finish() {
        const std::size_t count = secs_.size();
        // Lados cuyo eje medio cae a menos de kFanSnap del centro de un abanico de su lado (las
        // secciones de la junta de un arco con una recta): acaban en el centro.
        for (std::size_t slot = 0; slot < count * 2; ++slot) {
            SideEnd& e = ends_[slot];
            if (e.apex >= 0 || e.band.kind == StageBandKind::Free) {
                continue;
            }
            for (std::size_t a = 0; a < apexes_.size(); ++a) {
                const double dx = e.medial.x - apexes_[a].at.x;
                const double dz = e.medial.z - apexes_[a].at.z;
                if (apexes_[a].side == e.band.side && dx * dx + dz * dz <= kFanSnap * kFanSnap) {
                    e.apex = static_cast<int>(a);
                    e.band.kind = StageBandKind::Fan;
                    e.end = apexes_[a].at;
                }
            }
        }
        StageBandCache cache;
        cache.density = density_;
        cache.feet_overflows = feet_overflows_;
        cache.refine_passes = refine_passes_;
        cache.refine_pending = refine_pending_;
        cache.sections.reserve(count);
        cache.sides.reserve(count * 2);
        cache.ends.reserve(count * 2);
        double error_s = std::numeric_limits<double>::infinity();
        for (std::size_t i = 0; i < count; ++i) {
            cache.sections.push_back(secs_[i].s);
            for (int lane = 0; lane < 2; ++lane) {
                SideEnd& e = ends_[i * 2 + static_cast<std::size_t>(lane)];
                // Pendiente de la tira, desde la cabeza.
                const BandPoint& p = secs_[i].e(lane);
                const double cx = p.x + kStageDitchWidth * secs_[i].ux[lane];
                const double cz = p.z + kStageDitchWidth * secs_[i].uz[lane];
                const double run =
                    rmath::sqrt((e.end.x - cx) * (e.end.x - cx) + (e.end.z - cz) * (e.end.z - cz));
                e.band.end_y = e.end.y;
                e.band.slope = run > 0.0 ? rmath::abs(e.end.y - e.band.rim_y) / run
                                         : std::numeric_limits<double>::infinity();
                if (e.bad) {
                    error_s = rmath::min(error_s, secs_[i].s);
                }
                cache.sides.push_back(e.band);
                cache.ends.push_back(StageBandEnd{e.end.x, e.end.y, e.end.z, e.apex});
            }
        }
        cache.error_s = error_s;
        return cache;
    }

    const StageTrack& track_;
    StageMeshDensity density_;
    const std::vector<StagePrimitive>& prims_;
    std::vector<BandSection> secs_;
    std::vector<SideEnd> ends_;
    std::vector<double> s_;
    std::vector<double> edge_[2];
    std::vector<double> rim_[2];
    std::vector<Apex> apexes_;
    std::size_t feet_overflows_ = 0;
    int refine_passes_ = 0;
    std::size_t refine_pending_ = 0;
};

// La malla del formato 2 de una banda ya resuelta: K puntos por sección (en el mundo), el índice
// del F de cada lado (el centro de un abanico es el F de su primera sección) y los triángulos,
// con la validación del criterio 5 en los de la banda (hacia arriba, área en planta > 1e-6 m² y
// ≤ 80°).
struct BandMesh {
    std::size_t per_section = 0;
    std::vector<BandSection> sections;
    std::vector<BandPoint> points;
    std::vector<std::uint32_t> end_index;
    std::vector<StageTriangle> triangles;
    std::size_t bad_triangles = 0;
    double bad_s = std::numeric_limits<double>::infinity();
};

BandMesh assemble_band(const StageTrack& track, const StageBandCache& cache) {
    BandMesh out;
    const int n = cache.density.ditch_segments;
    const auto per = static_cast<std::size_t>(2 * n + kStageCoreVertices + 2);
    out.per_section = per;
    const std::size_t count = cache.sections.size();
    out.sections.reserve(count);
    out.points.assign(count * per, BandPoint{});
    out.end_index.assign(count * 2, 0);
    std::vector<std::uint32_t> apex_index;
    for (std::size_t i = 0; i < count; ++i) {
        out.sections.push_back(band_section(track, cache.sections[i]));
        const BandSection& sec = out.sections.back();
        BandPoint* row = &out.points[i * per];
        for (int j = 0; j < kStageCoreVertices; ++j) {
            row[n + 1 + j] = sec.core[j];
        }
        for (int lane = 0; lane < 2; ++lane) {
            const int side = lane == 0 ? -1 : 1;
            for (int k = 1; k <= n; ++k) {
                row[band_ditch_index(n, side, k)] = ditch_point(sec, lane, k, n);
            }
            const StageBandEnd& e = cache.ends[i * 2 + static_cast<std::size_t>(lane)];
            const std::size_t slot = i * 2 + static_cast<std::size_t>(lane);
            const auto own = static_cast<std::uint32_t>(i * per + band_end_index(n, side));
            row[band_end_index(n, side)] = BandPoint{e.x, e.y, e.z};
            out.end_index[slot] = own;
            if (e.apex >= 0) {
                const auto a = static_cast<std::size_t>(e.apex);
                if (a >= apex_index.size()) {
                    apex_index.resize(a + 1, std::numeric_limits<std::uint32_t>::max());
                }
                if (apex_index[a] == std::numeric_limits<std::uint32_t>::max()) {
                    apex_index[a] = own;
                }
                out.end_index[slot] = apex_index[a];
            }
        }
    }
    // Por cada tramo entre dos secciones, las K − 1 tiras de izquierda a derecha, dos triángulos
    // por tira con la normal hacia arriba; el que tiene dos vértices iguales (el abanico) no se
    // emite.
    const double cos80 = 1.0 / rmath::sqrt(1.0 + kStageWallMaxSlope * kStageWallMaxSlope);
    out.triangles.reserve((count - 1) * 2 * (per - 1));
    for (std::size_t i = 0; i + 1 < count; ++i) {
        const Surface road = track.pose(0.5 * (cache.sections[i] + cache.sections[i + 1])).surface;
        const auto base = static_cast<std::uint32_t>(i * per);
        const auto next = static_cast<std::uint32_t>(base + per);
        // El F de cada lado, por su índice (el centro de un abanico es uno solo).
        const auto vertex = [&](std::uint32_t section_base, std::size_t section, std::uint32_t k) {
            if (k == 0) {
                return out.end_index[section * 2];
            }
            if (k + 1 == per) {
                return out.end_index[section * 2 + 1];
            }
            return section_base + k;
        };
        for (std::uint32_t j = 0; j + 1 < per; ++j) {
            const auto strip = static_cast<int>(j);
            StageLook look = StageLook::Ditch;
            Surface surface = Surface::Dirt;
            if (strip == 0 || strip == 2 * n + 5) {
                look = StageLook::Slope; // o muro, por la pendiente de cada triángulo
            } else if (strip == n + 1 || strip == n + 4) {
                look = StageLook::Shoulder;
                surface = Surface::Sand;
            } else if (strip == n + 2 || strip == n + 3) {
                look = StageLook::Road;
                surface = road;
            }
            const std::uint32_t va = vertex(base, i, j);
            const std::uint32_t vb = vertex(base, i, j + 1);
            const std::uint32_t vc = vertex(next, i + 1, j);
            const std::uint32_t vd = vertex(next, i + 1, j + 1);
            for (const std::array<std::uint32_t, 3>& t :
                 {std::array<std::uint32_t, 3>{va, vb, vc},
                  std::array<std::uint32_t, 3>{vb, vd, vc}}) {
                if (t[0] == t[1] || t[1] == t[2] || t[0] == t[2]) {
                    continue; // el abanico del centro de un arco
                }
                if (look == StageLook::Ditch || look == StageLook::Slope) {
                    const BandPoint& a = out.points[t[0]];
                    const BandPoint& b = out.points[t[1]];
                    const BandPoint& c = out.points[t[2]];
                    const double ux = b.x - a.x;
                    const double uy = b.y - a.y;
                    const double uz = b.z - a.z;
                    const double wx = c.x - a.x;
                    const double wy = c.y - a.y;
                    const double wz = c.z - a.z;
                    const double nx = uy * wz - uz * wy;
                    const double ny = uz * wx - ux * wz;
                    const double nz = ux * wy - uy * wx;
                    const double norm = rmath::sqrt(nx * nx + ny * ny + nz * nz);
                    if (!(ny > 0.0) || 0.5 * ny <= 1.0e-6 || ny < cos80 * norm) {
                        out.bad_s = rmath::min(out.bad_s, cache.sections[i]);
                        ++out.bad_triangles;
                    }
                }
                out.triangles.push_back(
                    StageTriangle{t[0], t[1], t[2], surface, static_cast<std::uint32_t>(i), look});
            }
        }
    }
    return out;
}

// La banda resuelta para `density` y el error que da (el del eje medio o el de los triángulos).
std::shared_ptr<StageBandCache> solve_band(const StageTrack& track,
                                           const StageMeshDensity& density) {
    BandSolver solver(track, density);
    auto cache = std::make_shared<StageBandCache>(solver.solve());
    const BandMesh mesh = assemble_band(track, *cache);
    cache->bad_triangles = mesh.bad_triangles;
    cache->error_s = rmath::min(cache->error_s, mesh.bad_s);
    if (cache->error_s < std::numeric_limits<double>::infinity()) {
        cache->error = "s = " + fixed2(cache->error_s) + ": no cabe el talud entre pasadas";
    }
    return cache;
}

bool same_density(const StageMeshDensity& a, const StageMeshDensity& b) {
    return a.max_step == b.max_step && a.chord_plan == b.chord_plan &&
           a.chord_height == b.chord_height && a.ditch_segments == b.ditch_segments;
}

} // namespace

std::string stage_band_attach(StageTrack& track) {
    std::shared_ptr<StageBandCache> cache = solve_band(track, StageMeshDensity{});
    std::string error = cache->error;
    track.band_cache_ = std::move(cache);
    return error;
}

std::string stage_band_error(const StageTrack& track) {
    if (const StageBandCache* cache = track.band_cache()) {
        return cache->error;
    }
    return solve_band(track, StageMeshDensity{})->error;
}

StageCellError::StageCellError(const StageTrack& track, double offset) {
    s0_ = track.s_begin();
    const double s1 = track.s_end();
    const auto count =
        static_cast<std::size_t>(rmath::ceil((s1 - s0_) / kStageCellErrorStep)) + std::size_t{1};
    raw_.assign(count, 0.0);
    // ½·M·R², con R² = (kStageCell/√2)².
    const double half_r2 = 0.25 * kStageCell * kStageCell;
    constexpr double kProbe = 0.01;
    const auto side_edge = [offset](const StagePose& p, int side) {
        return (side > 0 ? p.half_width_right : p.half_width_left) + kStageShoulderWidth + offset;
    };
    for (std::size_t i = 0; i < count; ++i) {
        const double s = rmath::min(s0_ + static_cast<double>(i) * kStageCellErrorStep, s1);
        const StagePose p = track.pose(s);
        const double lo = rmath::max(s - kProbe, s0_);
        const double hi = rmath::min(s + kProbe, s1);
        const StagePose pl = track.pose(lo);
        const StagePose ph = track.pose(hi);
        const double bank_rate = (ph.bank - pl.bank) / (hi - lo);
        const double curvature_rate = (ph.curvature - pl.curvature) / (hi - lo);
        double m = 0.0;
        for (const int side : {-1, 1}) {
            const double edge = side_edge(p, side);
            // La banda del lado del núcleo, de edge − d₁ a edge.
            for (int k = 0; k <= 4; ++k) {
                const double t =
                    static_cast<double>(side) * (edge - kStageEdgeBand + 0.25 * k * kStageEdgeBand);
                // En el interior de una curva cerrada no hay final libre cerca del centro de
                // curvatura (la banda acaba en el eje medio o en el abanico): la hessiana, que
                // allí crece sin límite, no cuenta.
                if (1.0 - p.curvature * t < kMinFreeRho) {
                    continue;
                }
                m = rmath::max(m, surface_hessian(p, curvature_rate, bank_rate, t));
            }
        }
        raw_[i] = half_r2 * m;
    }
    // Juntas donde salta ∂S/∂s (pendiente o rampa del peralte): J·d₁/4 en la muestra más cercana.
    constexpr double kSide = 0.05;
    constexpr double kJoint = 1.0e-6;
    for (const double b : track.axis().breaks()) {
        if (b - kSide <= s0_ || b + kSide >= s1) {
            continue;
        }
        const StagePose before = track.pose(b - kJoint);
        const StagePose after = track.pose(b + kJoint);
        const double bank_before = (before.bank - track.pose(b - kSide).bank) / (kSide - kJoint);
        const double bank_after = (track.pose(b + kSide).bank - after.bank) / (kSide - kJoint);
        const double jump_grade = after.grade - before.grade;
        const double jump_bank = bank_after - bank_before;
        double jump = rmath::abs(jump_grade);
        for (const int side : {-1, 1}) {
            const double t = static_cast<double>(side) * side_edge(after, side);
            jump = rmath::max(jump, rmath::abs(jump_grade + jump_bank * t));
        }
        const auto index =
            static_cast<std::size_t>(rmath::floor((b - s0_) / kStageCellErrorStep + 0.5));
        raw_[index < count ? index : count - 1] += 0.25 * kStageEdgeBand * jump;
    }
    // Máximo en meseta con rampa: continua en s y ≥ la cota en ±kStageCellErrorPlateau.
    e_.assign(count, 0.0);
    const auto reach = static_cast<std::ptrdiff_t>(
        rmath::ceil((kStageCellErrorPlateau + kStageCellErrorRamp) / kStageCellErrorStep));
    for (std::size_t i = 0; i < count; ++i) {
        double best = 0.0;
        const auto ii = static_cast<std::ptrdiff_t>(i);
        const std::ptrdiff_t j0 = ii - reach < 0 ? 0 : ii - reach;
        const auto last = static_cast<std::ptrdiff_t>(count) - 1;
        const std::ptrdiff_t j1 = ii + reach > last ? last : ii + reach;
        for (std::ptrdiff_t j = j0; j <= j1; ++j) {
            const double gap = rmath::abs(static_cast<double>(j - ii)) * kStageCellErrorStep;
            const double w = rmath::clamp((kStageCellErrorPlateau + kStageCellErrorRamp - gap) /
                                              kStageCellErrorRamp,
                                          0.0, 1.0);
            best = rmath::max(best, w * raw_[static_cast<std::size_t>(j)]);
        }
        e_[i] = best;
    }
}

double StageCellError::at(double s) const {
    if (e_.empty()) {
        return 0.0;
    }
    const double f = rmath::max(0.0, (s - s0_) / kStageCellErrorStep);
    const auto i = static_cast<std::size_t>(rmath::floor(f));
    if (i + 1 >= e_.size()) {
        return e_.back();
    }
    const double u = f - static_cast<double>(i);
    return e_[i] + u * (e_[i + 1] - e_[i]);
}

double StageCellError::raw_at(double s) const {
    if (raw_.empty()) {
        return 0.0;
    }
    const double f = rmath::max(0.0, (s - s0_) / kStageCellErrorStep + 0.5);
    const auto i = static_cast<std::size_t>(rmath::floor(f));
    return raw_[i < raw_.size() ? i : raw_.size() - 1];
}

namespace {

// Salto de la combinación de pasadas cuando un pie a menos de kNearBoundary del final de su
// zona (d → d₁ + D) sale de ella: |G con él − G sin él| (diagnóstico; con la combinación
// continua tiende a 0 con kNearBoundary).
double zone_boundary_jump(const Acc& a, double natural) {
    constexpr double kNearBoundary = 0.01;
    const double all = combine_zone(a, natural, -1);
    double jump = 0.0;
    for (int f = 0; f < a.zone_count; ++f) {
        if (a.zone[f].d < kStageEdgeBand + kStageEdgeBlend - kNearBoundary) {
            continue;
        }
        jump = rmath::max(jump, rmath::abs(all - combine_zone(a, natural, f)));
    }
    return jump;
}

} // namespace

StageGround StageGround::build(const StageTrack& track, double cell, StageGroundKind kind,
                               const StageMeshDensity& density) {
    RALLY_CHECK(cell > 0.0, "StageGround::build: celda no válida");
    (void)kind; // un solo generador de suelo
    StageGround g;
    g.cell_ = cell;
    // La banda: la de StageTrack::build si la densidad es esa; si no, se resuelve aquí.
    std::shared_ptr<StageBandCache> own;
    const StageBandCache* cache = track.band_cache();
    if (cache == nullptr || !same_density(cache->density, density)) {
        own = solve_band(track, density);
        cache = own.get();
    }
    const BandMesh mesh = assemble_band(track, *cache);
    const std::vector<double>& sections = cache->sections;
    g.band_error_ = cache->error;
    g.band_bad_triangles_ = mesh.bad_triangles;
    g.band_refine_passes_ = cache->refine_passes;
    g.band_refine_pending_ = cache->refine_pending;
    g.band_feet_overflows_ = cache->feet_overflows;
    g.band_sides_ = cache->sides;
    g.sections_ = sections;
    g.vertices_per_section_ = mesh.per_section;

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

    // Malla: núcleo y banda.
    g.vertices_.reserve(mesh.points.size());
    for (const BandPoint& p : mesh.points) {
        g.vertices_.push_back(StageVertex{static_cast<float>(p.x - origin.x),
                                          static_cast<float>(p.y - origin.y),
                                          static_cast<float>(p.z - origin.z)});
    }
    g.triangles_ = mesh.triangles;
    // El aspecto de muro, con los vértices tal como quedan (float respecto al origen): lo que ven
    // el dibujo y los tests (en 30 km, el redondeo mueve la pendiente en la 5.ª cifra). La cuneta
    // con más pendiente que la tierra también es muro (sube en espiral en las horquillas).
    for (StageTriangle& t : g.triangles_) {
        if (t.look != StageLook::Ditch && t.look != StageLook::Slope) {
            continue;
        }
        const StageVertex& a = g.vertices_[t.a];
        const StageVertex& b = g.vertices_[t.b];
        const StageVertex& c = g.vertices_[t.c];
        const double ux = static_cast<double>(b.x) - a.x;
        const double uy = static_cast<double>(b.y) - a.y;
        const double uz = static_cast<double>(b.z) - a.z;
        const double wx = static_cast<double>(c.x) - a.x;
        const double wy = static_cast<double>(c.y) - a.y;
        const double wz = static_cast<double>(c.z) - a.z;
        const double nx = uy * wz - uz * wy;
        const double ny = uz * wx - ux * wz;
        const double nz = ux * wy - uy * wx;
        if (rmath::sqrt(nx * nx + nz * nz) > kStageDirtSlope * rmath::abs(ny)) {
            t.look = StageLook::Wall;
        }
    }

    // La banda de cada lado de cada sección para la rejilla, respecto al origen.
    const std::size_t count = sections.size();
    std::vector<BandEdge> edges(count * 2);
    for (std::size_t i = 0; i < count; ++i) {
        const BandSection& sec = mesh.sections[i];
        for (int lane = 0; lane < 2; ++lane) {
            const std::size_t slot = i * 2 + static_cast<std::size_t>(lane);
            const StageBandSide& side = cache->sides[slot];
            const StageBandEnd& end = cache->ends[slot];
            BandEdge& e = edges[slot];
            e.t_edge = sec.edge[lane];
            e.y_edge = sec.e(lane).y - origin.y;
            e.grade = sec.grade[lane];
            e.rim_y = sec.rim[lane] - origin.y;
            e.t_end = sec.edge[lane] + side.end_u;
            e.y_end = end.y - origin.y;
            const double run = side.end_u - kStageDitchWidth;
            e.slope = run > 1.0e-9 ? (end.y - sec.rim[lane]) / run : 0.0;
            e.exposed = side.kind == StageBandKind::Free || side.kind == StageBandKind::Ramp ||
                                side.kind == StageBandKind::Clamped
                            ? 1.0
                            : 0.0;
        }
    }
    // Cota de la rejilla en los finales libres que no son el plano del arcén (rampas y
    // recortados): ½·M·R², con M de la segunda diferencia de la altura del final a lo largo del
    // final (en planta) y de la diferencia de su pendiente lateral, con pasos de una celda (los
    // vértices del final, a centímetros en las rampas del exterior de las horquillas, no cuentan su
    // quiebro de cuerda como curvatura), y el máximo en ±(meseta + rampa) de s.
    {
        const double half_r2 = 0.25 * kStageCell * kStageCell;
        std::vector<double> raw(count * 2, 0.0);
        const auto plan = [&](std::size_t a, std::size_t b) {
            const StageBandEnd& p = cache->ends[a];
            const StageBandEnd& q = cache->ends[b];
            return rmath::sqrt((q.x - p.x) * (q.x - p.x) + (q.z - p.z) * (q.z - p.z));
        };
        for (std::size_t i = 1; i + 1 < count; ++i) {
            for (int lane = 0; lane < 2; ++lane) {
                const std::size_t slot = i * 2 + static_cast<std::size_t>(lane);
                const StageBandKind kind_here = cache->sides[slot].kind;
                if (kind_here != StageBandKind::Ramp && kind_here != StageBandKind::Clamped) {
                    continue;
                }
                // Las secciones a una celda en planta por cada lado (o los extremos).
                std::size_t lo = i;
                while (lo > 0 &&
                       plan((lo - 1) * 2 + static_cast<std::size_t>(lane), slot) < kStageCell) {
                    --lo;
                }
                lo = lo > 0 ? lo - 1 : 0;
                std::size_t hi = i;
                while (hi + 1 < count &&
                       plan(slot, (hi + 1) * 2 + static_cast<std::size_t>(lane)) < kStageCell) {
                    ++hi;
                }
                hi = hi + 1 < count ? hi + 1 : hi;
                const std::size_t s0 = lo * 2 + static_cast<std::size_t>(lane);
                const std::size_t s2 = hi * 2 + static_cast<std::size_t>(lane);
                const double h0 = plan(s0, slot);
                const double h1 = plan(slot, s2);
                if (!(h0 > 1.0e-6) || !(h1 > 1.0e-6)) {
                    continue;
                }
                const StageBandEnd& p0 = cache->ends[s0];
                const StageBandEnd& p1 = cache->ends[slot];
                const StageBandEnd& p2 = cache->ends[s2];
                const double second =
                    rmath::abs((p2.y - p1.y) / h1 - (p1.y - p0.y) / h0) / (0.5 * (h0 + h1));
                const double cross = rmath::abs(edges[s2].slope - edges[s0].slope) / (h0 + h1);
                raw[slot] = half_r2 * (second + 2.0 * cross);
            }
        }
        constexpr double kReach = kStageCellErrorPlateau + kStageCellErrorRamp;
        for (std::size_t i = 0; i < count; ++i) {
            for (int lane = 0; lane < 2; ++lane) {
                double best = 0.0;
                for (std::size_t j = i; j-- > 0 && sections[i] - sections[j] <= kReach;) {
                    best = rmath::max(best, raw[j * 2 + static_cast<std::size_t>(lane)]);
                }
                for (std::size_t j = i; j < count && sections[j] - sections[i] <= kReach; ++j) {
                    best = rmath::max(best, raw[j * 2 + static_cast<std::size_t>(lane)]);
                }
                edges[i * 2 + static_cast<std::size_t>(lane)].extra = best;
            }
        }
    }

    // Puntos de los abanicos: cada tramo entre secciones en partes de ≤ kStageFanStep, con la
    // banda interpolada entre sus dos secciones.
    std::vector<FanPoint> points;
    std::vector<Fan> fans;
    const auto with_band = [&](FanPoint p, std::size_t i, double f) {
        for (int lane = 0; lane < 2; ++lane) {
            const BandEdge& a = edges[i * 2 + static_cast<std::size_t>(lane)];
            const BandEdge& b =
                edges[rmath::min(i + 1, count - 1) * 2 + static_cast<std::size_t>(lane)];
            const auto lerp = [f](double x, double y) { return x + f * (y - x); };
            BandEdge& e = p.band[lane];
            e.t_edge = lerp(a.t_edge, b.t_edge);
            e.y_edge = lerp(a.y_edge, b.y_edge);
            e.grade = lerp(a.grade, b.grade);
            e.rim_y = lerp(a.rim_y, b.rim_y);
            e.t_end = lerp(a.t_end, b.t_end);
            e.y_end = lerp(a.y_end, b.y_end);
            e.slope = lerp(a.slope, b.slope);
            e.exposed = f <= 0.0 ? a.exposed : rmath::min(a.exposed, b.exposed);
            e.extra = rmath::max(a.extra, b.extra);
        }
        return p;
    };
    for (std::size_t i = 0; i + 1 < count; ++i) {
        const double a = sections[i];
        const double span = sections[i + 1] - a;
        const auto n = static_cast<int>(rmath::max(1.0, rmath::ceil(span / kStageFanStep)));
        for (int k = 0; k < n; ++k) {
            const double f = static_cast<double>(k) / static_cast<double>(n);
            points.push_back(with_band(fan_point(track, a + span * f, origin), i, f));
        }
    }
    points.push_back(with_band(fan_point(track, sections.back(), origin), count - 1, 0.0));
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
    const StageCellError cell_error(track);
    const Rasterizer raster(points, fans, kStageTerrainDrop, &cell_error);
    std::vector<std::pair<std::int64_t, std::uint32_t>> entries;
    const auto fan_count = static_cast<std::uint32_t>(fans.size() + 2);
    for (std::uint32_t f = 0; f < fan_count; ++f) {
        double x0 = 0.0;
        double x1 = 0.0;
        double z0 = 0.0;
        double z1 = 0.0;
        raster.bounds(f, x0, x1, z0, z1);
        const auto i0 = static_cast<std::int64_t>(rmath::floor(x0 / cell)) - 1;
        const auto i1 = static_cast<std::int64_t>(rmath::ceil(x1 / cell)) + 1;
        const auto k0 = static_cast<std::int64_t>(rmath::floor(z0 / cell)) - 1;
        const auto k1 = static_cast<std::int64_t>(rmath::ceil(z1 / cell)) + 1;
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
    std::vector<Acc> acc(static_cast<std::size_t>(n * n));
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
        for (Acc& a : acc) {
            a.reset();
        }
        for (std::size_t k = e; k < end; ++k) {
            raster.add(entries[k].second, grid, acc);
        }
        StageTile tile;
        tile.tx = tx;
        tile.tz = tz;
        tile.heights.resize(static_cast<std::size_t>(n * n));
        bool any = false;
        for (std::size_t k = 0; k < tile.heights.size(); ++k) {
            const Acc& a = acc[k];
            if (!(a.min_dist < kStageStripCollision)) {
                tile.heights[k] = kStageNoCollision;
                continue;
            }
            // En el centro de curvatura de un arco, todos sus abanicos tienen un pie a la misma
            // distancia: el desborde se cuenta aparte (abajo, con la altura) y no cambia nada.
            const bool overflow = a.overflow || a.zone_overflow;
            const bool at_center = overflow && a.center_dist < kStageArcCenterTolerance;
            g.candidate_overflows_ += overflow && !at_center && !a.core ? 1 : 0;
            double h = 0.0;
            if (!a.core && !blend(a, h)) {
                // Todos los pesos son 0 (el centro de un arco): el pie más cercano.
                h = a.nearest_h;
                ++g.nearest_fallbacks_;
            }
            if (!a.core) {
                const double jump = zone_boundary_jump(a, h);
                if (jump > g.zone_jump_) {
                    g.zone_jump_ = jump;
                    g.zone_jump_x_ = origin.x + grid.base_x + static_cast<double>(k % n) * cell;
                    g.zone_jump_z_ = origin.z + grid.base_z + static_cast<double>(k / n) * cell;
                }
            }
            h = design_height(a, h);
            tile.heights[k] = static_cast<float>(h);
            any = true;
            if (at_center && !a.core) {
                StageCenterOverflow c;
                c.x = origin.x + grid.base_x + static_cast<double>(k % n) * cell;
                c.z = origin.z + grid.base_z + static_cast<double>(k / n) * cell;
                c.distance = a.center_dist;
                c.height = h + origin.y;
                c.foot_min = a.foot_min + origin.y;
                c.foot_max = a.foot_max + origin.y;
                g.center_overflows_.push_back(c);
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
        hash.add_u32(t.section);
        hash.add_byte(static_cast<std::uint8_t>(t.look));
    }
    hash.add_u32(static_cast<std::uint32_t>(tiles_.size()));
    for (const StageTile& t : tiles_) {
        hash.add_u32(static_cast<std::uint32_t>(t.tx));
        hash.add_u32(static_cast<std::uint32_t>(t.tz));
        for (const float h : t.heights) {
            add_f32(h);
        }
    }
    return hash.value();
}

} // namespace rally::sim
