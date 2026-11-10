#pragma once

// La banda de entorno del formato 2 (tarea formato-2, lote 6): las cifras de las premisas P1, P2,
// P3 y P6 sobre el StageGround del juego (la malla con la banda), y los tramos de
// test que las miden. Lo usan stage_measure.cpp (a mano, «f2») y los tests de la banda.

#include "sim/jolt_runtime.h"
#include "sim/pcg32.h"
#include "sim/sim_world.h"
#include "sim/stage_ground.h"
#include "sim/stage_track.h"
#include "stage_driver.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace rally::tests {

// Con true, los informes escriben el detalle de los peores puntos (diagnóstico a mano).
inline bool g_band_debug = false;

// Tramo de test de una planta con `width` de semiancho a los dos lados, llano o con `grade` de
// pendiente uniforme, sin peralte y de grava.
inline std::string simple_stage(const std::string& plan, double length, double width = 3.0,
                                double grade = 0.0) {
    return "format = 1\nname = test\nstart_x = 0\nstart_z = 0\nstart_heading_deg = 0\n"
           "start_height = 0\n[plan]\n" +
           plan + "[profile]\npvi 0 0 0\npvi " + stage_number(length) + " " +
           stage_number(grade * length) + " 0\n[bank]\nbank 0 0\n[width]\nwidth 0 " +
           stage_number(width) + " " + stage_number(width) + "\n[surface]\nsurface 0 gravel\n";
}

// Peralte del 10 % en dos curvas de R 60 y pendiente del 5 % (test_stage_ground.cpp).
inline std::string max_bank_stage() {
    return "format = 1\nname = peralte 10 %\nstart_x = 0\nstart_z = 0\nstart_heading_deg = 0\n"
           "start_height = 0\n[plan]\nstraight 60\nspiral 20 inf 60\narc 60 60\nspiral 20 60 inf\n"
           "straight 60\nspiral 20 inf -60\narc 60 -60\nspiral 20 -60 inf\nstraight 60\n"
           "[profile]\npvi 0 0 0\npvi 380 19 0\n[bank]\nbank 0 0\nbank 60 0\nbank 80 -10\n"
           "bank 140 -10\nbank 160 0\nbank 220 0\nbank 240 10\nbank 300 10\nbank 320 0\n"
           "[width]\nwidth 0 3 3\n[surface]\nsurface 0 gravel\n";
}

// Horquilla de R 6 con brazos de 100 y 60 m y semiancho 2,5 (2 m entre cabezas de cuneta) y
// pendiente `grade`; el brazo de vuelta se aparta con un cuarto de vuelta de R 30, así que ningún
// extremo del tramo queda enfrente de otra pasada. Al 10 % el muro pasa de 80° entre los brazos
// (no carga); al 3 %, cabe.
inline std::string stacked_stage(double grade) {
    constexpr double kPi = 3.14159265358979323846;
    const double arc = kPi * 6.0;
    const double quarter = 0.5 * kPi * 30.0;
    return simple_stage("straight 100\narc " + stage_number(arc) + " 6\nstraight 60\narc " +
                            stage_number(quarter) + " -30\n",
                        160.0 + arc + quarter, 2.5, grade);
}

// Una curva de 86° de R 6 con semiancho 3 (test_stage_track: «R 6 con núcleo 4 sí vale»).
inline std::string tight_corner_stage() {
    return simple_stage("straight 50\narc 9 6\nstraight 50\n", 109.0);
}

struct BandGeometry {
    std::size_t triangles = 0;
    std::size_t band_triangles = 0; // cuneta, talud y muro
    int inverted = 0;               // normal con y ≤ 0
    int zero_area = 0;              // área en planta ≤ 1e-6 m²
    double min_area = 1e9;          // la menor área en planta (m²)
    double steepest = 0.0;          // la mayor pendiente de una cara (grados)
    double steepest_s = 0.0;
    int over_80 = 0;
    int slope_over_material = 0; // caras de cuneta o talud con más pendiente que la tierra
    int walls = 0;
    int fan_sides = 0;
    int meet_sides = 0;
    int ramp_sides = 0;
    int free_sides = 0;
    int clamped_sides = 0;
    double quant_step = 0.0; // paso de la cuantización de Jolt: caja / (2²¹ − 1), el mayor eje
    double box_x = 0.0, box_y = 0.0, box_z = 0.0;
    // P1: el lado con la mayor pendiente de la tira de la cabeza al final.
    sim::StageBandSide worst;
};

inline BandGeometry band_geometry(const sim::StageGround& g) {
    BandGeometry out;
    const std::vector<sim::StageVertex>& v = g.vertices();
    out.triangles = g.triangles().size();
    for (const sim::StageTriangle& t : g.triangles()) {
        const sim::StageVertex& a = v[t.a];
        const sim::StageVertex& b = v[t.b];
        const sim::StageVertex& c = v[t.c];
        const double ux = static_cast<double>(b.x) - a.x;
        const double uy = static_cast<double>(b.y) - a.y;
        const double uz = static_cast<double>(b.z) - a.z;
        const double wx = static_cast<double>(c.x) - a.x;
        const double wy = static_cast<double>(c.y) - a.y;
        const double wz = static_cast<double>(c.z) - a.z;
        const double nx = uy * wz - uz * wy;
        const double ny = uz * wx - ux * wz;
        const double nz = ux * wy - uy * wx;
        const double plan_area = 0.5 * std::abs(ny);
        out.inverted += ny <= 0.0 ? 1 : 0;
        out.zero_area += plan_area <= 1e-6 ? 1 : 0;
        out.min_area = std::min(out.min_area, plan_area);
        const double angle = std::atan2(std::hypot(nx, nz), ny) * 180.0 / 3.14159265358979323846;
        if (angle > out.steepest) {
            out.steepest = angle;
            out.steepest_s = g.sections().empty() ? 0.0 : g.sections()[t.section];
        }
        out.over_80 += angle > 80.0 + 1e-9 ? 1 : 0;
        const bool band = t.look == sim::StageLook::Ditch || t.look == sim::StageLook::Slope ||
                          t.look == sim::StageLook::Wall;
        out.band_triangles += band ? 1 : 0;
        out.walls += t.look == sim::StageLook::Wall ? 1 : 0;
        if ((t.look == sim::StageLook::Ditch || t.look == sim::StageLook::Slope) &&
            std::hypot(nx, nz) / ny > sim::kStageDirtSlope * (1.0 + 1e-5)) {
            ++out.slope_over_material;
        }
    }
    float x0 = 1e30f, x1 = -1e30f, y0 = 1e30f, y1 = -1e30f, z0 = 1e30f, z1 = -1e30f;
    for (const sim::StageVertex& p : v) {
        x0 = std::min(x0, p.x);
        x1 = std::max(x1, p.x);
        y0 = std::min(y0, p.y);
        y1 = std::max(y1, p.y);
        z0 = std::min(z0, p.z);
        z1 = std::max(z1, p.z);
    }
    out.box_x = static_cast<double>(x1) - x0;
    out.box_y = static_cast<double>(y1) - y0;
    out.box_z = static_cast<double>(z1) - z0;
    out.quant_step = std::max({out.box_x, out.box_y, out.box_z}) / 2097151.0;
    for (const sim::StageBandSide& side : g.band_sides()) {
        switch (side.kind) {
        case sim::StageBandKind::Fan:
            ++out.fan_sides;
            break;
        case sim::StageBandKind::Meet:
            ++out.meet_sides;
            break;
        case sim::StageBandKind::Ramp:
            ++out.ramp_sides;
            break;
        case sim::StageBandKind::Free:
            ++out.free_sides;
            break;
        case sim::StageBandKind::Clamped:
            ++out.clamped_sides;
            break;
        }
        if (side.slope > out.worst.slope) {
            out.worst = side;
        }
    }
    return out;
}

inline const char* band_kind_name(sim::StageBandKind k) {
    switch (k) {
    case sim::StageBandKind::Free:
        return "libre";
    case sim::StageBandKind::Meet:
        return "enfrentada";
    case sim::StageBandKind::Ramp:
        return "rampa";
    case sim::StageBandKind::Fan:
        return "abanico";
    case sim::StageBandKind::Clamped:
        return "recortada";
    }
    return "?";
}

// Consulta de la malla (núcleo y banda) por celdas de 1 m en planta: las alturas de todos los
// triángulos que contienen (x, z), en el mundo.
class MeshQuery {
public:
    explicit MeshQuery(const sim::StageGround& g) : g_(g) {
        const std::vector<sim::StageVertex>& v = g.vertices();
        x0_ = 1e30;
        z0_ = 1e30;
        double x1 = -1e30;
        double z1 = -1e30;
        for (const sim::StageVertex& p : v) {
            x0_ = std::min(x0_, static_cast<double>(p.x));
            z0_ = std::min(z0_, static_cast<double>(p.z));
            x1 = std::max(x1, static_cast<double>(p.x));
            z1 = std::max(z1, static_cast<double>(p.z));
        }
        nx_ = static_cast<int>(x1 - x0_) + 2;
        nz_ = static_cast<int>(z1 - z0_) + 2;
        cells_.assign(static_cast<std::size_t>(nx_) * static_cast<std::size_t>(nz_), {});
        for (std::uint32_t t = 0; t < g.triangles().size(); ++t) {
            const sim::StageTriangle& tri = g.triangles()[t];
            const double ax = std::min({v[tri.a].x, v[tri.b].x, v[tri.c].x});
            const double bx = std::max({v[tri.a].x, v[tri.b].x, v[tri.c].x});
            const double az = std::min({v[tri.a].z, v[tri.b].z, v[tri.c].z});
            const double bz = std::max({v[tri.a].z, v[tri.b].z, v[tri.c].z});
            for (int k = static_cast<int>(az - z0_); k <= static_cast<int>(bz - z0_); ++k) {
                for (int i = static_cast<int>(ax - x0_); i <= static_cast<int>(bx - x0_); ++i) {
                    cells_[static_cast<std::size_t>(k) * nx_ + i].push_back(t);
                }
            }
        }
    }
    // Las alturas (mundo) de los triángulos que contienen (x, z) en planta (y, si se pide, sus
    // índices).
    void heights(double x, double z, std::vector<double>& out,
                 std::vector<std::uint32_t>* ids = nullptr) const {
        out.clear();
        if (ids != nullptr) {
            ids->clear();
        }
        const double lx = x - g_.origin_x();
        const double lz = z - g_.origin_z();
        const int i = static_cast<int>(lx - x0_);
        const int k = static_cast<int>(lz - z0_);
        if (lx < x0_ || lz < z0_ || i >= nx_ || k >= nz_) {
            return;
        }
        const std::vector<sim::StageVertex>& v = g_.vertices();
        for (const std::uint32_t t : cells_[static_cast<std::size_t>(k) * nx_ + i]) {
            const sim::StageTriangle& tri = g_.triangles()[t];
            const sim::StageVertex& a = v[tri.a];
            const sim::StageVertex& b = v[tri.b];
            const sim::StageVertex& c = v[tri.c];
            const double d = (static_cast<double>(b.z) - c.z) * (static_cast<double>(a.x) - c.x) +
                             (static_cast<double>(c.x) - b.x) * (static_cast<double>(a.z) - c.z);
            if (d == 0.0) {
                continue;
            }
            const double w0 = ((static_cast<double>(b.z) - c.z) * (lx - c.x) +
                               (static_cast<double>(c.x) - b.x) * (lz - c.z)) /
                              d;
            const double w1 = ((static_cast<double>(c.z) - a.z) * (lx - c.x) +
                               (static_cast<double>(a.x) - c.x) * (lz - c.z)) /
                              d;
            const double w2 = 1.0 - w0 - w1;
            if (w0 < -1e-9 || w1 < -1e-9 || w2 < -1e-9) {
                continue;
            }
            out.push_back(w0 * a.y + w1 * b.y + w2 * c.y + g_.origin_y());
            if (ids != nullptr) {
                ids->push_back(t);
            }
        }
    }

private:
    const sim::StageGround& g_;
    double x0_ = 0.0;
    double z0_ = 0.0;
    int nx_ = 0;
    int nz_ = 0;
    std::vector<std::vector<std::uint32_t>> cells_;
};

// Criterio 3: rayos verticales en `points` puntos al azar de los núcleos (calzada y arcén): en
// cuántos una cara de la banda (cuneta, talud o muro) queda por encima de la cara más alta del
// núcleo, y lo más que sobresale (≤ 0 si ninguna).
inline int core_rays_wrong(const sim::StageGround& g, const sim::StageTrack& track, int points,
                           double& worst_above) {
    const MeshQuery query(g);
    std::vector<double> h;
    std::vector<std::uint32_t> ids;
    sim::Pcg32 rng(0xf2'2026'1110ull, 7);
    int wrong = 0;
    worst_above = -1e9;
    for (int i = 0; i < points; ++i) {
        const double s = track.s_begin() + (track.s_end() - track.s_begin()) *
                                               (static_cast<double>(rng.next_u32()) / 4294967296.0);
        const sim::StagePose p = track.pose(s);
        const double left = p.half_width_left + sim::kStageShoulderWidth;
        const double right = p.half_width_right + sim::kStageShoulderWidth;
        const double t =
            -left + (left + right) * (static_cast<double>(rng.next_u32()) / 4294967296.0);
        query.heights(p.x + t * std::cos(p.heading), p.z + t * std::sin(p.heading), h, &ids);
        double core = -1e30;
        double band = -1e30;
        for (std::size_t k = 0; k < ids.size(); ++k) {
            const sim::StageLook look = g.triangles()[ids[k]].look;
            const bool is_core = look == sim::StageLook::Road || look == sim::StageLook::Shoulder;
            (is_core ? core : band) = std::max(is_core ? core : band, h[k]);
        }
        if (core < -1e29) {
            continue; // en la junta de dos triángulos del núcleo a 1e-9: no cuenta
        }
        if (band > -1e29) {
            worst_above = std::max(worst_above, band - core);
        }
        wrong += band > core ? 1 : 0;
    }
    return wrong;
}

// Criterio 10 (adelanto, cambio 1 de la revisión del plan): el escalón de la costura banda con
// banda, la mayor diferencia de altura entre los triángulos que cubren el eje medio de cada lado
// enfrentado (las dos bandas se solapan 1 cm), y dónde.
inline double seam_step(const sim::StageGround& g, const sim::StageTrack& track, double& where_s) {
    const MeshQuery query(g);
    std::vector<double> h;
    double worst = 0.0;
    where_s = 0.0;
    for (const sim::StageBandSide& side : g.band_sides()) {
        if (side.kind != sim::StageBandKind::Meet) {
            continue;
        }
        const sim::StagePose p = track.pose(side.s);
        const double edge =
            (side.side > 0 ? p.half_width_right : p.half_width_left) + sim::kStageShoulderWidth;
        const double t = static_cast<double>(side.side) * (edge + side.delta);
        query.heights(p.x + t * std::cos(p.heading), p.z + t * std::sin(p.heading), h);
        if (h.size() < 2) {
            continue;
        }
        const double step =
            *std::max_element(h.begin(), h.end()) - *std::min_element(h.begin(), h.end());
        if (step > worst) {
            worst = step;
            where_s = side.s;
            if (g_band_debug) {
                std::vector<std::uint32_t> ids;
                query.heights(p.x + t * std::cos(p.heading), p.z + t * std::sin(p.heading), h,
                              &ids);
                std::printf("    costura en s %.3f lado %+d:", side.s, side.side);
                for (std::size_t k = 0; k < ids.size(); ++k) {
                    const sim::StageTriangle& tri = g.triangles()[ids[k]];
                    std::printf(" [sec %u col %zu/%zu/%zu aspecto %d y %.4f]", tri.section,
                                tri.a % g.vertices_per_section(), tri.b % g.vertices_per_section(),
                                tri.c % g.vertices_per_section(), static_cast<int>(tri.look), h[k]);
                }
                std::printf("\n");
            }
        }
    }
    return worst;
}

// Respuesta del consultor a la parte A, §2.1 (diagnóstico): la mayor pendiente de la rejilla a la
// vista (ni la muestra ni la vecina bajo la malla) entre muestras vecinas con colisión, por la
// distancia de la muestra al borde del núcleo más cercano: (5, 10], (10, 15], (15, 30] y (30, 60]
// m.
inline void grid_slopes(const std::string& text, const sim::StageTrack& track) {
    const sim::StageGround g = sim::StageGround::build(track);
    const MeshQuery query(g);
    std::vector<double> covered;
    constexpr int n = sim::kStageTileSamples;
    constexpr double kLimits[] = {5.0, 10.0, 15.0, 30.0, 60.0};
    double worst[4] = {0.0, 0.0, 0.0, 0.0};
    double where_x[4] = {};
    double where_z[4] = {};
    for (const sim::StageTile& tile : g.tiles()) {
        for (int k = 0; k < n; ++k) {
            for (int i = 0; i < n; ++i) {
                const float hv = tile.heights[static_cast<std::size_t>(k * n + i)];
                if (hv == sim::kStageNoCollision) {
                    continue;
                }
                const double x = g.origin_x() +
                                 static_cast<double>(tile.tx * sim::kStageTileCells + i) * g.cell();
                const double z = g.origin_z() +
                                 static_cast<double>(tile.tz * sim::kStageTileCells + k) * g.cell();
                // Solo la rejilla a la vista: ni la muestra ni la vecina bajo la malla.
                const auto visible = [&](double vx, double vz) {
                    query.heights(vx, vz, covered);
                    return covered.empty();
                };
                if (!visible(x, z)) {
                    continue;
                }
                double slope = 0.0;
                if (i + 1 < n && visible(x + g.cell(), z)) {
                    const float r = tile.heights[static_cast<std::size_t>(k * n + i + 1)];
                    if (r != sim::kStageNoCollision) {
                        slope = std::max(slope, std::abs(static_cast<double>(r - hv)) / g.cell());
                    }
                }
                if (k + 1 < n && visible(x, z + g.cell())) {
                    const float d = tile.heights[static_cast<std::size_t>((k + 1) * n + i)];
                    if (d != sim::kStageNoCollision) {
                        slope = std::max(slope, std::abs(static_cast<double>(d - hv)) / g.cell());
                    }
                }
                const sim::StageProjection p = track.acquire(x, z);
                const double d = p.distance - track.core_half_width(p.s, p.t >= 0.0 ? 1 : -1);
                for (int b = 0; b < 4; ++b) {
                    if (d > kLimits[b] && d <= kLimits[b + 1] && slope > worst[b]) {
                        if (g_band_debug && slope > 5.0) {
                            std::printf(
                                "    pendiente %.2f en (%.2f, %.2f) h %.3f (s %.2f t %.2f)\n",
                                slope, x, z, hv + g.origin_y(), p.s, p.t);
                        }
                        worst[b] = slope;
                        where_x[b] = x;
                        where_z[b] = z;
                    }
                }
            }
        }
    }
    (void)text;
    std::printf("  rejilla del formato 1, la mayor pendiente entre vecinas por distancia al borde "
                "del núcleo más cercano:\n");
    for (int b = 0; b < 4; ++b) {
        const sim::StageProjection p = track.acquire(where_x[b], where_z[b]);
        std::printf("    (%.0f, %.0f] m: %.2f (%.1f°) en (%.1f, %.1f), s = %.1f\n", kLimits[b],
                    kLimits[b + 1], worst[b], std::atan(worst[b]) * 180.0 / 3.14159265358979323846,
                    where_x[b], where_z[b], p.s);
    }
}

inline void band_report_stage(const char* name, const std::string& text, bool table) {
    sim::StageTrack track;
    const sim::StageParseResult parsed = sim::StageTrack::build(text, track);
    if (!parsed.ok) {
        std::printf("%s: el tramo no vale: %s\n", name, parsed.error.c_str());
        return;
    }
    const sim::StageGround g = sim::StageGround::build(track);
    const BandGeometry b = band_geometry(g);
    std::printf("\n%s: %zu secciones de %zu vértices, %zu triángulos (%zu de la banda)\n", name,
                g.sections().size(), g.vertices_per_section(), b.triangles, b.band_triangles);
    std::printf("  validación: %s; %zu triángulos de la banda que no pasan; %zu puntos con pies de "
                "más; %d pasadas de cuerda del final (%zu pendientes)\n",
                g.band_error().empty() ? "cabe" : g.band_error().c_str(), g.band_bad_triangles(),
                g.band_feet_overflows(), g.band_refine_passes(), g.band_refine_pending());
    std::printf("  lados: %d libres, %d enfrentados, %d en rampa, %d en abanico, %d recortados\n",
                b.free_sides, b.meet_sides, b.ramp_sides, b.fan_sides, b.clamped_sides);
    std::printf("  P1: la tira más empinada, %.3f (%.2f°) en s = %.2f, lado %+d, %s, eje medio a "
                "%.3f m del borde, pie de enfrente en s = %.2f, cabeza %.3f, final %.3f; muro: %d "
                "triángulos\n",
                b.worst.slope, std::atan(b.worst.slope) * 180.0 / 3.14159265358979323846, b.worst.s,
                b.worst.side, band_kind_name(b.worst.kind), b.worst.delta, b.worst.other_s,
                b.worst.rim_y, b.worst.end_y, b.walls);
    std::printf("  P2: %d invertidos, %d de área en planta ≤ 1e-6 m² (la menor, %.3g m²)\n",
                b.inverted, b.zero_area, b.min_area);
    std::printf("  P3: la cara más empinada, %.2f° (s = %.2f); %d de más de 80°; %d de cuneta o "
                "talud con más pendiente que la tierra\n",
                b.steepest, b.steepest_s, b.over_80, b.slope_over_material);
    // Los triángulos que no pasan, los 6 primeros.
    int shown = 0;
    const std::vector<sim::StageVertex>& v = g.vertices();
    for (const sim::StageTriangle& t : g.triangles()) {
        const sim::StageVertex& a = v[t.a];
        const sim::StageVertex& bb = v[t.b];
        const sim::StageVertex& c = v[t.c];
        const double ux = static_cast<double>(bb.x) - a.x;
        const double uy = static_cast<double>(bb.y) - a.y;
        const double uz = static_cast<double>(bb.z) - a.z;
        const double wx = static_cast<double>(c.x) - a.x;
        const double wy = static_cast<double>(c.y) - a.y;
        const double wz = static_cast<double>(c.z) - a.z;
        const double ny = uz * wx - ux * wz;
        const double nx = uy * wz - uz * wy;
        const double nz = ux * wy - uy * wx;
        const double angle = std::atan2(std::hypot(nx, nz), ny) * 180.0 / 3.14159265358979323846;
        if ((ny <= 0.0 || 0.5 * ny <= 1e-6 || angle > 80.0) && shown < 6) {
            ++shown;
            std::printf("    mal: sección %u (s = %.3f), aspecto %d, vértices %u %u %u (columnas "
                        "%zu %zu %zu), %.2f°, área %.3g\n",
                        t.section, g.sections()[t.section], static_cast<int>(t.look), t.a, t.b, t.c,
                        t.a % g.vertices_per_section(), t.b % g.vertices_per_section(),
                        t.c % g.vertices_per_section(), angle, 0.5 * ny);
        }
    }
    double gap = 0.0;
    double gap_s = 0.0;
    for (const sim::StageBandSide& side : g.band_sides()) {
        if ((side.kind == sim::StageBandKind::Ramp || side.kind == sim::StageBandKind::Clamped) &&
            std::abs(side.plane_gap) > std::abs(gap)) {
            gap = side.plane_gap;
            gap_s = side.s;
        }
    }
    std::printf(
        "  rampa y recortados: el final más lejos del plano del arcén, %.3f m en s = %.2f\n", gap,
        gap_s);
    double seam_s = 0.0;
    const double seam = seam_step(g, track, seam_s);
    std::printf("  costura banda con banda (eje medio): escalón máximo %.2f mm en s = %.2f\n",
                1000.0 * seam, seam_s);
    std::printf("  P6: caja de la malla %.1f × %.1f × %.1f m: paso de cuantización %.3f mm\n",
                b.box_x, b.box_y, b.box_z, 1000.0 * b.quant_step);
    if (g.band_error().empty()) {
        double above = 0.0;
        const int wrong = core_rays_wrong(g, track, 10000, above);
        std::printf("  P3: 10 000 rayos en los núcleos: %d con una cara de la banda encima; la que "
                    "más sobresale, %.3f "
                    "mm\n",
                    wrong, 1000.0 * above);
    }
    if (!table) {
        return;
    }
    grid_slopes(text, track);
    // La tabla de P1: los lados enfrentados, en rampa o en abanico, cada ~5 m de s por lado.
    std::printf("  P1, lados con pasada enfrentada (cada ~5 m):\n");
    std::printf("    %8s | %4s | %10s | %7s | %8s | %8s | %7s | %7s\n", "s", "lado", "tipo",
                "eje m.", "s enfr.", "desnivel", "pend.", "grados");
    double last_s[2] = {-1e9, -1e9};
    for (const sim::StageBandSide& side : g.band_sides()) {
        if (side.kind == sim::StageBandKind::Free) {
            continue;
        }
        const int lane = side.side < 0 ? 0 : 1;
        if (side.s - last_s[lane] < 5.0 && side.slope < b.worst.slope) {
            continue;
        }
        last_s[lane] = side.s;
        std::printf("    %8.2f | %+4d | %10s | %7.3f | %8.2f | %8.3f | %7.3f | %7.2f\n", side.s,
                    side.side, band_kind_name(side.kind), side.delta, side.other_s,
                    2.0 * (side.end_y - side.rim_y), side.slope,
                    std::atan(side.slope) * 180.0 / 3.14159265358979323846);
    }
}

// Aristas de la malla entre dos triángulos (premisa PA de la respuesta del consultor a la parte
// B): el ángulo entre sus normales, por el aspecto de los dos triángulos. Jolt deja activa una
// arista convexa si las caras forman más de `active_deg` (MeshShapeSettings: 5° por defecto); una
// cóncava, nunca (MeshShape::sFindActiveEdges).
struct EdgeAngles {
    // Categorías: 0 núcleo–núcleo, 1 núcleo–cuneta (E), 2 cuneta–cuneta, 3 cuneta–talud/muro
    // (cabeza), 4 talud/muro–talud/muro.
    double max_deg[5] = {0, 0, 0, 0, 0};
    int active[5] = {0, 0, 0, 0, 0};
    int edges[5] = {0, 0, 0, 0, 0};
    // Solo las aristas laterales (entre tiras vecinas de la misma sección): el ángulo de la cuneta.
    double ditch_lateral_max = 0.0;
    // La mayor convexa cuneta–cuneta, su sección y cuántas pasan de ctive_deg por sección.
    double ditch_convex_max = 0.0;
    double ditch_convex_s = 0.0;
    int ditch_convex_sections = 0;
};

inline const char* edge_category_name(int c) {
    static const char* names[5] = {"núcleo–núcleo", "núcleo–cuneta (E)", "cuneta–cuneta",
                                   "cuneta–talud/muro (cabeza)", "talud/muro–talud/muro"};
    return names[c];
}

inline EdgeAngles edge_angles(const sim::StageGround& g, double active_deg) {
    EdgeAngles out;
    const std::vector<sim::StageVertex>& v = g.vertices();
    const std::vector<sim::StageTriangle>& tris = g.triangles();
    std::vector<std::array<double, 3>> normals(tris.size());
    for (std::size_t i = 0; i < tris.size(); ++i) {
        const sim::StageVertex& a = v[tris[i].a];
        const sim::StageVertex& b = v[tris[i].b];
        const sim::StageVertex& c = v[tris[i].c];
        const double ux = static_cast<double>(b.x) - a.x;
        const double uy = static_cast<double>(b.y) - a.y;
        const double uz = static_cast<double>(b.z) - a.z;
        const double wx = static_cast<double>(c.x) - a.x;
        const double wy = static_cast<double>(c.y) - a.y;
        const double wz = static_cast<double>(c.z) - a.z;
        const double nx = uy * wz - uz * wy;
        const double ny = uz * wx - ux * wz;
        const double nz = ux * wy - uy * wx;
        const double len = std::sqrt(nx * nx + ny * ny + nz * nz);
        normals[i] = {nx / len, ny / len, nz / len};
    }
    // (vértice menor, vértice mayor, triángulo), ordenado: las dos caras de cada arista seguidas.
    struct Entry {
        std::uint32_t lo, hi, tri;
    };
    std::vector<Entry> entries;
    entries.reserve(tris.size() * 3);
    for (std::uint32_t i = 0; i < tris.size(); ++i) {
        const std::uint32_t ids[3] = {tris[i].a, tris[i].b, tris[i].c};
        for (int k = 0; k < 3; ++k) {
            const std::uint32_t p = ids[k];
            const std::uint32_t q = ids[(k + 1) % 3];
            entries.push_back({std::min(p, q), std::max(p, q), i});
        }
    }
    std::sort(entries.begin(), entries.end(), [](const Entry& a, const Entry& b) {
        return a.lo != b.lo ? a.lo < b.lo : (a.hi != b.hi ? a.hi < b.hi : a.tri < b.tri);
    });
    const auto group = [](sim::StageLook l) {
        return l == sim::StageLook::Road || l == sim::StageLook::Shoulder ? 0
               : l == sim::StageLook::Ditch                               ? 1
                                                                          : 2;
    };
    const std::size_t per = g.vertices_per_section();
    const double cos_active = std::cos(active_deg * 3.14159265358979323846 / 180.0);
    for (std::size_t k = 0; k + 1 < entries.size(); ++k) {
        if (entries[k].lo != entries[k + 1].lo || entries[k].hi != entries[k + 1].hi) {
            continue;
        }
        const std::uint32_t t0 = entries[k].tri;
        const std::uint32_t t1 = entries[k + 1].tri;
        const auto& n0 = normals[t0];
        const auto& n1 = normals[t1];
        const double dot = std::clamp(n0[0] * n1[0] + n0[1] * n1[1] + n0[2] * n1[2], -1.0, 1.0);
        // Convexa si el vértice de t1 fuera de la arista queda bajo el plano de t0.
        const std::uint32_t ids1[3] = {tris[t1].a, tris[t1].b, tris[t1].c};
        std::uint32_t far = ids1[0];
        for (const std::uint32_t id : ids1) {
            if (id != entries[k].lo && id != entries[k].hi) {
                far = id;
            }
        }
        const sim::StageVertex& o = v[tris[t0].a];
        const double below = n0[0] * (static_cast<double>(v[far].x) - o.x) +
                             n0[1] * (static_cast<double>(v[far].y) - o.y) +
                             n0[2] * (static_cast<double>(v[far].z) - o.z);
        const bool convex = below < 0.0;
        const double deg = std::acos(dot) * 180.0 / 3.14159265358979323846;
        const int g0 = group(tris[t0].look);
        const int g1 = group(tris[t1].look);
        int cat = 0;
        if (g0 == 0 && g1 == 0) {
            cat = 0;
        } else if ((g0 == 0) != (g1 == 0)) {
            cat = 1;
        } else if (g0 == 1 && g1 == 1) {
            cat = 2;
        } else if (g0 == 2 && g1 == 2) {
            cat = 4;
        } else {
            cat = 3;
        }
        ++out.edges[cat];
        out.active[cat] += convex && dot < cos_active ? 1 : 0;
        out.max_deg[cat] = std::max(out.max_deg[cat], deg);
        // Arista lateral: sus dos vértices en la misma sección.
        if (cat == 2 && convex && deg > out.ditch_convex_max) {
            out.ditch_convex_max = deg;
            out.ditch_convex_s = g.sections()[tris[t0].section];
        }
        if (cat == 2 && entries[k].lo / per == entries[k].hi / per) {
            out.ditch_lateral_max = std::max(out.ditch_lateral_max, deg);
        }
    }
    return out;
}

inline void edge_report(const std::string& text, double active_deg) {
    sim::StageTrack track;
    sim::StageTrack::build(text, track);
    for (const int segments : {18, 36, 38, 40, 44, 48, 72}) {
        sim::StageMeshDensity density;
        density.ditch_segments = segments;
        const sim::StageGround g =
            sim::StageGround::build(track, sim::kStageCell, sim::StageGroundKind::Design, density);
        const EdgeAngles e = edge_angles(g, active_deg);
        std::printf(
            "cuneta de %d tramos: %zu triángulos; ángulo lateral máximo en la cuneta %.2f°; "
            "convexa máxima %.2f° (s %.3f)\n",
            segments, g.triangles().size(), e.ditch_lateral_max, e.ditch_convex_max,
            e.ditch_convex_s);
        for (int c = 0; c < 5; ++c) {
            std::printf("  %-28s %7d aristas, máx. %6.2f°, activas (convexas > %.0f°) %6d\n",
                        edge_category_name(c), e.edges[c], e.max_deg[c], active_deg, e.active[c]);
        }
    }
}

// Invariante 7b (respuesta del consultor a la parada de la parte B, §2): el escalón de la malla a
// la rejilla de Jolt (cuantizada: un mundo con el terreno solo) 2 cm dentro del final de cada lado
// expuesto (libre, rampa o recortada) de cada sección: toda la costura, no una muestra.
struct SeamStep {
    double s = 0.0;
    int side = 0;
    sim::StageBandKind kind = sim::StageBandKind::Free;
    double step = 0.0;      // malla − rejilla (m)
    double from_edge = 0.0; // del borde de la calzada al final (m)
    double end_u = 0.0;
    double slope = 0.0;     // la tira de la cabeza al final
    double plane_gap = 0.0; // el final − el plano del arcén prolongado
    double e = 0.0;         // e(s)
};

struct SeamScan {
    int sides = 0;
    SeamStep worst;
    // El menor margen de la malla sobre la rejilla de 2 cm a d₁ dentro del final (< 0: la rejilla
    // asoma) y dónde.
    double lowest = 1e9;
    double lowest_s = 0.0;
    int lowest_side = 0;
    double lowest_depth = 0.0;
    std::vector<SeamStep> over; // los de más de `limit`
};

inline SeamScan seam_scan(const std::string& text, double limit) {
    sim::StageTrack track;
    sim::StageTrack::build(text, track);
    const sim::StageGround g = sim::StageGround::build(track);
    sim::WorldDesc desc;
    desc.stage_text = text;
    sim::SimWorld mesh;
    mesh.build(desc);
    mesh.seal();
    desc.stage_variant = sim::StageGroundVariant::TerrainOnly;
    sim::SimWorld terrain;
    terrain.build(desc);
    terrain.seal();
    const sim::StageCellError error(track);
    SeamScan out;
    for (const sim::StageBandSide& b : g.band_sides()) {
        if (b.kind != sim::StageBandKind::Free && b.kind != sim::StageBandKind::Ramp &&
            b.kind != sim::StageBandKind::Clamped) {
            continue;
        }
        if (b.s <= track.s_begin() + 2.0 || b.s >= track.s_end() - 2.0) {
            continue;
        }
        const sim::StagePose p = track.pose(b.s);
        const double edge = b.side > 0 ? p.half_width_right : p.half_width_left;
        const double t =
            static_cast<double>(b.side) * (edge + sim::kStageShoulderWidth + b.end_u - 0.02);
        const double x = p.x + t * std::cos(p.heading);
        const double z = p.z + t * std::sin(p.heading);
        const sim::GroundProbe m = mesh.probe_ground(x, z);
        const sim::GroundProbe r = terrain.probe_ground(x, z);
        if (!m.hit || !r.hit) {
            continue;
        }
        ++out.sides;
        SeamStep st;
        st.s = b.s;
        st.side = b.side;
        st.kind = b.kind;
        st.step = m.y - r.y;
        st.from_edge = sim::kStageShoulderWidth + b.end_u;
        st.end_u = b.end_u;
        st.slope = b.slope;
        st.plane_gap = b.plane_gap;
        st.e = error.at(b.s, b.side);
        for (const double depth : {0.02, 0.25, 0.5, 0.75, 1.0}) {
            const double td =
                static_cast<double>(b.side) * (edge + sim::kStageShoulderWidth + b.end_u - depth);
            const sim::GroundProbe md =
                mesh.probe_ground(p.x + td * std::cos(p.heading), p.z + td * std::sin(p.heading));
            const sim::GroundProbe rd = terrain.probe_ground(p.x + td * std::cos(p.heading),
                                                             p.z + td * std::sin(p.heading));
            if (md.hit && rd.hit && md.y - rd.y < out.lowest) {
                out.lowest = md.y - rd.y;
                out.lowest_s = b.s;
                out.lowest_side = b.side;
                out.lowest_depth = depth;
            }
        }
        if (st.step > out.worst.step) {
            out.worst = st;
        }
        if (st.step > limit) {
            out.over.push_back(st);
        }
    }
    return out;
}

inline void seam_report(const char* name, const std::string& text, double limit) {
    const SeamScan scan = seam_scan(text, limit);
    std::printf("%s: %d lados expuestos; escalón máximo %.1f mm (s %.3f, lado %+d, %s); %zu de "
                "más de %.0f mm\n",
                name, scan.sides, 1000.0 * scan.worst.step, scan.worst.s, scan.worst.side,
                band_kind_name(scan.worst.kind), scan.over.size(), 1000.0 * limit);
    std::printf(
        "  margen mínimo de la malla sobre la rejilla dentro del final: %.1f mm (s %.3f, lado "
        "%+d, a %.2f m del final)\n",
        1000.0 * scan.lowest, scan.lowest_s, scan.lowest_side, scan.lowest_depth);
    for (const SeamStep& st : scan.over) {
        std::printf("  s %10.3f lado %+d %-10s escalón %6.1f mm, final a %6.3f m del borde, "
                    "pendiente %6.3f, final − plano del arcén %7.1f mm, e %5.1f mm\n",
                    st.s, st.side, band_kind_name(st.kind), 1000.0 * st.step, st.from_edge,
                    st.slope, 1000.0 * st.plane_gap, 1000.0 * st.e);
    }
}

inline void band_report() {
    std::printf("== formato 2: banda de entorno (premisas P1, P2, P3 y P6) ==\n");
    band_report_stage("lazadas.stage", lazadas_stage_text(), true);
    band_report_stage("hairpin_flat.stage", stage_file("hairpin_flat.stage"), true);
    band_report_stage("curva de R 6 y 86°", tight_corner_stage(), false);
    band_report_stage("peralte 10 %", max_bank_stage(), false);
    band_report_stage("horquilla apilada al 3 %", stacked_stage(0.03), false);
    band_report_stage("horquilla apilada al 10 % (no cabe)", stacked_stage(0.10), false);
    band_report_stage("sintético 30 km", synthetic_stage_30km(), false);
}

} // namespace rally::tests
