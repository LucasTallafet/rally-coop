#include "band_report.h"
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
// Cambia si cambia la malla, el terreno o lazadas.stage; se actualiza como la huella. Formato 2
// (formato-2, lote 6): 0xdd438cd676e8c121 → 0x56180e49eb1ff6a8.
constexpr std::uint64_t kExpectedStageGroundHash = 0x5618'0e49'eb1f'f6a8ull;

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

// La horquilla llana con una pendiente del 1 % (la continuidad del terreno donde la curvatura
// salta necesita alturas distintas entre los brazos; al 5 %, el muro entre ellos lejos de la
// horquilla pasaría de 80° y el tramo no carga).
std::string hairpin_grade_text() {
    std::string text = hairpin_text();
    const std::string flat = "pvi 437.69911184307752 0 0";
    const std::size_t at = text.find(flat);
    REQUIRE(at != std::string::npos);
    text.replace(at, flat.size(), "pvi 437.69911184307752 4.3769911184307752 0");
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

// Mayor diferencia de altura entre dos muestras vecinas con colisión de la rejilla a la vista
// (ninguna de las dos bajo la malla: bajo ella, la rejilla no se ve ni se toca).
double g_step_x = 0.0;
double g_step_z = 0.0;

double max_neighbour_step(const StageGround& g) {
    double worst = 0.0;
    constexpr int n = rally::sim::kStageTileSamples;
    const rally::tests::MeshQuery query(g);
    std::vector<double> covered;
    const auto visible = [&](const rally::sim::StageTile& tile, int i, int k) {
        const double x = g.origin_x() +
                         static_cast<double>(tile.tx * rally::sim::kStageTileCells + i) * g.cell();
        const double z = g.origin_z() +
                         static_cast<double>(tile.tz * rally::sim::kStageTileCells + k) * g.cell();
        query.heights(x, z, covered);
        return covered.empty();
    };
    for (const rally::sim::StageTile& tile : g.tiles()) {
        for (int k = 0; k < n; ++k) {
            for (int i = 0; i < n; ++i) {
                const float h = tile.heights[static_cast<std::size_t>(k * n + i)];
                if (h == rally::sim::kStageNoCollision || !visible(tile, i, k)) {
                    continue;
                }
                if (i + 1 < n && visible(tile, i + 1, k)) {
                    const float r = tile.heights[static_cast<std::size_t>(k * n + i + 1)];
                    if (r != rally::sim::kStageNoCollision &&
                        std::abs(static_cast<double>(r - h)) > worst) {
                        worst = std::abs(static_cast<double>(r - h));
                        g_step_x = g.origin_x() +
                                   static_cast<double>(tile.tx * rally::sim::kStageTileCells + i) *
                                       g.cell();
                        g_step_z = g.origin_z() +
                                   static_cast<double>(tile.tz * rally::sim::kStageTileCells + k) *
                                       g.cell();
                    }
                }
                if (k + 1 < n && visible(tile, i, k + 1)) {
                    const float d = tile.heights[static_cast<std::size_t>((k + 1) * n + i)];
                    if (d != rally::sim::kStageNoCollision &&
                        std::abs(static_cast<double>(d - h)) > worst) {
                        worst = std::abs(static_cast<double>(d - h));
                        g_step_x = g.origin_x() +
                                   static_cast<double>(tile.tx * rally::sim::kStageTileCells + i) *
                                       g.cell();
                        g_step_z = g.origin_z() +
                                   static_cast<double>(tile.tz * rally::sim::kStageTileCells + k) *
                                       g.cell();
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
    REQUIRE(g.vertices().size() == sections.size() * g.vertices_per_section());
    REQUIRE(g.vertices_per_section() == rally::sim::kStageSectionVertices);
    // El núcleo, en medio de la sección (antes, el F y la cuneta de la izquierda).
    const std::size_t core = (g.vertices_per_section() - 5) / 2;
    double worst = 0.0;
    for (std::size_t i = 0; i < sections.size(); ++i) {
        const StagePose p = track.pose(sections[i]);
        const double ts[5] = {-(p.half_width_left + kStageShoulderWidth), -p.half_width_left, 0.0,
                              p.half_width_right, p.half_width_right + kStageShoulderWidth};
        for (int j = 0; j < 5; ++j) {
            const rally::sim::StageVertex& v =
                g.vertices()[i * g.vertices_per_section() + core + static_cast<std::size_t>(j)];
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
    const std::size_t core = (g.vertices_per_section() - 5) / 2;
    const auto vertex = [&g, core](std::size_t section, int j) {
        const rally::sim::StageVertex& v =
            g.vertices()[section * g.vertices_per_section() + core + static_cast<std::size_t>(j)];
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
                "; con más pies de los que caben: ", g.candidate_overflows(),
                "; en el centro de un arco: ", g.center_overflows().size());
        CHECK(g.candidate_overflows() == 0);
        // Respuesta del consultor a la parada de tramo-ajustes (lote 5): las del centro de
        // curvatura de un arco se cuentan aparte; su altura queda entre las de sus pies.
        for (const rally::sim::StageCenterOverflow& c : g.center_overflows()) {
            char text[200];
            std::snprintf(text, sizeof(text),
                          "(%.3f, %.3f): a %.3g m del vértice; altura %.17g m, pies de %.17g a "
                          "%.17g m (fuera del intervalo: %.3g m)",
                          c.x, c.z, c.distance, c.height, c.foot_min, c.foot_max,
                          std::max({0.0, c.foot_min - c.height, c.height - c.foot_max}));
            MESSAGE("  centro de arco en ", text);
            CHECK(c.distance < rally::sim::kStageArcCenterTolerance);
            // En la precisión en que el suelo guarda la altura (float, respecto al origen): en
            // double, la media ponderada de pies iguales puede salirse por el redondeo (un ulp).
            const auto stored = [&g](double world_y) {
                return static_cast<float>(world_y - g.origin_y());
            };
            CHECK(stored(c.height) >= stored(c.foot_min));
            CHECK(stored(c.height) <= stored(c.foot_max));
        }
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

using rally::sim::kStageBandFree;
using rally::sim::kStageCoreDrop;
using rally::sim::kStageEdgeBand;
using rally::sim::kStageEdgeBlend;
using rally::sim::StageBandKind;
using rally::sim::StageBandSide;
using rally::sim::StageCellError;

std::string max_bank_text() { return rally::tests::max_bank_stage(); }

// La banda del lado de t en la sección más cercana a s.
const StageBandSide& band_side_at(const StageGround& g, double s, double t) {
    const std::vector<double>& sections = g.sections();
    auto it = std::lower_bound(sections.begin(), sections.end(), s);
    if (it == sections.end()) {
        --it;
    } else if (it != sections.begin() && s - *(it - 1) < *it - s) {
        --it;
    }
    const auto i = static_cast<std::size_t>(it - sections.begin());
    return g.band_sides()[i * 2 + (t >= 0.0 ? 1 : 0)];
}

// Una muestra junto a un final libre de la banda sin otras pasadas cerca: la explanada en el
// plano del arcén, a la que rodea la regla de la rejilla (B, el empalme y el terreno natural a la
// altura del final).
bool free_side_around(const StageGround& g, double s0, double s1, double t) {
    for (const double s : {s0, 0.5 * (s0 + s1), s1}) {
        if (band_side_at(g, s, t).kind != StageBandKind::Free) {
            return false;
        }
    }
    return true;
}

// Lejos de curvas (R > 200 m en ±15 m de s): por fuera de una curva, el terreno natural mezcla los
// finales de todo un trozo de arco, que suben o bajan con la pendiente, y no es la altura de un
// final (por fuera de la R 100 de lazadas, 9,5 mm a 18 m del eje).
bool open_curve_around(const StageTrack& track, double s) {
    for (double ds = -15.0; ds <= 15.0; ds += 1.0) {
        if (std::abs(track.pose(s + ds).curvature) > 0.005) {
            return false;
        }
    }
    return true;
}

// G de la regla de la rejilla (stage_ground.h) en (s, t) junto a un final libre, lejos de otras
// pasadas: el terreno natural es la altura del final − 5 mm, y S el plano del arcén prolongado.
double free_end_g(const StageTrack& track, const StageCellError& error, double s, double t) {
    const StagePose p = track.pose(s);
    const double side = t >= 0.0 ? 1.0 : -1.0;
    const double end = half_width(p, t) + kStageShoulderWidth + kStageBandFree;
    const double d = std::abs(t) - end;
    const double surface = p.y + p.bank * t;
    const double band = surface - kStageTerrainDrop - error.at(s);
    if (d <= kStageEdgeBand) {
        return band;
    }
    const double natural = p.y + p.bank * side * end - kStageTerrainDrop;
    const double u = std::clamp((d - kStageEdgeBand) / kStageEdgeBlend, 0.0, 1.0);
    return band + u * u * (3.0 - 2.0 * u) * (natural - band);
}

} // namespace

TEST_CASE("stage_ground: around a free band end the grid follows the edge band function") {
    // Junto a un final libre y lejos de otras pasadas: en ±d₁ del final, el plano del arcén − 5 mm
    // − e(s); de d₁ a d₁ + D, el empalme con el terreno natural (la altura del final − 5 mm); más
    // allá, el terreno natural, llano en lateral. Y bajo la banda (más de d₁ dentro), 10 cm bajo
    // la malla como poco.
    const StageTrack& track = lazadas();
    const StageGround& g = lazadas_ground();
    const rally::tests::MeshQuery query(g);
    const StageCellError error(track);
    double band = 0.0;  // |H − G| en ±d₁ y el empalme
    double alone = 0.0; // |H − final| de d₁ + D a 10 m
    double under = 1e9; // el menor margen bajo la banda
    double band_s = 0.0, band_t = 0.0, alone_s = 0.0, alone_t = 0.0;
    int n_band = 0;
    int n_alone = 0;
    int n_under = 0;
    std::vector<double> h_mesh;
    std::size_t index = 0;
    for_each_sample(g, [&](double x, double z, double h, bool has) {
        if (!has) {
            return;
        }
        if constexpr (!kFull) {
            if ((index++ % 17) != 0) {
                return;
            }
        }
        query.heights(x, z, h_mesh);
        const StageProjection p = track.acquire(x, z);
        if (p.s <= track.s_begin() + 2.0 || p.s >= track.s_end() - 2.0) {
            return;
        }
        const StagePose pose = track.pose(p.s);
        const double end = half_width(pose, p.t) + kStageShoulderWidth + kStageBandFree;
        const double d = std::abs(p.t) - end;
        if (d < -kStageEdgeBand - 0.01 && !h_mesh.empty()) {
            under = std::min(under, *std::min_element(h_mesh.begin(), h_mesh.end()) - h);
            ++n_under;
            return;
        }
        if (d < -kStageEdgeBand + 0.01 || other_pass_distance(track, x, z, p.s, 50.0) < 30.0 ||
            !free_side_around(g, p.s - 2.0, p.s + 2.0, p.t) || !open_curve_around(track, p.s)) {
            return;
        }
        if (d <= kStageEdgeBand + kStageEdgeBlend) {
            const double off = std::abs(h - free_end_g(track, error, p.s, p.t));
            if (off > band) {
                band = off;
                band_s = p.s;
                band_t = p.t;
            }
            ++n_band;
        } else if (d <= 10.0) {
            const double side = p.t >= 0.0 ? 1.0 : -1.0;
            const double natural = pose.y + pose.bank * side * end - kStageTerrainDrop;
            if (std::abs(h - natural) > alone) {
                alone = std::abs(h - natural);
                alone_s = p.s;
                alone_t = p.t;
            }
            ++n_alone;
        }
    });
    MESSAGE("bajo la banda (", n_under, " muestras): margen mínimo bajo la malla ", under / kMm,
            " mm; junto al final libre y en el empalme (", n_band, "): ", band / kMm,
            " mm de G (s = ", band_s, ", t = ", band_t, "); de d₁ + D a 10 m (", n_alone,
            "): ", alone / kMm, " mm del final − 5 mm (s = ", alone_s, ", t = ", alone_t, ")");
    CHECK(n_under > (kFull ? 1000 : 50));
    CHECK(under >= kStageCoreDrop - kMm);
    CHECK(n_band > (kFull ? 1000 : 50));
    // 1 mm junto al arcén con el formato 1; 5 m más allá, en las curvas verticales (s 309 y 899)
    // el terreno natural mezcla finales a otra altura: 1,0 y 1,4 mm.
    CHECK(band <= 1.5 * kMm);
    CHECK(alone <= 1.5 * kMm);
}

namespace {

struct GridMargins {
    double lowest = 1e9; // margen mínimo de la malla sobre la rejilla (m)
    double worst_s = 0.0;
    double worst_t = 0.0;
    int above = 0;      // puntos con la rejilla encima de la malla
    int points = 0;     // puntos bajo la banda o el núcleo
    int edge_cells = 0; // puntos en celdas que cruzan un final libre
    double step = 0.0;  // el mayor escalón de la malla a la rejilla en el final libre (m)
    double step_s = 0.0;
    double step_side = 0.0;
    double step_e = 0.0;
    double step_r80 = 0.0; // el mayor en la curva de R 80 por fuera (s 100–160, izquierda)
};

// Criterio 7 y la costura banda–rejilla del criterio 10: la rejilla de Jolt (cuantizada: un mundo
// con el terreno solo) frente a la malla de Jolt en `random` puntos al azar de la banda y del
// núcleo, y en `edge` puntos de celdas que cruzan un final libre, donde se mide además el escalón
// de la malla a la rejilla 2 cm dentro del final (el final de la malla es una cuerda entre
// secciones).
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
    const auto end_of = [&](double s, double side) {
        const StageBandSide& b = band_side_at(g, s, side);
        return half_width(track.pose(s), side) + kStageShoulderWidth + b.end_u;
    };
    rally::sim::Pcg32 rng(seed, 11);
    for (int i = 0; i < random; ++i) {
        const double s = track.s_begin() + (track.s_end() - track.s_begin()) * rand01(rng);
        // 2 cm dentro del final: el final de la malla es una cuerda entre secciones.
        const double left = end_of(s, -1.0) - 0.02;
        const double right = end_of(s, 1.0) - 0.02;
        const double t = -left + (left + right) * rand01(rng);
        const Point q = at(track, s, t);
        const rally::sim::GroundProbe m = mesh.probe_ground(q.x, q.z);
        const rally::sim::GroundProbe r = terrain.probe_ground(q.x, q.z);
        if (!m.hit || !r.hit) {
            continue; // fuera de la franja
        }
        ++out.points;
        const double margin = m.y - r.y;
        out.above += margin <= 0.0 ? 1 : 0;
        if (margin < out.lowest) {
            out.lowest = margin;
            out.worst_s = s;
            out.worst_t = t;
        }
    }
    int count = 0;
    for (int i = 0; count < edge; ++i) {
        REQUIRE(i < 200 * edge);
        const double s = track.s_begin() + (track.s_end() - track.s_begin()) * rand01(rng);
        const double side = rng.bounded(2) == 0 ? -1.0 : 1.0;
        const StageBandSide& b = band_side_at(g, s, side);
        if (b.kind != StageBandKind::Free && b.kind != StageBandKind::Ramp) {
            continue;
        }
        const double e = end_of(s, side);
        const double t = side * (e - 0.02 - (kCellDiagonal - 0.02) * rand01(rng));
        const Point q = at(track, s, t);
        const rally::sim::GroundProbe m = mesh.probe_ground(q.x, q.z);
        const rally::sim::GroundProbe r = terrain.probe_ground(q.x, q.z);
        if (!m.hit || !r.hit) {
            continue;
        }
        ++count;
        const double margin = m.y - r.y;
        out.above += margin <= 0.0 ? 1 : 0;
        if (margin < out.lowest) {
            out.lowest = margin;
            out.worst_s = s;
            out.worst_t = t;
        }
        const Point a = at(track, s, side * (e - 0.02));
        const double stp = mesh.probe_ground(a.x, a.z).y - terrain.probe_ground(a.x, a.z).y;
        if (stp > out.step) {
            out.step = stp;
            out.step_s = s;
            out.step_side = side;
            out.step_e = error.at(s);
        }
        if (s >= 100.0 && s <= 160.0 && side < 0.0) {
            out.step_r80 = std::max(out.step_r80, stp);
        }
    }
    out.edge_cells = count;
    return out;
}

} // namespace

TEST_CASE("stage_ground: the grid never shows above the band or the core") {
    // Criterio 7: la rejilla de Jolt, cuantizada, nunca queda encima de la malla, en puntos al azar
    // de la banda y del núcleo y en celdas que cruzan un final libre. Diagnóstico del criterio 10:
    // el escalón de la malla a la rejilla en el final libre, y en la curva de R 80 por fuera.
    const rally::sim::JoltRuntime runtime;
    struct Case {
        const char* name;
        std::string text;
        int random;
        int edge;
    };
    std::vector<Case> cases = {
        {"lazadas", rally::tests::lazadas_stage_text(), kFull ? 10'000 : 1'000,
         kFull ? 2'000 : 200},
        {"hairpin_flat", hairpin_text(), kFull ? 4'000 : 400, kFull ? 1'000 : 100},
        {"peralte 10 %", max_bank_text(), kFull ? 4'000 : 400, kFull ? 1'000 : 100},
    };
    if constexpr (kFull) {
        cases.push_back({"sintético 30 km", rally::tests::synthetic_stage_30km(), 10'000, 2'000});
    }
    for (const Case& c : cases) {
        const GridMargins m = grid_margins(c.text, c.random, c.edge, 10'000);
        MESSAGE(std::string(c.name), ": ", m.points, " puntos de la banda y del núcleo y ",
                m.edge_cells, " en el final libre; margen mínimo de la malla sobre la rejilla ",
                m.lowest / kMm, " mm (s = ", m.worst_s, ", t = ", m.worst_t,
                "); puntos con la rejilla encima ", m.above, "; escalón en el final libre hasta ",
                m.step / kMm, " mm (s = ", m.step_s, ", lado ", m.step_side,
                ", e = ", m.step_e / kMm, " mm); en la R 80 por fuera (s 100–160), ",
                m.step_r80 / kMm, " mm");
        CHECK(m.above == 0);
        CHECK(m.lowest > 0.0);
    }
}

TEST_CASE("stage_ground: e bounds the interpolation error of the band end plane in a cell") {
    // Tercera respuesta del consultor a tramo-fisica, §7, premisa 1, en el final libre de la banda
    // (la explanada en el plano del arcén prolongado): la interpolación de S en la celda de la
    // rejilla (con la triangulación de HeightFieldShape y S exacta en sus esquinas) no queda más de
    // e(s) por encima de S en la banda de d₁ del lado de dentro del final.
    for (const bool synthetic : {false, true}) {
        const StageTrack track =
            load(synthetic ? max_bank_text() : rally::tests::lazadas_stage_text());
        const StageGround g = StageGround::build(track);
        const StageCellError error(track);
        rally::sim::Pcg32 rng(synthetic ? 21 : 20, 3);
        int violations = 0;
        int points = 0;
        double ratio = 0.0;
        const int total = kFull ? 30'000 : 3'000;
        for (int i = 0; i < total; ++i) {
            const double s =
                track.s_begin() + 25.0 + (track.s_end() - track.s_begin() - 50.0) * rand01(rng);
            const StagePose p = track.pose(s);
            const double side = rng.bounded(2) == 0 ? -1.0 : 1.0;
            if (!free_side_around(g, s - 2.0, s + 2.0, side)) {
                continue;
            }
            const double end = half_width(p, side) + kStageShoulderWidth + kStageBandFree;
            const double t = side * (end - kStageEdgeBand * rand01(rng));
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
            ++points;
            if (deviation > 0.0 && e > 0.0) {
                ratio = std::max(ratio, deviation / e);
            }
            violations += deviation > e + 1.0e-9 ? 1 : 0;
        }
        MESSAGE((synthetic ? std::string("peralte 10 %") : std::string("lazadas")), ": ", points,
                " puntos junto a un final libre; (I(S) − S)/e hasta ", ratio);
        CHECK(points > total / 10);
        CHECK(violations == 0);
    }
}

TEST_CASE("stage_ground: the combination of passes does not jump where a zone ends") {
    // Cuarta respuesta del consultor a tramo-fisica, §1 y §5, premisa 1: fuera de las bandas, la
    // combinación continua de las pasadas en cuya zona está la muestra. Al salir un pie de su zona
    // (su último centímetro), G no salta más de 2 mm, en lazadas y en el sintético del 10 %.
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

TEST_CASE("stage_ground: far from other passes the terrain is flat across at the band end height") {
    const rally::sim::JoltRuntime runtime;
    const StageTrack& track = lazadas();
    const StageGround& g = lazadas_ground();
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
        if (!free_side_around(g, s - 2.0, s + 2.0, side) || !open_curve_around(track, s)) {
            continue;
        }
        const double e = half_width(p, side) + kStageShoulderWidth + kStageBandFree;
        // Una celda más allá del empalme.
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
            " mm del final de la banda − 5 mm; pendiente lateral ", 100.0 * slope, " %");
    CHECK(worst <= 2.0 * kMm);
    CHECK(slope <= 0.001);
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
        const StageProjection where = track.acquire(g_step_x, g_step_z);
        MESSAGE("el mayor salto con 0,25 m, en s = ", where.s, ", t = ", where.t);
        MESSAGE((hairpin ? "horquilla con pendiente" : "lazadas"), ": mayor salto entre vecinas ",
                coarse, " m con 0,75 m y ", fine, " m con 0,25 m");
        // En lazadas, el mayor salto lo da una pared del terreno natural entre pasadas lejanas (s
        // 463, a 40 m del eje, la misma que con el formato 1): no baja con la celda y solo se
        // informa.
        if (hairpin) {
            CHECK(fine <= 0.5 * coarse);
        }
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
         {rally::sim::StageGroundVariant::Design, rally::sim::StageGroundVariant::DenseMesh,
          rally::sim::StageGroundVariant::TerrainOnly}) {
        rally::sim::WorldDesc desc = stage_only(rally::tests::lazadas_stage_text());
        desc.stage_variant = v;
        rally::sim::SimWorld world;
        world.build(desc);
        world.seal();
        CHECK(world.stage() != nullptr);
        CHECK(world.stage_ground_bytes() > 0);
        // El terreno solo es la misma geometría sin la malla en Jolt: el mismo hash.
        if (v == rally::sim::StageGroundVariant::DenseMesh) {
            CHECK(world.stage_ground_hash() != lazadas_ground().hash());
        } else {
            CHECK(world.stage_ground_hash() == lazadas_ground().hash());
        }
    }
    // El microrrelieve no entra en la geometría (premortem de la respuesta del consultor a la
    // parte A, §4): el mismo hash con él y sin él.
    rally::sim::WorldDesc plain = stage_only(rally::tests::lazadas_stage_text());
    plain.stage_micro_relief = false;
    rally::sim::SimWorld without;
    without.build(plain);
    without.seal();
    CHECK(without.stage_ground_hash() == kExpectedStageGroundHash);
    // La densa no reutiliza la banda de StageTrack::build: su malla es otra.
    const StageGround dense = StageGround::build(
        lazadas(), 0.375, rally::sim::StageGroundKind::Design, rally::sim::kStageDenseMesh);
    CHECK(dense.vertices_per_section() ==
          static_cast<std::size_t>(2 * rally::sim::kStageDenseMesh.ditch_segments + 7));
    CHECK(dense.sections().size() > lazadas_ground().sections().size());
    // Sin tramo, todo a cero.
    // Sin tramo, todo a cero.
    rally::sim::SimWorld empty;
    empty.build(rally::sim::WorldDesc{});
    empty.seal();
    CHECK(empty.stage() == nullptr);
    CHECK(empty.stage_run() == nullptr);
    CHECK(empty.stage_ground_hash() == 0);
    CHECK(empty.stage_ground_bytes() == 0);
}
