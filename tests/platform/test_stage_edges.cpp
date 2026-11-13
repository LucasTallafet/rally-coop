#include "io/recording.h"
#include "platform/near_field.h"
#include "platform/stage_checked.h"
#include "platform/stage_chunks.h"
#include "platform/stage_look.h"
#include "platform/stage_mesh.h"
#include "platform/stage_near.h"
#include "platform/stage_surface.h"
#include "sim/jolt_runtime.h"
#include "sim/pcg32.h"
#include "sim/sim_world.h"
#include "sim/stage_ground.h"
#include "sim/stage_track.h"
#include "sim/test_zone.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

// Bordes entre superficies cerca del ojo (lote 7, bordes-dibujo), sin ventana. Hasta el lote 6, a
// menos de 30 m del ojo solo se dibujaba la rejilla cercana, con un índice de color por celda de
// 0,25 m: un borde curvo salía en dientes. Ahora cerca se dibuja la malla del núcleo, con los
// bordes donde están. Estos tests miden el color que decide cada dibujo frente al del triángulo de
// arriba (StageSurface::query).

using namespace rally::platform;
namespace sim = rally::sim;

namespace {

constexpr double kNearRadius = 30.0;
constexpr double kNearSpacing = 0.25;
constexpr double kEdgeMargin = 0.05; // m: más cerca de un borde no se exige

struct Fixture {
    sim::JoltRuntime runtime;
    std::string text;
    sim::StageTrack track;
    sim::StageGround ground;
    sim::SimWorld world;
    sim::TestZone noise{};
    StageSurface* surface = nullptr;
    StageChunks chunks;
};

const Fixture& fixture() {
    static Fixture* f = [] {
        auto* fx = new Fixture;
        std::string error;
        REQUIRE_MESSAGE(
            rally::io::read_file(RALLY_SOURCE_DIR "/data/stages/lazadas.stage", fx->text, error),
            error);
        const sim::StageParseResult parsed = sim::StageTrack::build(fx->text, fx->track);
        REQUIRE_MESSAGE(parsed.ok, parsed.error);
        sim::WorldDesc desc;
        desc.stage_text = fx->text;
        fx->world.build(desc);
        fx->world.seal();
        REQUIRE_MESSAGE(
            build_checked_ground(fx->track, fx->world.stage_ground_hash(), fx->ground, error),
            error);
        fx->surface = new StageSurface(fx->ground);
        fx->chunks = StageChunks::build(fx->ground);
        return fx;
    }();
    return *f;
}

// Colores de prueba, distintos entre sí, para los siete índices de color (0..4 superficies, roca,
// tierra de cuneta). Sin sombrear.
using Rgb = std::array<double, 3>;
Rgb palette_rgb(int index) {
    static const Rgb colors[kStageColorCount] = {{96, 99, 106},   {198, 182, 142}, {164, 124, 84},
                                                 {240, 243, 247}, {236, 222, 160}, {100, 112, 138},
                                                 {134, 102, 69}};
    return colors[index];
}

double unit(sim::Pcg32& rng) { return static_cast<double>(rng.next_u32()) / 4294967296.0; }

// El índice de color del triángulo de arriba en (x, z), o −1 sin suelo.
int top_index(const StageSurface& surface, double x, double z) {
    const StageHit hit = surface.query(x, z);
    if (!hit.valid) {
        return -1;
    }
    return hit.core ? stage_color_index(hit.surface, hit.look) : static_cast<int>(hit.surface);
}

// (x, z) a más de kEdgeMargin de cualquier cambio de color (16 puntos de una circunferencia).
bool away_from_edges(const StageSurface& surface, double x, double z, int index) {
    for (int k = 0; k < 16; ++k) {
        const double a = 6.283185307179586 * k / 16.0;
        if (top_index(surface, x + kEdgeMargin * std::cos(a), z + kEdgeMargin * std::sin(a)) !=
            index) {
            return false;
        }
    }
    return true;
}

struct Candidate {
    double x = 0.0;
    double z = 0.0;
    int index = 0;
    double eye_distance = 0.0; // en planta, al ojo de ese grupo
};

// 2000 puntos: 4 ojos (s = 100, 633, 740, 841) × 500, la mitad al azar a menos de 30 m del ojo y la
// mitad a menos de 0,3 m de los bordes de la calzada y del arcén (donde están los bordes), todos a
// más de 5 cm de cualquier cambio de color.
std::vector<Candidate> candidates(const Fixture& f) {
    std::vector<Candidate> out;
    sim::Pcg32 rng(0x5eed1234ull, 17);
    for (const double s : {100.0, 633.0, 740.0, 841.0}) {
        const sim::StagePose eye = f.track.pose(s);
        int taken = 0;
        while (taken < 500) {
            double x = 0.0;
            double z = 0.0;
            if (taken % 2 == 0) {
                const double angle = 6.283185307179586 * unit(rng);
                const double r = kNearRadius * std::sqrt(unit(rng));
                x = eye.x + r * std::cos(angle);
                z = eye.z + r * std::sin(angle);
            } else {
                // Cerca de un borde de calzada o de arcén, a menos de 30 m del ojo.
                const double ds = -25.0 + 50.0 * unit(rng);
                const sim::StagePose p = f.track.pose(std::clamp(s + ds, 0.0, 1150.0));
                const double side = unit(rng) < 0.5 ? -1.0 : 1.0;
                const double half = side < 0 ? p.half_width_left : p.half_width_right;
                const double t =
                    side * (half + (unit(rng) < 0.5 ? 0.0 : 1.0) + 0.6 * (unit(rng) - 0.5));
                x = p.x + t * std::cos(p.heading);
                z = p.z + t * std::sin(p.heading);
                if (std::hypot(x - eye.x, z - eye.z) >= kNearRadius) {
                    continue;
                }
            }
            const int index = top_index(*f.surface, x, z);
            if (index < 0 || !away_from_edges(*f.surface, x, z, index)) {
                continue;
            }
            out.push_back({x, z, index, std::hypot(x - eye.x, z - eye.z)});
            ++taken;
        }
    }
    return out;
}

double color_error(const Rgb& a, const Rgb& b) {
    return std::max({std::abs(a[0] - b[0]), std::abs(a[1] - b[1]), std::abs(a[2] - b[2])});
}

} // namespace

TEST_CASE("stage_edges: the near grid colour is not the colour of the top triangle at the edges "
          "(measurement of the old drawing)") {
    const Fixture& f = fixture();
    const StageNearSampler sampler(f.track, *f.surface, f.noise);
    const std::vector<Candidate> points = candidates(f);
    REQUIRE(points.size() == 2000);
    int wrong = 0;
    int compared = 0;
    for (const Candidate& c : points) {
        // El color que da la rejilla: el de los tres vértices del triángulo de la malla que
        // contiene el punto (diagonal a–c), interpolado como el shader.
        const auto gx = static_cast<std::int64_t>(std::floor(c.x / kNearSpacing));
        const auto gz = static_cast<std::int64_t>(std::floor(c.z / kNearSpacing));
        const double u = c.x / kNearSpacing - static_cast<double>(gx);
        const double v = c.z / kNearSpacing - static_cast<double>(gz);
        struct Vertex {
            std::int64_t i, j;
            double w;
        };
        const std::array<Vertex, 3> tri =
            u >= v ? std::array<Vertex, 3>{{{gx, gz, 1.0 - u},
                                            {gx + 1, gz, u - v},
                                            {gx + 1, gz + 1, v}}}
                   : std::array<Vertex, 3>{
                         {{gx, gz, 1.0 - v}, {gx, gz + 1, v - u}, {gx + 1, gz + 1, u}}};
        Rgb blended{};
        bool valid = true;
        for (const Vertex& w : tri) {
            const NearTexel t = sampler.cell(w.i, w.j, kNearSpacing);
            valid = valid && t.valid > 0.5f;
            const Rgb rgb = palette_rgb(static_cast<int>(t.surface));
            for (std::size_t k = 0; k < 3; ++k) {
                blended[k] += w.w * rgb[k];
            }
        }
        if (!valid) {
            continue;
        }
        ++compared;
        if (color_error(blended, palette_rgb(c.index)) > 1.5) {
            ++wrong;
        }
    }
    MESSAGE("rejilla cercana de 0,25 m: ", wrong, " de ", compared,
            " puntos a más de 5 cm de un borde con un color distinto del del triángulo de arriba");
    // Si no falla, el test no mide los bordes.
    CHECK(wrong > 0);
}

TEST_CASE("stage_edges: the colour the core mesh draws is the colour of the top triangle at every "
          "point near the eye") {
    const Fixture& f = fixture();
    const std::vector<Candidate> points = candidates(f);
    REQUIRE(points.size() == 2000);
    // Los colores que dibuja la malla del núcleo, por trozo (cada uno se construye una vez).
    StagePalette palette{};
    for (std::size_t i = 0; i < palette.size(); ++i) {
        const Rgb c = palette_rgb(static_cast<int>(i));
        palette[i] = {static_cast<unsigned char>(c[0]), static_cast<unsigned char>(c[1]),
                      static_cast<unsigned char>(c[2])};
    }
    StageLookStyle style;
    style.wall = {100, 112, 138};
    style.ditch_darken = kStageDitchDarken;
    std::map<std::size_t, StageMeshBuffers> built;
    const std::vector<sim::StageTriangle>& triangles = f.ground.triangles();
    int core_points = 0;
    int terrain_points = 0;
    int wrong = 0;
    int missing = 0;
    int contested = 0;
    int clipped = 0;
    std::vector<int> near_chunks;
    for (const Candidate& c : points) {
        // Lo que el recorte deja sin dibujar (radio de kStageNearClipRadius: 0, nada).
        if (c.eye_distance < kStageNearClipRadius) {
            ++clipped;
            continue;
        }
        const StageHit hit = f.surface->query(c.x, c.z);
        ++(hit.core ? core_points : terrain_points);
        // El triángulo de arriba de la malla, entre los de los trozos a menos de 5 cm.
        near_chunks.clear();
        f.chunks.select_core(c.x, c.z, 0.05, near_chunks);
        const double lx = c.x - f.ground.origin_x();
        const double lz = c.z - f.ground.origin_z();
        bool found = false;
        double best_y = 0.0;
        std::size_t best_chunk = 0;
        std::uint32_t best_n = 0;
        for (const int ci : near_chunks) {
            const StageChunk& chunk = f.chunks.core()[static_cast<std::size_t>(ci)];
            for (std::uint32_t n = 0; n < chunk.count; ++n) {
                const sim::StageTriangle& t = triangles[f.chunks.core_triangles()[chunk.first + n]];
                const sim::StageVertex& a = f.ground.vertices()[t.a];
                const sim::StageVertex& b = f.ground.vertices()[t.b];
                const sim::StageVertex& d = f.ground.vertices()[t.c];
                const double den =
                    (static_cast<double>(b.z) - d.z) * (static_cast<double>(a.x) - d.x) +
                    (static_cast<double>(d.x) - b.x) * (static_cast<double>(a.z) - d.z);
                if (den == 0.0) {
                    continue;
                }
                const double w0 = ((static_cast<double>(b.z) - d.z) * (lx - d.x) +
                                   (static_cast<double>(d.x) - b.x) * (lz - d.z)) /
                                  den;
                const double w1 = ((static_cast<double>(d.z) - a.z) * (lx - d.x) +
                                   (static_cast<double>(a.x) - d.x) * (lz - d.z)) /
                                  den;
                const double w2 = 1.0 - w0 - w1;
                if (w0 < -1e-9 || w1 < -1e-9 || w2 < -1e-9) {
                    continue;
                }
                const double y = w0 * a.y + w1 * b.y + w2 * d.y;
                if (!found || y > best_y) {
                    found = true;
                    best_y = y;
                    best_chunk = static_cast<std::size_t>(ci);
                    best_n = n;
                }
            }
        }
        // Gana el más alto; el núcleo gana los empates dentro de kTieEpsilon (el desplazamiento de
        // polígonos del dibujo). La baldosa: la altura del terreno con las diagonales de Jolt.
        constexpr double kTieEpsilon = 0.01;
        double terrain_y = 0.0;
        const bool terrain = f.surface->terrain_at(c.x, c.z, terrain_y);
        const bool core_wins =
            found && (!terrain || best_y + f.ground.origin_y() >= terrain_y - kTieEpsilon);
        if (core_wins != hit.core) {
            ++contested; // al ras, o la malla y la consulta no coinciden
        }
        if (!core_wins) {
            // Dibuja la baldosa: de tierra.
            if (!terrain || c.index != static_cast<int>(sim::Surface::Dirt)) {
                ++wrong;
            }
            continue;
        }
        if (!found) {
            ++missing;
            continue;
        }
        auto it = built.find(best_chunk);
        if (it == built.end()) {
            const StageChunk& chunk = f.chunks.core()[best_chunk];
            StageMeshBuffers buffers;
            build_core_mesh(f.ground, f.chunks.core_triangles().data() + chunk.first, chunk.count,
                            palette, buffers, style);
            it = built.emplace(best_chunk, std::move(buffers)).first;
        }
        // Color de los tres vértices del triángulo: sin sombrear, el del índice del punto.
        const StageChunk& chunk = f.chunks.core()[best_chunk];
        const sim::StageTriangle& t = triangles[f.chunks.core_triangles()[chunk.first + best_n]];
        const int index = stage_color_index(t.surface, t.look);
        if (index != c.index) {
            ++wrong;
            continue;
        }
        const unsigned char* rgba = &it->second.colors[static_cast<std::size_t>(best_n) * 12];
        float nx = 0;
        float ny = 0;
        float nz = 0;
        {
            const sim::StageVertex* a = &f.ground.vertices()[t.a];
            const sim::StageVertex* b = &f.ground.vertices()[t.b];
            const sim::StageVertex* d = &f.ground.vertices()[t.c];
            nx = (b->y - a->y) * (d->z - a->z) - (b->z - a->z) * (d->y - a->y);
            ny = (b->z - a->z) * (d->x - a->x) - (b->x - a->x) * (d->z - a->z);
            nz = (b->x - a->x) * (d->y - a->y) - (b->y - a->y) * (d->x - a->x);
            if (ny < 0.0f) {
                nx = -nx;
                ny = -ny;
                nz = -nz;
            }
        }
        const float shade = stage_shade(nx, ny, nz);
        Rgb want = palette_rgb(index);
        if (index == kStageColorDitch) {
            const Rgb dirt = palette_rgb(static_cast<int>(sim::Surface::Dirt));
            want = {dirt[0] * kStageDitchDarken, dirt[1] * kStageDitchDarken,
                    dirt[2] * kStageDitchDarken};
        }
        for (std::size_t v = 0; v < 3; ++v) {
            const Rgb got = {static_cast<double>(rgba[v * 4]) / shade,
                             static_cast<double>(rgba[v * 4 + 1]) / shade,
                             static_cast<double>(rgba[v * 4 + 2]) / shade};
            if (color_error(got, want) > 2.5) {
                ++wrong;
                break;
            }
        }
    }
    MESSAGE("malla del núcleo: ", core_points, " puntos del núcleo y ", terrain_points,
            " del terreno; con color distinto: ", wrong, "; sin triángulo en el trozo: ", missing,
            "; ganador distinto del de la consulta: ", contested, "; recortados: ", clipped);
    CHECK(wrong == 0);
    CHECK(missing == 0);
    CHECK(clipped == 0);
    // La rejilla del terreno queda 5 mm bajo el final libre: casi nunca hay empate.
    CHECK(contested <= 20);
    CHECK(core_points > 500);
}

TEST_CASE("stage_edges: the core and terrain meshes are not clipped near the eye") {
    // El recorte de distancia del dibujo del tramo es 0: nada de lo cercano se descarta. Con un
    // radio mayor, el color de los puntos de arriba lo decidiría otra cosa que la malla.
    static_assert(kStageNearClipRadius == 0.0);
    const Fixture& f = fixture();
    std::vector<int> selected;
    for (const double s : {100.0, 633.0, 740.0, 841.0}) {
        const sim::StagePose p = f.track.pose(s);
        f.chunks.select_core(p.x, p.z, kStageDrawDistance, selected);
        int under_eye = 0;
        for (const int i : selected) {
            const StageChunk& c = f.chunks.core()[static_cast<std::size_t>(i)];
            under_eye += p.x >= c.min_x && p.x <= c.max_x && p.z >= c.min_z && p.z <= c.max_z;
        }
        CHECK(under_eye > 0); // el trozo bajo el ojo se elige (y se dibuja entero)
    }
}
