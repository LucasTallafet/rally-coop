#include "car_worlds.h"
#include "sim/jolt_runtime.h"
#include "sim/pcg32.h"
#include "sim/sim_world.h"
#include "sim/stage_ground.h"
#include "sim/stage_track.h"
#include "sim/test_zone.h"
#include "stage_driver.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// Geometría de colisión del tramo (tarea tramo-fisica, lote 4; mensajes/lote-4.md §2.6):
// sim/stage_ground.h y el suelo del tramo en Jolt (probe_ground), sin conducir. Criterios 1, 2 y 4
// del prompt y kExpectedStageGroundHash.

using rally::sim::kStageShoulderWidth;
using rally::sim::kStageTerrainDrop;
using rally::sim::StageGround;
using rally::sim::StagePose;
using rally::sim::StageProjection;
using rally::sim::StageTrack;
using rally::sim::Surface;

namespace {

// FNV-1a de la geometría de colisión de lazadas.stage (StageGround::hash, diseño de 0,75 m).
// Cambia si cambia la malla, el terreno o lazadas.stage; se actualiza como la huella.
constexpr std::uint64_t kExpectedStageGroundHash = 0x9494'dcf5'e269'9e5aull;

constexpr double kMm = 1.0e-3;

// En Debug, versiones cortas de las pruebas que recorren todo el tramo (respuesta del consultor a
// tramo-fisica, §5): bastan para Debug = Release; las cifras son las de Release.
#ifdef NDEBUG
constexpr bool kFull = true;
#else
constexpr bool kFull = false;
#endif
// Diagonal de una celda de 0,75 m.
constexpr double kCellDiagonal = 0.75 * 1.4142135623730951;

StageTrack load(const std::string& text) {
    StageTrack track;
    const rally::sim::StageParseResult r = StageTrack::build(text, track);
    INFO(r.error);
    REQUIRE(r.ok);
    return track;
}

const StageTrack& lazadas() {
    static const StageTrack track = load(rally::tests::lazadas_stage_text());
    return track;
}

const StageGround& lazadas_ground() {
    static const StageGround ground = StageGround::build(lazadas());
    return ground;
}

const std::string& hairpin_text() {
    static const std::string text = rally::tests::stage_file("hairpin_flat.stage");
    return text;
}

// La horquilla llana con una pendiente del 5 % (la continuidad del terreno donde la curvatura
// salta necesita alturas distintas entre los brazos).
std::string hairpin_grade_text() {
    std::string text = hairpin_text();
    const std::string flat = "pvi 437.69911184307752 0 0";
    const std::size_t at = text.find(flat);
    REQUIRE(at != std::string::npos);
    text.replace(at, flat.size(), "pvi 437.69911184307752 21.884955592153876 0");
    return text;
}

double rand01(rally::sim::Pcg32& rng) { return static_cast<double>(rng.next_u32()) / 4294967296.0; }

struct Point {
    double x = 0.0;
    double z = 0.0;
};

Point at(const StageTrack& track, double s, double t) {
    const StagePose p = track.pose(s);
    return {p.x + t * std::cos(p.heading), p.z + t * std::sin(p.heading)};
}

double half_width(const StagePose& p, double t) {
    return t >= 0.0 ? p.half_width_right : p.half_width_left;
}

// Llama a f(x, z, altura en el mundo, con colisión) con cada muestra de las baldosas.
template <typename F>
void for_each_sample(const StageGround& g, F&& f) {
    for (const rally::sim::StageTile& tile : g.tiles()) {
        for (int k = 0; k < rally::sim::kStageTileSamples; ++k) {
            for (int i = 0; i < rally::sim::kStageTileSamples; ++i) {
                const double x =
                    g.origin_x() +
                    static_cast<double>(tile.tx * rally::sim::kStageTileCells + i) * g.cell();
                const double z =
                    g.origin_z() +
                    static_cast<double>(tile.tz * rally::sim::kStageTileCells + k) * g.cell();
                const float h =
                    tile.heights[static_cast<std::size_t>(k * rally::sim::kStageTileSamples + i)];
                const bool collide = h != rally::sim::kStageNoCollision;
                f(x, z, collide ? static_cast<double>(h) + g.origin_y() : 0.0, collide);
            }
        }
    }
}

// Distancia en planta de (x, z) al eje fuera de [s − gap, s + gap] (otras pasadas).
double other_pass_distance(const StageTrack& track, double x, double z, double s, double gap) {
    double best = 1e18;
    if (s - gap > track.s_begin()) {
        best = std::min(best, track.project(x, z, track.s_begin(), s - gap).distance);
    }
    if (s + gap < track.s_end()) {
        best = std::min(best, track.project(x, z, s + gap, track.s_end()).distance);
    }
    return best;
}

rally::sim::WorldDesc stage_only(const std::string& text) {
    rally::sim::WorldDesc desc;
    desc.stage_text = text;
    return desc;
}

// Altura del terreno en (x, z) con la triangulación de HeightFieldShape (diagonal de (i, k) a
// (i + 1, k + 1)), desde las muestras sin cuantizar. NaN sin colisión.
double terrain_at(const StageGround& g, double x, double z) {
    const double fx = (x - g.origin_x()) / g.cell();
    const double fz = (z - g.origin_z()) / g.cell();
    const auto i = static_cast<std::int64_t>(std::floor(fx));
    const auto k = static_cast<std::int64_t>(std::floor(fz));
    const auto floor_div = [](std::int64_t a, std::int64_t b) {
        return a >= 0 ? a / b : -((-a + b - 1) / b);
    };
    const std::int64_t tx = floor_div(i, rally::sim::kStageTileCells);
    const std::int64_t tz = floor_div(k, rally::sim::kStageTileCells);
    for (const rally::sim::StageTile& tile : g.tiles()) {
        if (tile.tx != tx || tile.tz != tz) {
            continue;
        }
        const auto li = static_cast<int>(i - tx * rally::sim::kStageTileCells);
        const auto lk = static_cast<int>(k - tz * rally::sim::kStageTileCells);
        constexpr int n = rally::sim::kStageTileSamples;
        const auto at = [&](int a, int b) {
            return static_cast<double>(
                tile.heights[static_cast<std::size_t>((lk + b) * n + li + a)]);
        };
        const double h00 = at(0, 0);
        const double h10 = at(1, 0);
        const double h01 = at(0, 1);
        const double h11 = at(1, 1);
        if (h00 >= 1e30 || h10 >= 1e30 || h01 >= 1e30 || h11 >= 1e30) {
            return std::nan("");
        }
        const double u = fx - static_cast<double>(i);
        const double v = fz - static_cast<double>(k);
        const double h = v >= u ? h00 + v * (h01 - h00) + u * (h11 - h01)
                                : h00 + u * (h10 - h00) + v * (h11 - h10);
        return h + g.origin_y();
    }
    return std::nan("");
}

// Mayor diferencia de altura entre dos muestras vecinas con colisión.
double max_neighbour_step(const StageGround& g) {
    double worst = 0.0;
    constexpr int n = rally::sim::kStageTileSamples;
    for (const rally::sim::StageTile& tile : g.tiles()) {
        for (int k = 0; k < n; ++k) {
            for (int i = 0; i < n; ++i) {
                const float h = tile.heights[static_cast<std::size_t>(k * n + i)];
                if (h == rally::sim::kStageNoCollision) {
                    continue;
                }
                if (i + 1 < n) {
                    const float r = tile.heights[static_cast<std::size_t>(k * n + i + 1)];
                    if (r != rally::sim::kStageNoCollision) {
                        worst = std::max(worst, std::abs(static_cast<double>(r - h)));
                    }
                }
                if (k + 1 < n) {
                    const float d = tile.heights[static_cast<std::size_t>((k + 1) * n + i)];
                    if (d != rally::sim::kStageNoCollision) {
                        worst = std::max(worst, std::abs(static_cast<double>(d - h)));
                    }
                }
            }
        }
    }
    return worst;
}

} // namespace

// --- Criterio 1: geometría del núcleo
// -------------------------------------------------------------

TEST_CASE("stage_ground: core vertices lie on the axis pose at the half widths with the bank") {
    const StageTrack& track = lazadas();
    const StageGround& g = lazadas_ground();
    const std::vector<double>& sections = g.sections();
    REQUIRE(g.vertices().size() == sections.size() * rally::sim::kStageSectionVertices);
    double worst = 0.0;
    for (std::size_t i = 0; i < sections.size(); ++i) {
        const StagePose p = track.pose(sections[i]);
        const double ts[5] = {-(p.half_width_left + kStageShoulderWidth), -p.half_width_left, 0.0,
                              p.half_width_right, p.half_width_right + kStageShoulderWidth};
        for (int j = 0; j < 5; ++j) {
            const rally::sim::StageVertex& v = g.vertices()[i * 5 + static_cast<std::size_t>(j)];
            const double ex = p.x + ts[j] * std::cos(p.heading);
            const double ey = p.y + p.bank * ts[j];
            const double ez = p.z + ts[j] * std::sin(p.heading);
            const double dx = g.origin_x() + v.x - ex;
            const double dy = g.origin_y() + v.y - ey;
            const double dz = g.origin_z() + v.z - ez;
            worst = std::max(worst, std::sqrt(dx * dx + dy * dy + dz * dz));
        }
    }
    MESSAGE("vértices del núcleo: el peor, a ", worst / kMm, " mm de su punto (", sections.size(),
            " secciones)");
    CHECK(worst <= kMm);

    // Una sección en cada junta, extremo de curva vertical y punto de peralte, anchura y
    // superficie, y en los extremos; paso ≤ 2 m.
    CHECK(sections.front() == track.s_begin());
    CHECK(sections.back() == track.s_end());
    for (const double b : track.axis().breaks()) {
        const auto it = std::lower_bound(sections.begin(), sections.end(), b - 1.0e-3);
        CHECK((it != sections.end() && std::abs(*it - b) <= 1.0e-3));
    }
    double longest = 0.0;
    for (std::size_t i = 0; i + 1 < sections.size(); ++i) {
        longest = std::max(longest, sections[i + 1] - sections[i]);
    }
    CHECK(longest <= rally::sim::kStageMeshMaxStep + 1e-9);
}

TEST_CASE(
    "stage_ground: the chord error at the middle of each mesh piece is within 1 cm and 2 mm") {
    const StageTrack& track = lazadas();
    const StageGround& g = lazadas_ground();
    const std::vector<double>& sections = g.sections();
    double plan = 0.0;
    double height = 0.0;
    double twist = 0.0;
    const auto vertex = [&g](std::size_t section, int j) {
        const rally::sim::StageVertex& v = g.vertices()[section * 5 + static_cast<std::size_t>(j)];
        return std::array<double, 3>{g.origin_x() + v.x, g.origin_y() + v.y, g.origin_z() + v.z};
    };
    for (std::size_t i = 0; i + 1 < sections.size(); ++i) {
        const double sm = 0.5 * (sections[i] + sections[i + 1]);
        const StagePose p = track.pose(sm);
        const double ts[5] = {-(p.half_width_left + kStageShoulderWidth), -p.half_width_left, 0.0,
                              p.half_width_right, p.half_width_right + kStageShoulderWidth};
        for (int j = 0; j < 5; ++j) {
            const auto a = vertex(i, j);
            const auto b = vertex(i + 1, j);
            // Punto de verdad de la línea j en sm, frente a la cuerda.
            const double tx = p.x + ts[j] * std::cos(p.heading);
            const double ty = p.y + p.bank * ts[j];
            const double tz = p.z + ts[j] * std::sin(p.heading);
            const double cx = b[0] - a[0];
            const double cz = b[2] - a[2];
            const double len = std::hypot(cx, cz);
            plan = std::max(plan, std::abs((tx - a[0]) * cz - (tz - a[2]) * cx) / len);
            height = std::max(height, std::abs(0.5 * (a[1] + b[1]) - ty));
        }
        // Alabeo: centro de cada cuadrilátero frente al punto medio de su diagonal.
        for (int j = 0; j < 4; ++j) {
            const auto va = vertex(i, j);
            const auto vb = vertex(i, j + 1);
            const auto vc = vertex(i + 1, j);
            const auto vd = vertex(i + 1, j + 1);
            const double tm = 0.5 * (ts[j] + ts[j + 1]);
            const double truth = p.y + p.bank * tm;
            twist = std::max(twist, std::abs(0.5 * (vb[1] + vc[1]) - truth));
            (void)va;
            (void)vd;
        }
    }
    MESSAGE("cuerda: planta ", plan / kMm, " mm, alzado ", height / kMm, " mm, alabeo ",
            twist / kMm, " mm");
    CHECK(plan <= 0.01);
    CHECK(height <= 0.002);
    CHECK(twist <= 0.002);
}

// --- Criterio 2: superficies
// -----------------------------------------------------------------------

TEST_CASE(
    "stage_ground: a vertical ray gives the road surface, sand on the shoulder and dirt out") {
    const rally::sim::JoltRuntime runtime;
    const StageTrack& track = lazadas();
    rally::sim::SimWorld world;
    world.build(stage_only(rally::tests::lazadas_stage_text()));
    world.seal();
    rally::sim::Pcg32 rng(2026'10'27, 3);
    int road = 0;
    int shoulder = 0;
    int outside = 0;
    int wrong = 0;
    int edge_points = 0;
    for (int i = 0; road < 200 || shoulder < 200 || outside < 200; ++i) {
        REQUIRE(i < 100000);
        const double s = track.s_begin() + (track.s_end() - track.s_begin()) * rand01(rng);
        const StagePose p = track.pose(s);
        const double side = rng.bounded(2) == 0 ? -1.0 : 1.0;
        const double w = half_width(p, side);
        const double e = w + kStageShoulderWidth;
        const int kind = static_cast<int>(rng.bounded(3));
        // Una de cada cuatro, a 5 cm de un borde.
        const bool edge = rng.bounded(4) == 0;
        double t = 0.0;
        Surface expected = Surface::Dirt;
        if (kind == 0) {
            if (road >= 200) {
                continue;
            }
            t = edge ? w - 0.05 : (w - 0.05) * rand01(rng);
            expected = p.surface;
            ++road;
        } else if (kind == 1) {
            if (shoulder >= 200) {
                continue;
            }
            t = edge ? (rng.bounded(2) == 0 ? w + 0.05 : e - 0.05)
                     : w + 0.05 + (kStageShoulderWidth - 0.1) * rand01(rng);
            expected = Surface::Sand;
            ++shoulder;
        } else {
            if (outside >= 200) {
                continue;
            }
            t = edge ? e + 0.05 : e + 0.05 + 10.0 * rand01(rng);
            const Point q = at(track, s, side * t);
            StageProjection other;
            if (track.on_any_road(q.x, q.z, other)) {
                continue; // en el núcleo de otra pasada
            }
            expected = Surface::Dirt;
            ++outside;
        }
        edge_points += edge ? 1 : 0;
        const Point q = at(track, s, side * t);
        const rally::sim::GroundProbe g = world.probe_ground(q.x, q.z);
        if (!g.hit || g.surface != expected) {
            ++wrong;
            if (wrong <= 5) {
                MESSAGE("s ", s, " t ", side * t, ": ", (g.hit ? static_cast<int>(g.surface) : -1),
                        " en lugar de ", static_cast<int>(expected));
            }
        }
    }
    MESSAGE("superficies: ", road, " en la calzada, ", shoulder, " en el arcén, ", outside,
            " fuera; ", edge_points, " a 5 cm de un borde");
    CHECK(wrong == 0);
}

// --- Microrrelieve del tramo (respuesta del consultor §1.1) ---

namespace {

// h en (x, z) con un marco propio (la proyección exacta del punto), como una rueda.
double relief_at(const StageTrack& track, const rally::sim::TestZone& noise, double s_hint,
                 double x, double z) {
    const rally::sim::StageReliefFrame f = rally::sim::stage_relief_frame(track, s_hint, x, z);
    return rally::sim::stage_relief_height(f, noise, f.s, f.t, x, z);
}

// La misma h sin mezclar: la banda de la superficie del sitio (lo que hacía MicroRelief::Noise).
double unblended_at(const StageTrack& track, const rally::sim::TestZone& noise, double s, double t,
                    double x, double z) {
    const StagePose p = track.pose(s);
    const double w = half_width(p, t);
    const double a = std::abs(t);
    Surface surface = Surface::Dirt;
    if (a <= w) {
        surface = p.surface;
    } else if (a <= w + kStageShoulderWidth) {
        surface = Surface::Sand;
    }
    return noise.surface_noise(surface, x, z);
}

} // namespace

TEST_CASE("stage_ground: the micro relief has no step across the road edges and surface changes") {
    const StageTrack& track = lazadas();
    const rally::sim::TestZone noise;
    rally::sim::Pcg32 rng(15, 1000);
    double blended = 0.0;
    double raw = 0.0;
    // 1000 sitios: una recta de cada lado, del eje a 1 m fuera del arcén, cada 1 cm.
    for (int i = 0; i < (kFull ? 1000 : 100); ++i) {
        const double s = rand01(rng) * track.axis().length();
        const StagePose p = track.pose(s);
        const double side = (i & 1) != 0 ? 1.0 : -1.0;
        const double end = half_width(p, side) + kStageShoulderWidth + 1.0;
        double previous = 0.0;
        double previous_raw = 0.0;
        for (int k = 0; k * 0.01 <= end; ++k) {
            const double t = side * k * 0.01;
            const Point q = at(track, s, t);
            const double h = relief_at(track, noise, s, q.x, q.z);
            const double r = unblended_at(track, noise, s, t, q.x, q.z);
            if (k > 0) {
                blended = std::max(blended, std::abs(h - previous));
                raw = std::max(raw, std::abs(r - previous_raw));
            }
            previous = h;
            previous_raw = r;
        }
    }
    // Y a lo largo de s en el cambio de grava a tierra (s = 650), en 50 rectas de la calzada.
    double along = 0.0;
    for (int i = 0; i < 50; ++i) {
        const double t = -2.5 + 5.0 * rand01(rng);
        double previous = 0.0;
        for (int k = 0; k <= 200; ++k) {
            const double s = 649.0 + 0.01 * k;
            const Point q = at(track, s, t);
            const double h = relief_at(track, noise, s, q.x, q.z);
            if (k > 0) {
                along = std::max(along, std::abs(h - previous));
            }
            previous = h;
        }
    }
    MESSAGE("mayor salto de h en 1 cm: ", blended / kMm, " mm cruzando los bordes, ", along / kMm,
            " mm en el cambio de superficie; sin mezclar, ", raw / kMm, " mm");
    CHECK(blended <= 1.5 * kMm);
    CHECK(along <= 1.5 * kMm);
    // Las rectas ven el salto que la mezcla quita.
    CHECK(raw > 1.5 * kMm);
}

// --- Criterio 4: terreno ---

TEST_CASE("stage_ground: every sample is finite and the strip has ground up to 59.4 m and none "
          "past 60.5 m") {
    for (const bool hairpin : {false, true}) {
        const StageTrack track =
            hairpin ? load(hairpin_text()) : load(rally::tests::lazadas_stage_text());
        const StageGround g = StageGround::build(track);
        int bad = 0;
        int missing = 0;
        int extra = 0;
        std::size_t collide = 0;
        double farthest = 0.0;
        double nearest_without = 1e9;
        std::size_t index = 0;
        for_each_sample(g, [&](double x, double z, double h, bool has) {
            // En Debug, una muestra de cada 17 (versión corta).
            if constexpr (!kFull) {
                if ((index++ % 17) != 0) {
                    return;
                }
            }
            const double d = track.acquire(x, z).distance;
            if (has) {
                ++collide;
                bad += std::isfinite(h) ? 0 : 1;
                extra += d >= rally::sim::kStageStripCollision ? 1 : 0;
                farthest = std::max(farthest, d);
            } else {
                missing += d <= rally::sim::kStageStripCollision - kCellDiagonal ? 1 : 0;
                nearest_without = std::min(nearest_without, d);
            }
        });
        MESSAGE(std::string(hairpin ? "hairpin_flat" : "lazadas"), ": ", g.tiles().size(),
                " baldosas, ", collide, " muestras con colisión de ", g.sample_count(),
                "; la más lejana a ", farthest, " m del eje, la más cercana sin colisión a ",
                nearest_without, " m; sin peso (pie más cercano): ", g.nearest_fallbacks(),
                "; con más pies de los que caben: ", g.candidate_overflows());
        CHECK(g.candidate_overflows() == 0);
        CHECK(bad == 0);
        CHECK(missing == 0);
        CHECK(extra == 0);
    }
}

TEST_CASE("stage_ground: a ray 59 m from the axis hits the ground and one 61 m away does not") {
    const rally::sim::JoltRuntime runtime;
    const StageTrack& track = lazadas();
    rally::sim::SimWorld world;
    world.build(stage_only(rally::tests::lazadas_stage_text()));
    world.seal();
    rally::sim::Pcg32 rng(59, 61);
    int near_checked = 0;
    int far_checked = 0;
    int wrong = 0;
    for (int i = 0; near_checked < 500 || far_checked < 500; ++i) {
        REQUIRE(i < 200000);
        const double s = track.s_begin() + (track.s_end() - track.s_begin()) * rand01(rng);
        const double side = rng.bounded(2) == 0 ? -1.0 : 1.0;
        const bool far = (i & 1) != 0;
        if ((far && far_checked >= 500) || (!far && near_checked >= 500)) {
            continue;
        }
        const double t = far ? 61.0 : 59.0;
        const Point q = at(track, s, side * t);
        // Solo donde el punto del eje más cercano es este (a 59 o 61 m).
        if (std::abs(track.acquire(q.x, q.z).distance - t) > 1e-6) {
            continue;
        }
        (far ? far_checked : near_checked)++;
        const bool hit = world.probe_ground(q.x, q.z).hit;
        wrong += hit == far ? 1 : 0;
    }
    CHECK(wrong == 0);
}

TEST_CASE(
    "stage_ground: the terrain is the drop under the core and at the edge height past one cell") {
    const StageTrack& track = lazadas();
    const StageGround& g = lazadas_ground();
    double under = 0.0;      // |H − (plano − bajada)| bajo el núcleo
    double first_cell = 0.0; // |H − borde| en la primera celda (≤ 5 cm)
    double one_cell = 0.0;   // |H − borde| de una celda a la diagonal (también en las lazadas)
    double alone = 0.0;      // |H − borde| hasta 10 m fuera, sin otra pasada a 30 m
    double gap_15 = 0.0;     // a 1,5 m del borde en el hueco de las lazadas (informe)
    int n_under = 0;
    int n_one = 0;
    int n_alone = 0;
    std::size_t index = 0;
    for_each_sample(g, [&](double x, double z, double h, bool has) {
        if (!has) {
            return;
        }
        // En Debug, una muestra de cada 17 (versión corta).
        if constexpr (!kFull) {
            if ((index++ % 17) != 0) {
                return;
            }
        }
        StageProjection p;
        if (track.on_any_road(x, z, p)) {
            const StagePose pose = track.pose(p.s);
            under = std::max(under, std::abs(h - (pose.y + pose.bank * p.t - kStageTerrainDrop)));
            ++n_under;
            return;
        }
        p = track.acquire(x, z);
        if (p.s <= track.s_begin() || p.s >= track.s_end()) {
            return;
        }
        const StagePose pose = track.pose(p.s);
        const double side = p.t >= 0.0 ? 1.0 : -1.0;
        const double e = half_width(pose, p.t) + kStageShoulderWidth;
        const double d = std::abs(p.t) - e;
        const double edge = pose.y + pose.bank * side * e;
        const double off = std::abs(h - edge);
        if (d < 0.75) {
            first_cell = std::max(first_cell, off);
        } else if (d <= kCellDiagonal) {
            one_cell = std::max(one_cell, off);
            ++n_one;
        } else if (d <= 10.0 && other_pass_distance(track, x, z, p.s, 50.0) > 30.0) {
            alone = std::max(alone, off);
            ++n_alone;
        }
        if (d > 1.45 && d < 1.55 && p.s > 700.0 && p.s < 910.0) {
            gap_15 = std::max(gap_15, off);
        }
    });
    MESSAGE("bajo el núcleo (", n_under, " muestras): ", under / kMm, " mm del plano − 5 mm");
    MESSAGE("primera celda: ", first_cell / kMm, " mm del borde; de una celda a la diagonal (",
            n_one, "): ", one_cell / kMm, " mm; solas hasta 10 m (", n_alone, "): ", alone / kMm,
            " mm; a 1,5 m en el hueco de las lazadas: ", gap_15 / kMm, " mm");
    CHECK(n_under > 1000);
    CHECK(under <= kMm);
    CHECK(first_cell <= 0.05);
    CHECK(one_cell <= kStageTerrainDrop + kMm);
    CHECK(alone <= kStageTerrainDrop + kMm);
}

TEST_CASE("stage_ground: under the core the grid stays below the mesh") {
    // Respuesta del consultor §1.2: el terreno baja 5 mm bajo el núcleo; en 10 000 puntos de
    // los núcleos de lazadas (hasta el borde exterior del arcén), la rejilla queda bajo la malla.
    const rally::sim::JoltRuntime runtime;
    const StageTrack& track = lazadas();
    const StageGround& g = lazadas_ground();
    rally::sim::SimWorld world;
    world.build(stage_only(rally::tests::lazadas_stage_text()));
    world.seal();
    rally::sim::Pcg32 rng(10'000, 5);
    double lowest = 1e9;
    double worst_s = 0.0;
    double worst_t = 0.0;
    int above = 0;
    for (int i = 0; i < (kFull ? 10'000 : 2'000); ++i) {
        const double s = track.s_begin() + (track.s_end() - track.s_begin()) * rand01(rng);
        const StagePose p = track.pose(s);
        const double left = p.half_width_left + kStageShoulderWidth;
        const double right = p.half_width_right + kStageShoulderWidth;
        const double t = -left + (left + right) * rand01(rng);
        const Point q = at(track, s, t);
        const rally::sim::GroundProbe mesh = world.probe_ground(q.x, q.z);
        const double terrain = terrain_at(g, q.x, q.z);
        REQUIRE(mesh.hit);
        REQUIRE(std::isfinite(terrain));
        const double margin = mesh.y - terrain;
        above += margin <= 0.0 ? 1 : 0;
        if (margin < lowest) {
            lowest = margin;
            worst_s = s;
            worst_t = t;
        }
    }
    MESSAGE("rejilla bajo la malla: margen mínimo ", lowest / kMm, " mm (s = ", worst_s,
            ", t = ", worst_t, "); puntos con la rejilla encima: ", above);
    CHECK(above == 0);
}

TEST_CASE("stage_ground: far from other passes the terrain is flat across at the edge height") {
    const rally::sim::JoltRuntime runtime;
    const StageTrack& track = lazadas();
    rally::sim::SimWorld world;
    world.build(stage_only(rally::tests::lazadas_stage_text()));
    world.seal();
    rally::sim::Pcg32 rng(4, 30);
    int checked = 0;
    double worst = 0.0;
    double slope = 0.0;
    for (int i = 0; checked < 200; ++i) {
        REQUIRE(i < 100000);
        const double s = rand01(rng) * track.axis().length();
        const StagePose p = track.pose(s);
        // Lado de fuera de la curva (o recta).
        double side = rng.bounded(2) == 0 ? -1.0 : 1.0;
        if (p.curvature * side > 0.0) {
            side = -side;
        }
        const double e = half_width(p, side) + kStageShoulderWidth;
        const Point a = at(track, s, side * (e + 2.0));
        const Point b = at(track, s, side * (e + 10.0));
        if (other_pass_distance(track, b.x, b.z, s, 50.0) <= 30.0 ||
            other_pass_distance(track, a.x, a.z, s, 50.0) <= 30.0 ||
            track.acquire(b.x, b.z).distance < e + 9.999) {
            continue;
        }
        ++checked;
        const double edge = p.y + p.bank * side * e - kStageTerrainDrop;
        const rally::sim::GroundProbe ga = world.probe_ground(a.x, a.z);
        const rally::sim::GroundProbe gb = world.probe_ground(b.x, b.z);
        REQUIRE(ga.hit);
        REQUIRE(gb.hit);
        worst = std::max(worst, std::max(std::abs(ga.y - edge), std::abs(gb.y - edge)));
        slope = std::max(slope, std::abs(gb.y - ga.y) / 8.0);
    }
    MESSAGE("llano lejos de otras pasadas: a ", worst / kMm,
            " mm de borde − 5 mm; pendiente "
            "lateral ",
            100.0 * slope, " %");
    CHECK(worst <= 2.0 * kMm);
    CHECK(slope <= 0.001);
}

TEST_CASE("stage_ground: between the arms of the hairpins the terrain has no crests or holes") {
    const rally::sim::JoltRuntime runtime;
    const StageTrack& track = lazadas();
    rally::sim::SimWorld world;
    world.build(stage_only(rally::tests::lazadas_stage_text()));
    world.seal();
    // Brazo central (s 770–820) frente al de entrada (antes de 730) y al de salida (tras 860).
    int lines = 0;
    int bad = 0;
    double steepest = 0.0;
    for (double s = 770.0; s <= 820.0; s += 2.5) {
        const StagePose p = track.pose(s);
        for (const bool towards_entry : {true, false}) {
            // El otro brazo, por su proyección más cercana.
            const StageProjection other = towards_entry ? track.project(p.x, p.z, 640.0, 735.0)
                                                        : track.project(p.x, p.z, 850.0, 960.0);
            const StagePose q = track.pose(other.s);
            const double side_here =
                (q.x - p.x) * std::cos(p.heading) + (q.z - p.z) * std::sin(p.heading) >= 0.0 ? 1.0
                                                                                             : -1.0;
            const double side_there =
                (p.x - q.x) * std::cos(q.heading) + (p.z - q.z) * std::sin(q.heading) >= 0.0 ? 1.0
                                                                                             : -1.0;
            const double e_here = half_width(p, side_here) + kStageShoulderWidth + 0.01;
            const double e_there = half_width(q, side_there) + kStageShoulderWidth + 0.01;
            const Point a = at(track, s, side_here * e_here);
            const Point b = at(track, other.s, side_there * e_there);
            const double ha = p.y + p.bank * side_here * (e_here - 0.01) - kStageTerrainDrop;
            const double hb = q.y + q.bank * side_there * (e_there - 0.01) - kStageTerrainDrop;
            const double lo = std::min(ha, hb) - 0.02;
            const double hi = std::max(ha, hb) + 0.02;
            const double sign = hb >= ha ? 1.0 : -1.0;
            const double length = std::hypot(b.x - a.x, b.z - a.z);
            const int steps = static_cast<int>(length / 0.05);
            double previous = 0.0;
            bool line_bad = false;
            for (int k = 0; k <= steps; ++k) {
                const double f = static_cast<double>(k) / steps;
                const rally::sim::GroundProbe g =
                    world.probe_ground(a.x + f * (b.x - a.x), a.z + f * (b.z - a.z));
                if (!g.hit || g.y < lo || g.y > hi) {
                    line_bad = true;
                }
                // Sin cambiar de sentido (± 1,5 mm de cuantización).
                if (k > 0 && sign * (g.y - previous) < -1.5 * kMm) {
                    line_bad = true;
                }
                if (k > 0) {
                    steepest = std::max(steepest, std::abs(g.y - previous) / (length / steps));
                }
                previous = g.y;
            }
            bad += line_bad ? 1 : 0;
            ++lines;
        }
    }
    MESSAGE(lines, " rectas entre brazos enfrentados; la más empinada sube ", 100.0 * steepest,
            " % en algún tramo de 5 cm");
    CHECK(lines >= 20);
    CHECK(bad == 0);
}

TEST_CASE("stage_ground: neighbouring tiles give the same height on their common border") {
    const StageGround& g = lazadas_ground();
    constexpr int n = rally::sim::kStageTileSamples;
    int pairs = 0;
    int different = 0;
    for (const rally::sim::StageTile& a : g.tiles()) {
        for (const rally::sim::StageTile& b : g.tiles()) {
            const bool right = b.tx == a.tx + 1 && b.tz == a.tz;
            const bool below = b.tz == a.tz + 1 && b.tx == a.tx;
            if (!right && !below) {
                continue;
            }
            ++pairs;
            for (int k = 0; k < n; ++k) {
                const std::size_t ia = right ? static_cast<std::size_t>(k * n + n - 1)
                                             : static_cast<std::size_t>((n - 1) * n + k);
                const std::size_t ib =
                    right ? static_cast<std::size_t>(k * n) : static_cast<std::size_t>(k);
                different += a.heights[ia] == b.heights[ib] ? 0 : 1;
            }
        }
    }
    MESSAGE(pairs, " pares de baldosas vecinas");
    CHECK(pairs > 50);
    CHECK(different == 0);
}

TEST_CASE("stage_ground: the terrain is continuous, also where the curvature jumps") {
    // Una función continua: el mayor salto entre muestras vecinas baja con la celda. Un salto de
    // verdad (un pie que muere con peso) no bajaría.
    for (const bool hairpin : {false, true}) {
        if constexpr (!kFull) {
            if (!hairpin) {
                continue; // Debug: solo la horquilla (versión corta)
            }
        }
        const StageTrack track =
            load(hairpin ? hairpin_grade_text() : rally::tests::lazadas_stage_text());
        const double coarse = max_neighbour_step(StageGround::build(track, 0.75));
        const double fine = max_neighbour_step(StageGround::build(track, 0.25));
        MESSAGE((hairpin ? "horquilla con pendiente" : "lazadas"), ": mayor salto entre vecinas ",
                coarse, " m con 0,75 m y ", fine, " m con 0,25 m");
        CHECK(fine <= 0.5 * coarse);
    }
}

TEST_CASE("stage_ground: the collision geometry hash is the same twice and the expected one") {
    const StageGround again = StageGround::build(lazadas());
    CHECK(again.hash() == lazadas_ground().hash());
    if (lazadas_ground().hash() != kExpectedStageGroundHash) {
        char text[32];
        std::snprintf(text, sizeof(text), "0x%016llx",
                      static_cast<unsigned long long>(lazadas_ground().hash()));
        MESSAGE("la geometría de colisión de lazadas.stage ha cambiado; si es intencionado, "
                "kExpectedStageGroundHash = ",
                text);
    }
    CHECK(lazadas_ground().hash() == kExpectedStageGroundHash);
}

TEST_CASE(
    "stage_ground: the three ground variants build and the world gives their hash and bytes") {
    const rally::sim::JoltRuntime runtime;
    for (const rally::sim::StageGroundVariant v :
         {rally::sim::StageGroundVariant::Design, rally::sim::StageGroundVariant::CoarseTiles,
          rally::sim::StageGroundVariant::GridOnly}) {
        rally::sim::WorldDesc desc = stage_only(rally::tests::lazadas_stage_text());
        desc.stage_variant = v;
        rally::sim::SimWorld world;
        world.build(desc);
        world.seal();
        CHECK(world.stage() != nullptr);
        CHECK(world.stage_ground_bytes() > 0);
        if (v == rally::sim::StageGroundVariant::Design) {
            CHECK(world.stage_ground_hash() == lazadas_ground().hash());
        } else {
            CHECK(world.stage_ground_hash() != lazadas_ground().hash());
        }
    }
    const StageGround grid =
        StageGround::build(lazadas(), 0.75, rally::sim::StageGroundKind::GridOnly);
    CHECK(grid.triangles().empty());
    REQUIRE(!grid.tiles().empty());
    CHECK(grid.tiles().front().surfaces.size() ==
          static_cast<std::size_t>(rally::sim::kStageTileCells * rally::sim::kStageTileCells));
    // Sin tramo, todo a cero.
    rally::sim::SimWorld empty;
    empty.build(rally::sim::WorldDesc{});
    empty.seal();
    CHECK(empty.stage() == nullptr);
    CHECK(empty.stage_run() == nullptr);
    CHECK(empty.stage_ground_hash() == 0);
    CHECK(empty.stage_ground_bytes() == 0);
}
