#include "io/recording.h"
#include "new_counter.h"
#include "platform/near_field.h"
#include "platform/near_ring.h"
#include "platform/stage_checked.h"
#include "platform/stage_chunks.h"
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
#include <cstdio>
#include <string>
#include <vector>

// Dibujo del tramo (lote 5, tramo-dibujo), sin ventana: la geometría que se dibuja es la de la
// física, lo cercano coincide con lo que pisa la rueda, no hay escalón entre lo cercano y lo
// lejano y los trozos cubren el tramo una vez.

using namespace rally::platform;
namespace sim = rally::sim;

namespace {

constexpr double kNearRadius = 30.0;
constexpr double kNearFadeStart = 27.0;
constexpr double kNearSpacing = 0.25;

// Se construye una sola vez para todos los tests del archivo (el ctest de Debug es lento).
struct Fixture {
    sim::JoltRuntime runtime; // antes que el mundo
    std::string text;
    sim::StageTrack track;
    sim::StageGround ground;
    std::uint64_t physics_hash = 0;
    sim::SimWorld world;
    sim::TestZone noise{};
    StageSurface* surface = nullptr;
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
        // El hash de la física: un SimWorld de solo tramo.
        sim::WorldDesc desc;
        desc.stage_text = fx->text;
        fx->world.build(desc);
        fx->world.seal();
        fx->physics_hash = fx->world.stage_ground_hash();
        REQUIRE_MESSAGE(build_checked_ground(fx->track, fx->physics_hash, fx->ground, error),
                        error);
        fx->surface = new StageSurface(fx->ground);
        return fx;
    }();
    return *f;
}

StagePalette test_palette() {
    StagePalette p{};
    for (std::size_t i = 0; i < p.size(); ++i) {
        p[i] = {static_cast<unsigned char>(10 * i + 50), static_cast<unsigned char>(20 * i + 20),
                static_cast<unsigned char>(200 - 30 * i)};
    }
    return p;
}

double unit(sim::Pcg32& rng) { return static_cast<double>(rng.next_u32()) / 4294967296.0; }

} // namespace

TEST_CASE("stage_draw: the drawn ground has the hash of the physics and a wrong hash is rejected") {
    const Fixture& f = fixture();
    CHECK(f.ground.hash() == f.physics_hash);
    CHECK(f.physics_hash != 0);
    sim::StageGround other;
    std::string error;
    CHECK_FALSE(build_checked_ground(f.track, f.physics_hash ^ 1ull, other, error));
    CHECK(!error.empty());
    char wanted[40];
    std::snprintf(wanted, sizeof wanted, "0x%016llx",
                  static_cast<unsigned long long>(f.physics_hash ^ 1ull));
    CHECK(error.find(wanted) != std::string::npos);
    char got[40];
    std::snprintf(got, sizeof got, "0x%016llx", static_cast<unsigned long long>(f.physics_hash));
    CHECK(error.find(got) != std::string::npos);
    CHECK(other.vertices().empty()); // no se toca
}

TEST_CASE("stage_draw: the far core mesh is the vertices and triangles of StageGround") {
    const Fixture& f = fixture();
    const StageChunks chunks = StageChunks::build(f.ground);
    const StagePalette palette = test_palette();
    std::size_t triangles = 0;
    std::size_t mismatches = 0;
    StageMeshBuffers buffers;
    for (const StageChunk& chunk : chunks.core()) {
        const std::uint32_t* ids = chunks.core_triangles().data() + chunk.first;
        build_core_mesh(f.ground, ids, chunk.count, palette, buffers);
        REQUIRE(buffers.triangle_count() == chunk.count);
        REQUIRE_FALSE(buffers.indexed);
        for (std::uint32_t n = 0; n < chunk.count; ++n) {
            const sim::StageTriangle& t = f.ground.triangles()[ids[n]];
            // Los tres vértices, en cualquier orden (se orienta hacia arriba), exactos en float.
            std::array<std::array<float, 3>, 3> drawn{};
            std::array<std::array<float, 3>, 3> wanted{};
            for (std::size_t v = 0; v < 3; ++v) {
                for (std::size_t c = 0; c < 3; ++c) {
                    drawn[v][c] = buffers.positions[(n * 3 + v) * 3 + c];
                }
                const sim::StageVertex& w =
                    f.ground.vertices()[v == 0 ? t.a : (v == 1 ? t.b : t.c)];
                wanted[v] = {w.x, w.y, w.z};
            }
            std::sort(drawn.begin(), drawn.end());
            std::sort(wanted.begin(), wanted.end());
            if (drawn != wanted) {
                ++mismatches;
            }
        }
        triangles += chunk.count;
    }
    CHECK(triangles == f.ground.triangles().size());
    CHECK(mismatches == 0);
    MESSAGE("núcleo: ", f.ground.vertices().size(), " vértices, ", triangles, " triángulos en ",
            chunks.core().size(), " trozos");
}

TEST_CASE("stage_draw: the far tile meshes have the samples and triangles of StageGround") {
    const Fixture& f = fixture();
    const StagePalette palette = test_palette();
    StageMeshBuffers buffers;
    std::size_t position_errors = 0;
    std::size_t triangle_errors = 0;
    std::size_t triangles = 0;
    std::size_t expected = 0;
    for (std::size_t i = 0; i < f.ground.tiles().size(); ++i) {
        const sim::StageTile& tile = f.ground.tiles()[i];
        build_tile_mesh(*f.surface, i, palette, buffers);
        REQUIRE(buffers.indexed);
        REQUIRE(buffers.vertex_count() ==
                static_cast<std::size_t>(sim::kStageTileSamples) * sim::kStageTileSamples);
        for (int k = 0; k < sim::kStageTileSamples; ++k) {
            for (int s = 0; s < sim::kStageTileSamples; ++s) {
                const std::size_t v = static_cast<std::size_t>(k * sim::kStageTileSamples + s);
                const float h = tile.heights[v];
                const float x = static_cast<float>((tile.tx * sim::kStageTileCells + s) * 0.75);
                const float z = static_cast<float>((tile.tz * sim::kStageTileCells + k) * 0.75);
                const float y = h == sim::kStageNoCollision ? 0.0f : h;
                if (buffers.positions[v * 3] != x || buffers.positions[v * 3 + 1] != y ||
                    buffers.positions[v * 3 + 2] != z) {
                    ++position_errors;
                }
            }
        }
        // Triángulos con colisión (sus tres esquinas, como en Jolt), contados aparte.
        const std::size_t with_collision = drawn_tile_triangles(tile);
        if (buffers.indices.size() != 3 * with_collision) {
            ++triangle_errors;
        }
        expected += with_collision;
        triangles += buffers.triangle_count();
    }
    CHECK(position_errors == 0);
    CHECK(triangle_errors == 0);
    CHECK(triangles == expected);
    MESSAGE(f.ground.tiles().size(), " baldosas, ", triangles, " triángulos con colisión");
}

TEST_CASE("stage_draw: every core triangle and every tile is in exactly one chunk and the draw "
          "distance decides") {
    const Fixture& f = fixture();
    const StageChunks chunks = StageChunks::build(f.ground);
    std::vector<int> seen(f.ground.triangles().size(), 0);
    for (const StageChunk& chunk : chunks.core()) {
        for (std::uint32_t n = 0; n < chunk.count; ++n) {
            ++seen[chunks.core_triangles()[chunk.first + n]];
        }
    }
    CHECK(std::count(seen.begin(), seen.end(), 1) == static_cast<std::ptrdiff_t>(seen.size()));
    CHECK(chunks.core_triangles().size() == f.ground.triangles().size());
    REQUIRE(chunks.tiles().size() == f.ground.tiles().size());
    std::vector<int> tile_seen(f.ground.tiles().size(), 0);
    for (const StageChunk& chunk : chunks.tiles()) {
        CHECK(chunk.count == 1);
        ++tile_seen[chunk.first];
    }
    CHECK(std::count(tile_seen.begin(), tile_seen.end(), 1) ==
          static_cast<std::ptrdiff_t>(tile_seen.size()));

    // Distancia de dibujo: 0,1 m más allá no se dibuja, 0,1 m más acá sí.
    const StageChunk& tile = chunks.tiles().front();
    const double mid_z = 0.5 * (tile.min_z + tile.max_z);
    std::vector<int> selected;
    selected.reserve(chunks.tiles().size());
    chunks.select_tiles(tile.max_x + kStageDrawDistance - 0.1, mid_z, kStageDrawDistance, selected);
    CHECK(std::count(selected.begin(), selected.end(), 0) == 1);
    chunks.select_tiles(tile.max_x + kStageDrawDistance + 0.1, mid_z, kStageDrawDistance, selected);
    CHECK(std::count(selected.begin(), selected.end(), 0) == 0);
    // Un trozo del núcleo: igual.
    const StageChunk& core = chunks.core().front();
    const double core_z = 0.5 * (core.min_z + core.max_z);
    chunks.select_core(core.min_x - kStageDrawDistance + 0.1, core_z, kStageDrawDistance, selected);
    CHECK(std::count(selected.begin(), selected.end(), 0) == 1);
    chunks.select_core(core.min_x - kStageDrawDistance - 0.1, core_z, kStageDrawDistance, selected);
    CHECK(std::count(selected.begin(), selected.end(), 0) == 0);
    MESSAGE("distancia de dibujo ", kStageDrawDistance, " m; ", chunks.core().size(),
            " trozos del núcleo (", kStageCoreChunkLength, " m de s) y ", chunks.tiles().size(),
            " baldosas");
}

TEST_CASE("stage_draw: StageSurface gives the ground of Jolt (measurement)") {
    const Fixture& f = fixture();
    sim::Pcg32 rng(0x13572468ull, 5);
    const double length = f.track.axis().length();
    int hit_mismatch_inside = 0;
    int hit_mismatch_edge = 0;
    int surface_mismatch = 0;
    int compared = 0;
    double worst_core = 0.0;
    double worst_terrain = 0.0;
    for (int n = 0; n < 3000; ++n) {
        const double s = length * unit(rng);
        const sim::StagePose pose = f.track.pose(s);
        const double t = -62.0 + 124.0 * unit(rng);
        const double x = pose.x + t * std::cos(pose.heading);
        const double z = pose.z + t * std::sin(pose.heading);
        const StageHit mine = f.surface->query(x, z);
        const sim::GroundProbe probe = f.world.probe_ground(x, z);
        if (mine.valid != probe.hit) {
            // La rejilla acaba entre 59,44 m (siempre hay suelo) y 60,5 m (nunca) del eje.
            if (std::abs(t) < 58.0) {
                ++hit_mismatch_inside;
            } else {
                ++hit_mismatch_edge;
            }
            continue;
        }
        if (!mine.valid) {
            continue;
        }
        ++compared;
        if (mine.surface != probe.surface) {
            ++surface_mismatch;
        }
        const double dy = std::abs(mine.y - probe.y);
        double& worst = mine.core ? worst_core : worst_terrain;
        worst = std::max(worst, dy);
    }
    MESSAGE(compared, " puntos con suelo en los dos; huecos distintos: ", hit_mismatch_inside,
            " a menos de 58 m del eje y ", hit_mismatch_edge,
            " en el borde de la franja; superficies distintas: ", surface_mismatch,
            "; diferencia de y máxima: ", 1000.0 * worst_core, " mm en el núcleo, ",
            1000.0 * worst_terrain, " mm en el terreno");
    CHECK(hit_mismatch_inside == 0);
    CHECK(compared > 2000);
}

TEST_CASE("stage_draw: the near grid height is the ground of Jolt plus the relief of the wheel "
          "(measurement)") {
    const Fixture& f = fixture();
    const StageNearSampler sampler(f.track, *f.surface, f.noise);
    const auto sample = [&](std::int64_t gx, std::int64_t gz) {
        return sampler.cell(gx, gz, kNearSpacing);
    };
    sim::Pcg32 rng(0x7a3f2026ull, 11);
    const double length = f.track.axis().length();
    double sum = 0.0;
    double worst = 0.0;
    double worst_s = 0.0;
    double worst_t = 0.0;
    double worst_relief = 0.0;
    double sum_query = 0.0;
    double worst_query = 0.0;
    int invalid = 0;
    constexpr int kPoints = 1000;
    for (int n = 0; n < kPoints; ++n) {
        const double s = length * unit(rng);
        const sim::StagePose pose = f.track.pose(s);
        const double t = -(pose.half_width_left + 1.0) +
                         (pose.half_width_left + pose.half_width_right + 2.0) * unit(rng);
        const double x = pose.x + t * std::cos(pose.heading);
        const double z = pose.z + t * std::sin(pose.heading);
        // El ojo, dentro de la zona donde h no se desvanece.
        const double angle = 6.283185307179586 * unit(rng);
        const double distance = kNearFadeStart * unit(rng);
        bool ok = false;
        const double drawn = near_drawn_height(sample, kNearSpacing, kNearRadius, kNearFadeStart,
                                               x + distance * std::cos(angle),
                                               z + distance * std::sin(angle), x, z, ok);
        const sim::GroundProbe probe = f.world.probe_ground(x, z);
        if (!ok || !probe.hit) {
            ++invalid;
            continue;
        }
        const sim::StageReliefFrame frame = sim::stage_relief_frame(f.track, s, x, z);
        const double relief = sim::stage_relief_height(frame, f.noise, frame.s, frame.t, x, z);
        const double error = std::abs(drawn - (probe.y + relief));
        sum += error;
        if (error > worst) {
            worst = error;
            worst_s = s;
            worst_t = t;
            worst_relief = relief;
        }
        const StageHit mine = f.surface->query(x, z);
        const double query_error = std::abs(drawn - (mine.y + relief));
        sum_query += query_error;
        worst_query = std::max(worst_query, query_error);
    }
    const int counted = kPoints - invalid;
    CHECK(invalid == 0);
    MESSAGE("altura dibujada frente a probe_ground + h de la rueda, ", counted, " puntos: media ",
            1000.0 * sum / counted, " mm, máximo ", 1000.0 * worst, " mm en s = ", worst_s,
            " m, t = ", worst_t, " m (h allí ", 1000.0 * worst_relief,
            " mm); frente a StageSurface + h: media ", 1000.0 * sum_query / counted, " mm, máximo ",
            1000.0 * worst_query, " mm");
}

TEST_CASE("stage_draw: the ring gives the same drawn height as the sampler and the vertex "
          "height is texel plus faded relief") {
    const Fixture& f = fixture();
    const StageNearSampler sampler(f.track, *f.surface, f.noise);
    NearRing ring(kNearRadius, kNearSpacing);
    const sim::StagePose pose = f.track.pose(400.0);
    const Vec3d eye{pose.x, pose.y + 1.1, pose.z};
    const auto direct = [&](double x, double z) { return sampler.at(x, z); };
    CHECK(ring.update(eye, direct) == ring.cells() * ring.cells());
    const auto from_ring = [&](std::int64_t gx, std::int64_t gz) { return ring.texel(gx, gz); };
    const auto from_sampler = [&](std::int64_t gx, std::int64_t gz) {
        return sampler.cell(gx, gz, kNearSpacing);
    };
    sim::Pcg32 rng(0x99ull, 2);
    int differences = 0;
    double worst_vertex = 0.0;
    for (int n = 0; n < 500; ++n) {
        const double x = eye.x + 40.0 * (unit(rng) - 0.5);
        const double z = eye.z + 40.0 * (unit(rng) - 0.5);
        bool ok_a = false;
        bool ok_b = false;
        const double a = near_drawn_height(from_ring, kNearSpacing, kNearRadius, kNearFadeStart,
                                           eye.x, eye.z, x, z, ok_a);
        const double b = near_drawn_height(from_sampler, kNearSpacing, kNearRadius, kNearFadeStart,
                                           eye.x, eye.z, x, z, ok_b);
        if (ok_a != ok_b || (ok_a && a != b)) {
            ++differences;
        }
        // En un vértice de la rejilla: texel + h × desvanecido.
        const auto gx = static_cast<std::int64_t>(std::floor(x / kNearSpacing));
        const auto gz = static_cast<std::int64_t>(std::floor(z / kNearSpacing));
        const double vx = static_cast<double>(gx) * kNearSpacing;
        const double vz = static_cast<double>(gz) * kNearSpacing;
        const NearTexel texel = from_sampler(gx, gz);
        bool ok_v = false;
        const double at_vertex = near_drawn_height(from_ring, kNearSpacing, kNearRadius,
                                                   kNearFadeStart, eye.x, eye.z, vx, vz, ok_v);
        if (ok_v) {
            const double fade =
                near_fade(std::hypot(vx - eye.x, vz - eye.z), kNearFadeStart, kNearRadius);
            worst_vertex = std::max(
                worst_vertex, std::abs(at_vertex - (static_cast<double>(texel.height) +
                                                    static_cast<double>(texel.micro) * fade)));
        }
    }
    CHECK(differences == 0);
    // Redondeo de la altura en float de la textura (las alturas de lazadas son de decenas de m).
    CHECK(worst_vertex <= 1e-4 * 1e-2);
}

TEST_CASE("stage_draw: the step between the near grid and the far mesh at the edge of the radius "
          "(measurement)") {
    const Fixture& f = fixture();
    const StageNearSampler sampler(f.track, *f.surface, f.noise);
    const auto sample = [&](std::int64_t gx, std::int64_t gz) {
        return sampler.cell(gx, gz, kNearSpacing);
    };
    sim::Pcg32 rng(0x51ab0de1ull, 3);
    const double length = f.track.axis().length();
    double worst = 0.0;
    double worst_x = 0.0;
    double worst_z = 0.0;
    double sum = 0.0;
    int counted = 0;
    constexpr int kEyes = 120;
    constexpr int kAngles = 72;
    for (int n = 0; n < kEyes; ++n) {
        const sim::StagePose pose = f.track.pose(length * unit(rng));
        for (int a = 0; a < kAngles; ++a) {
            const double angle = 6.283185307179586 * a / kAngles;
            const double x = pose.x + kNearRadius * std::cos(angle);
            const double z = pose.z + kNearRadius * std::sin(angle);
            bool ok = false;
            const double near_y = near_drawn_height(sample, kNearSpacing, kNearRadius,
                                                    kNearFadeStart, pose.x, pose.z, x, z, ok);
            const StageHit far_hit = f.surface->query(x, z);
            if (!ok || !far_hit.valid) {
                continue;
            }
            const double step = std::abs(near_y - far_hit.y);
            sum += step;
            ++counted;
            if (step > worst) {
                worst = step;
                worst_x = x;
                worst_z = z;
            }
        }
    }
    REQUIRE(counted > 1000);
    MESSAGE("salto entre lo cercano y lo lejano a ", kNearRadius, " m, ", counted,
            " puntos: media ", 1000.0 * sum / counted, " mm, máximo ", 1000.0 * worst, " mm en (",
            worst_x, ", ", worst_z, ")");
}

TEST_CASE("stage_draw: choosing chunks and moving the ring do not allocate") {
    const Fixture& f = fixture();
    const StageChunks chunks = StageChunks::build(f.ground);
    std::vector<int> core;
    std::vector<int> tiles;
    chunks.reserve_selection(core, tiles);
    const StageNearSampler sampler(f.track, *f.surface, f.noise);
    NearRing ring(kNearRadius, kNearSpacing);
    const auto direct = [&](double x, double z) { return sampler.at(x, z); };
    const sim::StagePose pose = f.track.pose(300.0);
    ring.update(Vec3d{pose.x, 0.0, pose.z}, direct); // primer llenado
    const std::uint64_t before = rally::tests::global_new_count();
    int cells = 0;
    for (int n = 1; n <= 200; ++n) {
        const double x = pose.x + 0.37 * n;
        const double z = pose.z + 0.11 * n;
        cells += ring.update(Vec3d{x, 0.0, z}, direct);
        chunks.select_core(x, z, kStageDrawDistance, core);
        chunks.select_tiles(x, z, kStageDrawDistance, tiles);
    }
    const std::uint64_t after = rally::tests::global_new_count();
    CHECK(after == before);
    CHECK(cells > 0);
    CHECK_FALSE(core.empty());
    CHECK_FALSE(tiles.empty());
}

TEST_CASE("stage_draw: the relief with the nearest s against the relief of the wheel "
          "(measurement)") {
    const Fixture& f = fixture();
    // El dibujo usa TestZone{} con los argumentos por defecto, como SimWorld::build.
    const sim::TestZone noise{};
    const StageNearSampler sampler(f.track, *f.surface, noise);
    sim::Pcg32 rng(0x2468aceull, 7);
    const double length = f.track.axis().length();
    int different = 0;
    double worst = 0.0;
    double worst_s = 0.0;
    double sum_sq = 0.0;
    constexpr int kPoints = 1000;
    for (int n = 0; n < kPoints; ++n) {
        // La mitad de los puntos en las dos horquillas (730–860 m), donde hay dos pasadas cerca.
        const double s = n % 2 == 0 ? 730.0 + 130.0 * unit(rng) : length * unit(rng);
        const sim::StagePose pose = f.track.pose(s);
        const double t = -(pose.half_width_left + 1.5) +
                         (pose.half_width_left + pose.half_width_right + 3.0) * unit(rng);
        const double x = pose.x + t * std::cos(pose.heading);
        const double z = pose.z + t * std::sin(pose.heading);
        const double wheel = sampler.relief(x, z, s);
        const double drawn = sampler.relief(x, z, f.track.acquire(x, z).s);
        sum_sq += wheel * wheel;
        if (wheel != drawn) {
            ++different;
            if (std::abs(wheel - drawn) > worst) {
                worst = std::abs(wheel - drawn);
                worst_s = s;
            }
        }
    }
    MESSAGE("h con la s de la pasada del punto (la rueda) frente a la s más cercana (el dibujo): ",
            different, " de ", kPoints, " distintos, diferencia máxima ", 1000.0 * worst,
            " mm en s = ", worst_s, " m; rms de h ", 1000.0 * std::sqrt(sum_sq / kPoints), " mm");
    CHECK(sum_sq > 0.0);
}

TEST_CASE("stage_draw: the lines go at the start, every split and the finish") {
    const Fixture& f = fixture();
    // lazadas (1150 m): 1000 > 1150 − 200, sin parciales.
    const std::vector<double> lazadas = stage_line_positions(f.track.axis().length());
    REQUIRE(lazadas.size() == 2);
    CHECK(lazadas.front() == 0.0);
    CHECK(lazadas.back() == f.track.axis().length());
    // 3200 m: 1000, 2000 y 3000 (3000 no es > 3000).
    const std::vector<double> long_stage = stage_line_positions(3200.0);
    REQUIRE(long_stage.size() == 5);
    CHECK(long_stage[1] == 1000.0);
    CHECK(long_stage[2] == 2000.0);
    CHECK(long_stage[3] == 3000.0);
    CHECK(long_stage[4] == 3200.0);
    CHECK(stage_line_positions(3199.0).size() == 4); // 3000 > 2999: sin el tercero
    StageMeshBuffers buffers;
    build_stage_lines(f.track, *f.surface, f.noise, buffers);
    CHECK(buffers.triangle_count() > 0);
    CHECK(buffers.triangle_count() % 2 == 0);
}
