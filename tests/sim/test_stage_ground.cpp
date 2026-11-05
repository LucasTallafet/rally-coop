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
constexpr std::uint64_t kExpectedStageGroundHash = 0xdd43'8cd6'76e8'c121ull;

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
    // 1000 sitios: una recta de cada lado, del eje a 1 m fuera del arcén, cada 1 cm. La s de
    // referencia del marco (la de la pasada) va en s, s − 1,3 o s + 1,3 m: donde está una rueda
    // respecto al origen del coche.
    for (int i = 0; i < (kFull ? 1000 : 100); ++i) {
        const double s = rand01(rng) * track.axis().length();
        const double hint = s + 1.3 * static_cast<double>((i / 2) % 3 - 1);
        const StagePose p = track.pose(s);
        const double side = (i & 1) != 0 ? 1.0 : -1.0;
        const double end = half_width(p, side) + kStageShoulderWidth + 1.0;
        double previous = 0.0;
        double previous_raw = 0.0;
        for (int k = 0; k * 0.01 <= end; ++k) {
            const double t = side * k * 0.01;
            const Point q = at(track, s, t);
            const double h = relief_at(track, noise, hint, q.x, q.z);
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
    // Hasta la otra pasada (segunda revisión, cambio 4): rectas del brazo central de las lazadas
    // al de entrada y al de salida, y la del atajo de la horquilla llana, con la s del marco fija
    // en el brazo de salida, como la de la pasada de un coche que cruza.
    double across = 0.0;
    const auto cross = [&](const StageTrack& tr, double s0, double x0, double z0, double x1,
                           double z1) {
        const double length = std::hypot(x1 - x0, z1 - z0);
        const int steps = static_cast<int>(length / 0.01);
        double previous = 0.0;
        for (int k = 0; k <= steps; ++k) {
            const double f = static_cast<double>(k) / steps;
            const double h = relief_at(tr, noise, s0, x0 + f * (x1 - x0), z0 + f * (z1 - z0));
            if (k > 0) {
                across = std::max(across, std::abs(h - previous));
            }
            previous = h;
        }
    };
    int lines = 0;
    for (double s = 770.0; s <= 820.0; s += 2.5) {
        const StagePose p = track.pose(s);
        for (const bool towards_entry : {true, false}) {
            const StageProjection other = towards_entry ? track.project(p.x, p.z, 640.0, 735.0)
                                                        : track.project(p.x, p.z, 850.0, 960.0);
            const StagePose q = track.pose(other.s);
            cross(track, s, p.x, p.z, q.x, q.z);
            ++lines;
        }
    }
    const StageTrack hairpin = load(hairpin_text());
    const StagePose from = hairpin.pose(165.0);
    cross(hairpin, 165.0, from.x, from.z, from.x + 24.0, from.z);
    MESSAGE("mayor salto de h en 1 cm: ", blended / kMm, " mm cruzando los bordes, ", along / kMm,
            " mm en el cambio de superficie, ", across / kMm, " mm de una pasada a otra (",
            lines + 1, " rectas); sin mezclar, ", raw / kMm, " mm");
    CHECK(blended <= 1.5 * kMm);
    CHECK(along <= 1.5 * kMm);
    CHECK(across <= 1.5 * kMm);
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

    // Tarea tramo-ajustes, criterio 2: puntos fijos de la frenada de 250 m más allá de los 150 m
    // de antes (L + 155 … L + 245, cada 10 m), a ±59 m con suelo y a ±61 m sin él. Con la
    // colisión hasta L + 150, a 59 m solo habría suelo hasta L + 160,9 (√(60² − 59²) pasado el
    // final): desde L + 165 los puntos lo detectan.
    const double length = track.axis().length();
    int runoff_checked = 0;
    int runoff_wrong = 0;
    for (double s = length + 155.0; s < track.s_end(); s += 10.0) {
        for (const double side : {-1.0, 1.0}) {
            for (const double t : {59.0, 61.0}) {
                const Point q = at(track, s, side * t);
                if (std::abs(track.acquire(q.x, q.z).distance - t) > 1e-6) {
                    continue;
                }
                ++runoff_checked;
                const bool hit = world.probe_ground(q.x, q.z).hit;
                runoff_wrong += hit == (t > 60.0) ? 1 : 0;
            }
        }
    }
    MESSAGE("frenada: ", runoff_checked, " rayos de 40 medidos, ", runoff_wrong, " mal");
    CHECK(runoff_checked == 40);
    CHECK(runoff_wrong == 0);
}

namespace {

using rally::sim::kStageCoreDrop;
using rally::sim::kStageEdgeBand;
using rally::sim::kStageEdgeBlend;
using rally::sim::StageCellError;

// G de la tercera respuesta del consultor (§1) en (s, t) de una sola pasada, lejos de otras (el
// terreno natural es la altura del borde − 5 mm).
double single_pass_g(const StageTrack& track, const StageCellError& error, double s, double t) {
    const StagePose p = track.pose(s);
    const double side = t >= 0.0 ? 1.0 : -1.0;
    const double edge = half_width(p, t) + kStageShoulderWidth;
    const double d = std::abs(t) - edge;
    const double surface = p.y + p.bank * t;
    if (d < -kStageEdgeBand) {
        return surface - kStageCoreDrop;
    }
    const double band = surface - kStageTerrainDrop - error.at(s);
    if (d <= kStageEdgeBand) {
        return band;
    }
    const double natural = p.y + p.bank * side * edge - kStageTerrainDrop;
    const double u = std::clamp((d - kStageEdgeBand) / kStageEdgeBlend, 0.0, 1.0);
    return band + u * u * (3.0 - 2.0 * u) * (natural - band);
}

} // namespace

TEST_CASE("stage_ground: the terrain follows the edge band function of the distance to the edge") {
    // Tercera respuesta del consultor, §1: bajo el núcleo lejos del borde, S − 10 cm; en la banda
    // de ±d₁, S − 5 mm − e(s); de d₁ a d₁ + D, el empalme con el terreno natural; más allá, el
    // terreno natural (lejos de otras pasadas, la altura del borde − 5 mm, llano en lateral).
    const StageTrack& track = lazadas();
    const StageGround& g = lazadas_ground();
    const StageCellError error(track);
    double core = 0.0;  // |H − (S − 10 cm)| a d < −d₁
    double band = 0.0;  // |H − G| en la banda y el empalme, lejos de otras pasadas
    double alone = 0.0; // |H − borde| de d₁ + D a 10 m, lejos de otras pasadas
    double lift = 0.0;  // la mayor diferencia de G con el terreno natural (el lado alto)
    int n_core = 0;
    int n_band = 0;
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
        const bool on_road = track.on_any_road(x, z, p);
        if (!on_road) {
            p = track.acquire(x, z);
        }
        if (p.s <= track.s_begin() + 2.0 || p.s >= track.s_end() - 2.0) {
            return;
        }
        const StagePose pose = track.pose(p.s);
        const double side = p.t >= 0.0 ? 1.0 : -1.0;
        const double e = half_width(pose, p.t) + kStageShoulderWidth;
        const double d = std::abs(p.t) - e;
        if (on_road && d < -kStageEdgeBand - 0.01) {
            core = std::max(core, std::abs(h - (pose.y + pose.bank * p.t - kStageCoreDrop)));
            ++n_core;
            return;
        }
        if (d < -kStageEdgeBand + 0.01 || other_pass_distance(track, x, z, p.s, 50.0) < 30.0) {
            return;
        }
        const double natural = pose.y + pose.bank * side * e - kStageTerrainDrop;
        if (d <= kStageEdgeBand + kStageEdgeBlend) {
            const double expected = single_pass_g(track, error, p.s, p.t);
            band = std::max(band, std::abs(h - expected));
            if (d > 0.0) {
                lift = std::max(lift, std::abs(expected - natural));
            }
            ++n_band;
        } else if (d <= 10.0) {
            alone = std::max(alone, std::abs(h - natural));
            ++n_alone;
        }
    });
    MESSAGE("bajo el núcleo, a d < −d₁ (", n_core, " muestras): ", core / kMm,
            " mm de S − 10 cm; en la banda y el empalme (", n_band, "): ", band / kMm,
            " mm de G; de d₁ + D a 10 m (", n_alone, "): ", alone / kMm,
            " mm del borde − 5 mm; G se aparta del terreno natural hasta ", lift / kMm, " mm");
    // En Debug, una muestra de cada 17.
    CHECK(n_core > (kFull ? 1000 : 500));
    CHECK(core <= kMm);
    CHECK(n_band > (kFull ? 1000 : 500));
    CHECK(band <= kMm);
    CHECK(alone <= kMm);
}

namespace {

// Tramo sintético con el 10 % de peralte (el formato no pone tope; 10 % es más del doble que
// lazadas) en una curva a derechas y otra a izquierdas, con una pendiente del 5 %.
std::string max_bank_text() {
    return "format = 1\nname = peralte 10 %\nstart_x = 0\nstart_z = 0\nstart_heading_deg = 0\n"
           "start_height = 0\n[plan]\nstraight 60\nspiral 20 inf 60\narc 60 60\nspiral 20 60 inf\n"
           "straight 60\nspiral 20 inf -60\narc 60 -60\nspiral 20 -60 inf\nstraight 60\n"
           "[profile]\npvi 0 0 0\npvi 380 19 0\n[bank]\nbank 0 0\nbank 60 0\nbank 80 -10\n"
           "bank 140 -10\nbank 160 0\nbank 220 0\nbank 240 10\nbank 300 10\nbank 320 0\n"
           "[width]\nwidth 0 3 3\n[surface]\nsurface 0 gravel\n";
}

struct GridMargins {
    double lowest = 1e9; // margen mínimo de la malla sobre la rejilla (m)
    double worst_s = 0.0;
    double worst_t = 0.0;
    int above = 0;      // puntos con la rejilla encima de la malla
    int edge_cells = 0; // puntos en celdas que cruzan un borde
    int low_side = 0;   // de ellos, en el borde bajo de un tramo con peralte
    double step = 0.0;  // el mayor escalón de la malla a la rejilla en el borde (m)
    double step_s = 0.0;
    double step_side = 0.0;
    double step_e = 0.0;
    double step_over_e = -1e9; // el mayor escalón − e(s)
    double step_outside = 0.0; // el mayor fuera de las horquillas de lazadas
    double step_over_e_outside = -1e9;
    int over_15 = 0; // puntos con el escalón > 15 mm
};

// La rejilla de Jolt (cuantizada: un mundo con el terreno solo) frente a la malla de Jolt, en
// `random` puntos al azar de los núcleos y en `edge` puntos de celdas que cruzan el borde
// arcén–terreno, la mitad en el borde bajo de los tramos con peralte. Y el escalón en el borde.
GridMargins grid_margins(const std::string& text, int random, int edge, std::uint64_t seed) {
    const StageTrack track = load(text);
    const StageGround g = StageGround::build(track);
    rally::sim::SimWorld mesh;
    mesh.build(stage_only(text));
    mesh.seal();
    rally::sim::WorldDesc terrain_desc = stage_only(text);
    terrain_desc.stage_variant = rally::sim::StageGroundVariant::TerrainOnly;
    rally::sim::SimWorld terrain;
    terrain.build(terrain_desc);
    terrain.seal();
    const StageCellError error(track);
    GridMargins out;
    const auto measure = [&](double s, double t) {
        const Point q = at(track, s, t);
        const rally::sim::GroundProbe m = mesh.probe_ground(q.x, q.z);
        const rally::sim::GroundProbe r = terrain.probe_ground(q.x, q.z);
        REQUIRE(m.hit);
        REQUIRE(r.hit);
        // La malla con el rayo del mundo entero; si la rejilla asoma, el rayo da la rejilla y el
        // margen sale 0: entonces, la del plano de la sección (la malla, con ≤ 2 mm de cuerda).
        const StagePose pose = track.pose(s);
        const double margin = m.y > r.y ? m.y - r.y : pose.y + pose.bank * t - r.y;
        out.above += margin <= 0.0 ? 1 : 0;
        if (margin < out.lowest) {
            out.lowest = margin;
            out.worst_s = s;
            out.worst_t = t;
        }
    };
    // ¿Cruza el borde la celda de la rejilla que contiene (x, z)? Esquinas dentro y fuera.
    const auto in_edge_cell = [&](double x, double z) {
        const double i = std::floor((x - g.origin_x()) / g.cell());
        const double k = std::floor((z - g.origin_z()) / g.cell());
        int inside = 0;
        for (const double di : {0.0, 1.0}) {
            for (const double dk : {0.0, 1.0}) {
                StageProjection p;
                inside += track.on_any_road(g.origin_x() + (i + di) * g.cell(),
                                            g.origin_z() + (k + dk) * g.cell(), p)
                              ? 1
                              : 0;
            }
        }
        return inside > 0 && inside < 4;
    };
    rally::sim::Pcg32 rng(seed, 11);
    for (int i = 0; i < random; ++i) {
        const double s = track.s_begin() + (track.s_end() - track.s_begin()) * rand01(rng);
        const StagePose p = track.pose(s);
        const double left = p.half_width_left + kStageShoulderWidth;
        const double right = p.half_width_right + kStageShoulderWidth;
        measure(s, -left + (left + right) * rand01(rng));
    }
    int low = 0;
    int other = 0;
    for (int i = 0; low + other < edge; ++i) {
        REQUIRE(i < 100 * edge);
        const double s = rand01(rng) * track.axis().length();
        const StagePose p = track.pose(s);
        const double side = rng.bounded(2) == 0 ? -1.0 : 1.0;
        const bool is_low = p.bank * side < -0.005; // el plano baja hacia fuera
        if (is_low ? low >= edge / 2 : other >= edge - edge / 2) {
            continue;
        }
        const double e = half_width(p, side) + kStageShoulderWidth;
        const double t = side * (e - kCellDiagonal * rand01(rng));
        const Point q = at(track, s, t);
        if (!in_edge_cell(q.x, q.z)) {
            continue;
        }
        (is_low ? low : other)++;
        measure(s, t);
        // Escalón en el borde: la malla y la rejilla en el mismo punto, 1 mm dentro del borde
        // (la caída que ve la rueda al salir; a ±1 cm se sumaría la pendiente del peralte).
        const Point a = at(track, s, side * (e - 0.001));
        const double stp = mesh.probe_ground(a.x, a.z).y - terrain.probe_ground(a.x, a.z).y;
        const double over = stp - error.at(s);
        if (stp > out.step) {
            out.step = stp;
            out.step_s = s;
            out.step_side = side;
            out.step_e = error.at(s);
        }
        out.step_over_e = std::max(out.step_over_e, over);
        out.over_15 += stp > 15.0 * kMm ? 1 : 0;
        // Las horquillas de R 6 de lazadas (s 725–765 y 822–862; el sintético no llega).
        if (s < 725.0 || (s > 765.0 && s < 822.0) || s > 862.0) {
            out.step_outside = std::max(out.step_outside, stp);
            out.step_over_e_outside = std::max(out.step_over_e_outside, over);
        }
    }
    out.edge_cells = low + other;
    out.low_side = low;
    return out;
}

} // namespace

TEST_CASE("stage_ground: under the core and across the edge the grid stays below the mesh") {
    // Tercera respuesta del consultor, §2, y cuarta, §2: la rejilla de Jolt, cuantizada, nunca
    // queda encima de la malla; margen ≥ 1,5 mm en la banda; fuera de las horquillas de R 6, el
    // escalón en el borde ≤ 8 mm + e(s). En las horquillas el escalón es diagnóstico (decide el
    // impacto del criterio 4 A).
    const rally::sim::JoltRuntime runtime;
    const GridMargins lazadas_margins = grid_margins(
        rally::tests::lazadas_stage_text(), kFull ? 8'000 : 800, kFull ? 2'000 : 200, 10'000);
    const GridMargins bank_margins =
        grid_margins(max_bank_text(), kFull ? 2'000 : 200, kFull ? 2'000 : 200, 10);
    for (const GridMargins* m : {&lazadas_margins, &bank_margins}) {
        MESSAGE((m == &lazadas_margins ? std::string("lazadas") : std::string("peralte 10 %")),
                ": margen mínimo de la malla sobre la rejilla ", m->lowest / kMm,
                " mm (s = ", m->worst_s, ", t = ", m->worst_t, "); puntos con la rejilla encima ",
                m->above, "; ", m->edge_cells, " en el borde, ", m->low_side,
                " en el borde bajo; escalón en el borde hasta ", m->step / kMm,
                " mm (s = ", m->step_s, ", lado ", m->step_side, ", e = ", m->step_e / kMm,
                " mm), escalón − e hasta ", m->step_over_e / kMm, " mm, ", m->over_15,
                " puntos de más de 15 mm; fuera de las horquillas, escalón hasta ",
                m->step_outside / kMm, " mm y escalón − e hasta ", m->step_over_e_outside / kMm,
                " mm");
        CHECK(m->above == 0);
        CHECK(m->lowest >= 1.5 * kMm);
        CHECK(m->step_over_e_outside <= 8.0 * kMm);
        CHECK(m->low_side * 2 >= m->edge_cells);
    }
}

TEST_CASE("stage_ground: e bounds the interpolation error of the stage surface in a cell") {
    // Tercera respuesta del consultor, §7, premisa 1: en la parte del núcleo de la banda
    // (−d₁ ≤ d ≤ 0), la interpolación de S en la celda de la rejilla (con la triangulación de
    // HeightFieldShape y S exacta en sus esquinas) no queda más de e(s) por encima de S. En
    // lazadas, con las dos horquillas de R 6 por separado, y en el sintético del 10 %.
    for (const bool synthetic : {false, true}) {
        const StageTrack track =
            load(synthetic ? max_bank_text() : rally::tests::lazadas_stage_text());
        const StageGround g = StageGround::build(track);
        const StageCellError error(track);
        struct Zone {
            std::string name;
            double s0 = 0.0;
            double s1 = 0.0;
            double above = 0.0; // mayor I(S) − S
            double below = 0.0; // mayor S − I(S)
            double ratio = 0.0; // mayor (I(S) − S)/e
            double e_min = 1e9;
            double e_max = 0.0;
            int points = 0;
        };
        std::vector<Zone> zones;
        if (synthetic) {
            zones.push_back({"peralte 10 %", track.s_begin() + 25.0, track.s_end() - 25.0});
        } else {
            zones.push_back({"horquilla 1 (s 725-765)", 725.0, 765.0});
            zones.push_back({"horquilla 2 (s 822-862)", 822.0, 862.0});
            zones.push_back({"resto de lazadas", track.s_begin() + 25.0, track.s_end() - 25.0});
        }
        rally::sim::Pcg32 rng(synthetic ? 21 : 20, 3);
        int violations = 0;
        const int total = kFull ? 30'000 : 3'000;
        for (int i = 0; i < total; ++i) {
            const Zone& from = synthetic ? zones[0] : zones[static_cast<std::size_t>(i % 3)];
            const double s = from.s0 + (from.s1 - from.s0) * rand01(rng);
            const StagePose p = track.pose(s);
            const double side = rng.bounded(2) == 0 ? -1.0 : 1.0;
            const double edge = half_width(p, side) + kStageShoulderWidth;
            const double t = side * (edge - kStageEdgeBand * rand01(rng));
            const Point q = at(track, s, t);
            const double fx = (q.x - g.origin_x()) / g.cell();
            const double fz = (q.z - g.origin_z()) / g.cell();
            const double ci = std::floor(fx);
            const double ck = std::floor(fz);
            const auto surface_at = [&](double x, double z) {
                const StageProjection c = track.project(x, z, s - 20.0, s + 20.0);
                const StagePose cp = track.pose(c.s);
                return cp.y + cp.bank * c.t;
            };
            const double x0 = g.origin_x() + ci * g.cell();
            const double z0 = g.origin_z() + ck * g.cell();
            const double h00 = surface_at(x0, z0);
            const double h10 = surface_at(x0 + g.cell(), z0);
            const double h01 = surface_at(x0, z0 + g.cell());
            const double h11 = surface_at(x0 + g.cell(), z0 + g.cell());
            const double u = fx - ci;
            const double v = fz - ck;
            const double interpolated = v >= u ? h00 + v * (h01 - h00) + u * (h11 - h01)
                                               : h00 + u * (h10 - h00) + v * (h11 - h10);
            const double deviation = interpolated - (p.y + p.bank * t);
            const double e = error.at(s);
            Zone* zone = &zones.back();
            for (Zone& z : zones) {
                if (s >= z.s0 && s <= z.s1) {
                    zone = &z;
                    break;
                }
            }
            zone->above = std::max(zone->above, deviation);
            zone->below = std::max(zone->below, -deviation);
            zone->e_min = std::min(zone->e_min, e);
            zone->e_max = std::max(zone->e_max, e);
            if (deviation > 0.0 && e > 0.0) {
                zone->ratio = std::max(zone->ratio, deviation / e);
            }
            ++zone->points;
            if (deviation > e + 1.0e-9) {
                ++violations;
                if (violations <= 10) {
                    MESSAGE("e por debajo: s ", s, ", t ", t, ": I(S) − S ", deviation / kMm,
                            " mm, e ", e / kMm, " mm");
                }
            }
        }
        for (const Zone& z : zones) {
            MESSAGE(z.name, " (", z.points, " puntos): I(S) − S hasta ", z.above / kMm,
                    " mm por encima y ", z.below / kMm, " mm por debajo; e de ", z.e_min / kMm,
                    " a ", z.e_max / kMm, " mm; (I(S) − S)/e hasta ", z.ratio);
        }
        CHECK(violations == 0);
    }
}

TEST_CASE("stage_ground: along the stage the grid follows the edge band function without ripple") {
    // Tercera respuesta del consultor, §2 y §7, premisa 4: para cada d del perfil, en tramos de
    // peralte constante y lejos de otras pasadas, el máximo − el mínimo de (rejilla de Jolt − G)
    // a lo largo de s no pasa de 5 mm.
    const rally::sim::JoltRuntime runtime;
    struct Piece {
        bool synthetic;
        double s0;
        double s1;
        double side;
    };
    // lazadas: la recta de salida (sin peralte) y el arco de R 80 (−5 %), a los dos lados;
    // sintético: los arcos de R 60 con −10 % y +10 %, a los dos lados.
    const Piece pieces[] = {{false, 10.0, 90.0, -1.0},   {false, 10.0, 90.0, 1.0},
                            {false, 145.0, 205.0, -1.0}, {false, 145.0, 205.0, 1.0},
                            {true, 85.0, 135.0, -1.0},   {true, 85.0, 135.0, 1.0},
                            {true, 245.0, 295.0, -1.0},  {true, 245.0, 295.0, 1.0}};
    const double ds[] = {0.05, 0.3, 0.6, 0.9, 1.2, 1.5, 2.0, 3.0, 4.0, 5.0};
    for (const bool synthetic : {false, true}) {
        const std::string text = synthetic ? max_bank_text() : rally::tests::lazadas_stage_text();
        const StageTrack track = load(text);
        const StageCellError error(track);
        rally::sim::WorldDesc desc = stage_only(text);
        desc.stage_variant = rally::sim::StageGroundVariant::TerrainOnly;
        rally::sim::SimWorld terrain;
        terrain.build(desc);
        terrain.seal();
        double worst = 0.0;
        double worst_d = 0.0;
        std::string line;
        for (const double d : ds) {
            double ripple_d = 0.0;
            for (const Piece& piece : pieces) {
                if (piece.synthetic != synthetic) {
                    continue;
                }
                double lo = 1e9;
                double hi = -1e9;
                for (double s = piece.s0; s <= piece.s1; s += kFull ? 0.05 : 0.5) {
                    const StagePose p = track.pose(s);
                    const double t =
                        piece.side * (half_width(p, piece.side) + kStageShoulderWidth + d);
                    const Point q = at(track, s, t);
                    const rally::sim::GroundProbe r = terrain.probe_ground(q.x, q.z);
                    REQUIRE(r.hit);
                    const double off = r.y - single_pass_g(track, error, s, t);
                    lo = std::min(lo, off);
                    hi = std::max(hi, off);
                }
                ripple_d = std::max(ripple_d, hi - lo);
            }
            char buffer[64];
            std::snprintf(buffer, sizeof(buffer), " %.2f m: %.2f mm;", d, ripple_d / kMm);
            line += buffer;
            if (ripple_d > worst) {
                worst = ripple_d;
                worst_d = d;
            }
        }
        MESSAGE((synthetic ? "peralte 10 %" : "lazadas"),
                ": ondulación (máx − mín de rejilla − G a lo largo de s) a d =", line,
                " la mayor, ", worst / kMm, " mm a ", worst_d, " m");
        // Objetivo 5 mm (tercera respuesta del consultor); con D = 4 m quedan 7,2 y 13,8 mm, que la
        // cuarta respuesta (§4) acepta como límite del formato 1. El test guarda esas cifras.
        CHECK(worst <= 15.0 * kMm);
    }
}

TEST_CASE("stage_ground: the combination of passes does not jump where a zone ends") {
    // Cuarta respuesta del consultor, §1 y §5, premisa 1: fuera de los núcleos, la combinación
    // continua de las pasadas en cuya zona está la muestra. Al salir un pie de su zona (su último
    // centímetro), G no salta más de 2 mm, en lazadas y en el sintético del 10 %.
    for (const bool synthetic : {false, true}) {
        const StageTrack track =
            load(synthetic ? max_bank_text() : rally::tests::lazadas_stage_text());
        const StageGround g = StageGround::build(track);
        const StageProjection p = track.acquire(g.zone_jump_x(), g.zone_jump_z());
        MESSAGE((synthetic ? "peralte 10 %" : "lazadas"),
                ": mayor salto de la combinación al salir de una zona ", g.zone_jump() / kMm,
                " mm, en s = ", p.s, ", t = ", p.t);
        CHECK(g.zone_jump() <= 2.0 * kMm);
    }
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
        // Una celda más allá del empalme de la banda (d₁ + D, tercera respuesta del consultor §1).
        const Point a = at(track, s, side * (e + 2.0 * kStageEdgeBand + kStageEdgeBlend + 0.1));
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
        slope = std::max(slope, std::abs(gb.y - ga.y) /
                                    (10.0 - 2.0 * kStageEdgeBand - kStageEdgeBlend - 0.1));
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
          rally::sim::StageGroundVariant::GridOnly, rally::sim::StageGroundVariant::DenseMesh,
          rally::sim::StageGroundVariant::TerrainOnly}) {
        rally::sim::WorldDesc desc = stage_only(rally::tests::lazadas_stage_text());
        desc.stage_variant = v;
        rally::sim::SimWorld world;
        world.build(desc);
        world.seal();
        CHECK(world.stage() != nullptr);
        CHECK(world.stage_ground_bytes() > 0);
        // El terreno solo es la misma geometría sin la malla en Jolt: el mismo hash.
        if (v == rally::sim::StageGroundVariant::Design ||
            v == rally::sim::StageGroundVariant::TerrainOnly) {
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
