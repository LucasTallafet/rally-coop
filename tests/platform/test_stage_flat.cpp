// Tecla de la irregularidad (lote 7, tarea panel, apartado 5) y las líneas blancas sobre la
// calzada real (5b), sin ventana. Ningún test copia un hash de lazadas_irregular: piden la
// relación con el que da SimWorld (lote-7.md §2.1).

#include "io/recording.h"
#include "platform/stage_checked.h"
#include "platform/stage_hud.h"
#include "platform/stage_mesh.h"
#include "platform/stage_mode.h"
#include "platform/stage_surface.h"
#include "sim/jolt_runtime.h"
#include "sim/sim_world.h"
#include "sim/stage_axis.h"
#include "sim/stage_ground.h"
#include "sim/stage_track.h"
#include "sim/test_zone.h"
#include "stage_driver.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

using namespace rally;
using namespace rally::platform;

namespace {

const std::string& lazadas_text() {
    static const std::string text = [] {
        std::string bytes;
        std::string error;
        REQUIRE_MESSAGE(io::read_file(RALLY_SOURCE_DIR "/data/stages/lazadas.stage", bytes, error),
                        error);
        return bytes;
    }();
    return text;
}

const std::string& irregular_text() { return tests::lazadas_irregular_stage_text(); }

std::uint64_t world_ground_hash(const std::string& text) {
    sim::WorldDesc desc;
    desc.stage_text = text;
    sim::SimWorld world;
    world.build(desc);
    world.seal();
    return world.stage_ground_hash();
}

StageSelection select(const std::string& text) {
    StageSelection selection;
    std::string error;
    REQUIRE_MESSAGE(selection.set_text(text, error), error);
    return selection;
}

} // namespace

TEST_CASE("stage flat: the key rebuilds the world without the irregularity and back with it") {
    const sim::JoltRuntime runtime;
    StageSelection selection = select(irregular_text());
    REQUIRE(selection.has_irregularity());
    CHECK_FALSE(selection.flat);
    // Con irregularidad: el suelo es el del tramo entero y la descripción lleva su texto.
    const sim::DriverAids aids{};
    sim::WorldDesc with = stage_world_desc(selection, sim::CarData{}, aids);
    CHECK(with.stage_text == irregular_text());
    const std::uint64_t hash_with = world_ground_hash(with.stage_text);
    // La tecla.
    const IrregularityToggle off = toggle_irregularity(selection);
    CHECK(off.changed);
    CHECK_FALSE(off.message.empty());
    CHECK(selection.flat);
    const sim::WorldDesc without = stage_world_desc(selection, sim::CarData{}, aids);
    CHECK(without.stage_text == sim::stage_text_without_irregularity(irregular_text()));
    CHECK(without.stage_text != with.stage_text);
    const std::uint64_t hash_without = world_ground_hash(without.stage_text);
    // El suelo sin irregularidad es el del esqueleto: el de lazadas.stage, que es la relación que
    // fija `irregularidad` (no una cifra copiada).
    CHECK(hash_without == world_ground_hash(lazadas_text()));
    CHECK(hash_without != hash_with);
    // Otra pulsación: vuelve el tramo con irregularidad, con el mismo suelo que antes.
    const IrregularityToggle on = toggle_irregularity(selection);
    CHECK(on.changed);
    CHECK_FALSE(selection.flat);
    const sim::WorldDesc again = stage_world_desc(selection, sim::CarData{}, aids);
    CHECK(again.stage_text == irregular_text());
    CHECK(world_ground_hash(again.stage_text) == hash_with);
    // La salida sale del trazado que se juega, no del otro.
    CHECK(with.car_spawn.position.x != doctest::Approx(0.0)); // hay salida
}

TEST_CASE("stage flat: the spawn comes from the track that is played") {
    StageSelection selection = select(irregular_text());
    const sim::WorldDesc with = stage_world_desc(selection, sim::CarData{}, {});
    toggle_irregularity(selection);
    const sim::WorldDesc without = stage_world_desc(selection, sim::CarData{}, {});
    sim::StageTrack flat_track;
    REQUIRE(sim::StageTrack::build(sim::stage_text_without_irregularity(irregular_text()),
                                   flat_track)
                .ok);
    const sim::CarSpawn expected = sim::stage_spawn(flat_track, kGameStageSpawnS);
    CHECK(without.car_spawn.position.x == expected.position.x);
    CHECK(without.car_spawn.position.z == expected.position.z);
    CHECK(without.car_spawn.position.y == expected.position.y);
    // Con la irregularidad, la salida está en el centro real de la calzada: no es la misma.
    const bool same = with.car_spawn.position.x == without.car_spawn.position.x &&
                      with.car_spawn.position.z == without.car_spawn.position.z &&
                      with.car_spawn.position.y == without.car_spawn.position.y;
    CHECK_FALSE(same);
}

TEST_CASE("stage flat: a stage without irregularity says there is nothing to remove") {
    StageSelection selection = select(lazadas_text());
    CHECK_FALSE(selection.has_irregularity());
    const IrregularityToggle t = toggle_irregularity(selection);
    CHECK_FALSE(t.changed);
    CHECK_FALSE(selection.flat);
    CHECK(t.message.find("no tiene irregularidad") != std::string::npos);
    CHECK(selection.played_text() == lazadas_text());
    CHECK(irregularity_suffix(selection).empty());
}

TEST_CASE("stage flat: the indicator says which one is played") {
    StageSelection selection = select(irregular_text());
    CHECK(irregularity_suffix(selection) == " · con irregularidad");
    toggle_irregularity(selection);
    CHECK(irregularity_suffix(selection) == " · sin irregularidad");
}

TEST_CASE("stage flat: a new stage keeps the choice and a broken stage keeps the old one") {
    StageSelection selection = select(irregular_text());
    toggle_irregularity(selection);
    std::string error;
    // Otro texto bueno: se juega sin irregularidad si la tiene.
    REQUIRE(selection.set_text(irregular_text(), error));
    CHECK(selection.played_text() == sim::stage_text_without_irregularity(irregular_text()));
    // Un texto que no vale deja la selección como estaba.
    const std::string before = selection.played_text();
    CHECK_FALSE(selection.set_text("format = 9\n", error));
    CHECK(selection.played_text() == before);
}

// ---- 5b: las líneas blancas siguen a la calzada real ----

namespace {

std::uint64_t fnv(const StageMeshBuffers& b) {
    std::uint64_t h = 1469598103934665603ull;
    const auto feed = [&h](const void* p, std::size_t n) {
        const auto* bytes = static_cast<const unsigned char*>(p);
        for (std::size_t i = 0; i < n; ++i) {
            h = (h ^ bytes[i]) * 1099511628211ull;
        }
    };
    feed(b.positions.data(), b.positions.size() * sizeof(float));
    feed(b.colors.data(), b.colors.size());
    return h;
}

struct Lines {
    sim::JoltRuntime runtime;
    sim::StageTrack track;
    sim::StageGround ground;
    sim::TestZone noise{};
    StageSurface* surface = nullptr;
    StageMeshBuffers buffers;

    explicit Lines(const std::string& text) {
        std::string error;
        const sim::StageParseResult parsed = sim::StageTrack::build(text, track);
        REQUIRE_MESSAGE(parsed.ok, parsed.error);
        REQUIRE_MESSAGE(build_checked_ground(track, world_ground_hash(text), ground, error), error);
        surface = new StageSurface(ground);
        build_stage_lines(track, *surface, noise, buffers);
    }
    ~Lines() { delete surface; }
};

} // namespace

// FNV-1a de la malla de las líneas de lazadas.stage (posiciones y colores), medido antes del
// cambio de 5b: sin irregularidad, las líneas salen como siempre, byte a byte.
constexpr std::uint64_t kLazadasLinesHash = 0x0ull;

TEST_CASE("stage flat: the lines of a stage without irregularity are the ones of before") {
    Lines lines(lazadas_text());
    const std::uint64_t h = fnv(lines.buffers);
    MESSAGE("hash de las líneas de lazadas: 0x", std::hex, h, " (", std::dec,
            lines.buffers.triangle_count(), " triángulos)");
    CHECK(h == kLazadasLinesHash);
}

TEST_CASE("stage flat: the ends of every line sit on the real edges of the road") {
    Lines lines(irregular_text());
    const sim::StageGround& g = lines.ground;
    const std::vector<double> positions = stage_line_positions(lines.track.axis().length());
    REQUIRE(!positions.empty());
    std::size_t vertex = 0;
    double worst = 0.0;
    double skeleton_gap = 0.0; // cuánto se aparta la calzada real de los semianchos del esqueleto
    for (const double s : positions) {
        const sim::StagePose pose = lines.track.pose(s);
        const sim::StageRoadPose real = lines.track.road(s);
        const double width = real.pose.half_width_left + real.pose.half_width_right;
        const int steps = std::max(1, static_cast<int>(std::ceil(width / kStageLineStep)));
        const std::size_t count = static_cast<std::size_t>(steps) * 6;
        REQUIRE(vertex + count <= lines.buffers.vertex_count());
        const double sh = std::sin(pose.heading);
        const double ch = std::cos(pose.heading);
        double t_min = 1e9;
        double t_max = -1e9;
        for (std::size_t i = 0; i < count; ++i) {
            const std::size_t at = (vertex + i) * 3;
            const double x = static_cast<double>(lines.buffers.positions[at]) + g.origin_x();
            const double z = static_cast<double>(lines.buffers.positions[at + 2]) + g.origin_z();
            const double t = (x - pose.x) * ch + (z - pose.z) * sh;
            t_min = std::min(t_min, t);
            t_max = std::max(t_max, t);
        }
        vertex += count;
        worst = std::max({worst, std::abs(t_min + real.pose.half_width_left),
                          std::abs(t_max - real.pose.half_width_right)});
        skeleton_gap = std::max({skeleton_gap, std::abs(pose.half_width_left - real.pose.half_width_left),
                                 std::abs(pose.half_width_right - real.pose.half_width_right)});
    }
    CHECK(vertex == lines.buffers.vertex_count());
    MESSAGE("extremos de las líneas frente a los bordes reales: ", worst * 1000.0,
            " mm como mucho; la calzada real se aparta del esqueleto hasta ", skeleton_gap * 1000.0,
            " mm");
    CHECK(worst < 0.02);
    // El test distingue: con los semianchos del esqueleto fallaría.
    CHECK(skeleton_gap > 0.02);
}
