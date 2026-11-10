#include "game/direction_changes.h"
#include "new_counter.h"
#include "sim/stage_axis.h"
#include "stage_driver.h"

#include <doctest/doctest.h>

#include <bit>
#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

using namespace rally::game;
using rally::sim::StageAxis;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;

StageAxis load(const std::string& text) {
    StageAxis axis;
    const rally::sim::StageParseResult r = StageAxis::parse(text, axis);
    INFO(r.error);
    REQUIRE(r.ok);
    return axis;
}

// Tramo mínimo: solo la planta (el alzado y lo demás, los justos para que parsee).
std::string stage_with_plan(const std::string& plan, double length) {
    return "format = 1\nname = cambios\nstart_x = 0\nstart_z = 0\nstart_heading_deg = 0\n"
           "start_height = 0\n[plan]\n" +
           plan + "[profile]\npvi 0 0 0\npvi " + rally::tests::stage_number(length) +
           " 0 0\n[bank]\nbank 0 0\n[width]\nwidth 0 3 3\n[surface]\nsurface 0 gravel\n";
}

bool same_bits(double a, double b) {
    return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}

} // namespace

TEST_CASE("direction changes: lazadas gives the six changes of the table") {
    const StageAxis axis = load(rally::tests::lazadas_stage_text());
    const std::vector<DirectionChange> c = direction_changes(axis);
    struct Row {
        double s_start;
        double s_end;
        double deg;
    };
    // Prompt de la tarea, criterio 1 (+ derecha); s_end de [plan].
    const Row table[] = {{100.0, 250.0, 78.8},   {390.0, 520.0, -121.8},   {520.0, 650.0, 121.8},
                         {730.0, 758.85, 180.0}, {828.85, 857.70, -180.0}, {907.70, 1027.70, 51.6}};
    REQUIRE(c.size() == 6);
    for (std::size_t i = 0; i < 6; ++i) {
        INFO("cambio ", i + 1, ": s_start ", c[i].s_start, ", ángulo ", c[i].angle / kDeg);
        CHECK(std::abs(c[i].s_start - table[i].s_start) <= 1.0);
        CHECK(std::abs(c[i].s_end - table[i].s_end) <= 1.0);
        CHECK(std::abs(c[i].angle / kDeg - table[i].deg) <= 0.5);
        // Premisa: coincide con el rumbo y la curvatura de pose().
        CHECK(std::abs((axis.pose(c[i].s_end).heading - axis.pose(c[i].s_start).heading) -
                       c[i].angle) <= 1e-9);
        CHECK((axis.pose(0.5 * (c[i].s_start + c[i].s_end)).curvature > 0.0) == (c[i].angle > 0.0));
        if (i > 0) {
            CHECK(c[i].s_start >= c[i - 1].s_end); // ordenados y sin solapes
        }
    }
    MESSAGE("lazadas: s_start/s_end/ángulo (°): ", c[0].s_start, "/", c[0].s_end, "/",
            c[0].angle / kDeg, "; ", c[1].s_start, "/", c[1].s_end, "/", c[1].angle / kDeg, "; ",
            c[2].s_start, "/", c[2].s_end, "/", c[2].angle / kDeg, "; ", c[3].s_start, "/",
            c[3].s_end, "/", c[3].angle / kDeg, "; ", c[4].s_start, "/", c[4].s_end, "/",
            c[4].angle / kDeg, "; ", c[5].s_start, "/", c[5].s_end, "/", c[5].angle / kDeg);
}

TEST_CASE("direction changes: the 5 degree threshold keeps only the 5.5 degree curve") {
    // Arcos de R 1000: L = R · Δθ. La recta de en medio los separa.
    const std::string plan = "straight 50\narc " + rally::tests::stage_number(1000.0 * 4.5 * kDeg) +
                             " 1000\nstraight 50\narc " +
                             rally::tests::stage_number(1000.0 * 5.5 * kDeg) +
                             " 1000\nstraight 50\n";
    const std::vector<DirectionChange> c =
        direction_changes(load(stage_with_plan(plan, 150.0 + 1000.0 * 10.0 * kDeg)));
    REQUIRE(c.size() == 1);
    CHECK(c[0].angle / kDeg == doctest::Approx(5.5).epsilon(1e-9));
    CHECK(c[0].s_start == doctest::Approx(50.0 + 1000.0 * 4.5 * kDeg + 50.0));
}

TEST_CASE("direction changes: a straight between two same-sense arcs separates them") {
    const std::string arc = "arc 100 100\n";
    const std::vector<DirectionChange> split = direction_changes(load(
        stage_with_plan("straight 10\n" + arc + "straight 20\n" + arc + "straight 10\n", 240.0)));
    REQUIRE(split.size() == 2);
    CHECK(split[0].angle == doctest::Approx(1.0));
    CHECK(split[1].s_start == doctest::Approx(130.0));
    // Sin recta en medio es uno solo, el doble de largo.
    const std::vector<DirectionChange> joined = direction_changes(
        load(stage_with_plan("straight 10\n" + arc + arc + "straight 10\n", 220.0)));
    REQUIRE(joined.size() == 1);
    CHECK(joined[0].angle == doctest::Approx(2.0));
    // Tramo que empieza y acaba en curva: el cambio abierto se cierra al final.
    const StageAxis ends = load(stage_with_plan(arc + "straight 20\n" + arc, 220.0));
    const std::vector<DirectionChange> edge = direction_changes(ends);
    REQUIRE(edge.size() == 2);
    CHECK(edge[0].s_start == 0.0);
    CHECK(edge[1].s_end == doctest::Approx(ends.length()));
    // Un cambio de sentido sin recta, con clotoide de inflexión, son dos.
    const std::vector<DirectionChange> flip = direction_changes(load(stage_with_plan(
        "straight 10\narc 100 100\nspiral 40 100 -100\narc 100 -100\nstraight 10\n", 260.0)));
    REQUIRE(flip.size() == 2);
    CHECK(flip[0].angle > 0.0);
    CHECK(flip[1].angle < 0.0);
    CHECK(flip[0].s_end == doctest::Approx(130.0));
    CHECK(flip[1].s_start == doctest::Approx(130.0));
}

TEST_CASE("direction changes: next and previous at the ends and without allocations") {
    const std::vector<DirectionChange> c = {
        {100.0, 150.0, 0.5}, {390.0, 450.0, -2.0}, {520.0, 600.0, 2.0}};
    const std::vector<DirectionChange> none;
    const std::uint64_t before = rally::tests::global_new_count();
    CHECK(next_direction_change(c, -5.0) == 0);
    CHECK(previous_direction_change(c, -5.0) == -1);
    CHECK(next_direction_change(c, 100.0) == 1); // justo en un s_start: estricto
    CHECK(previous_direction_change(c, 100.0) == -1);
    CHECK(previous_direction_change(c, 100.5) == 0);
    CHECK(next_direction_change(c, 250.0) == 1);
    CHECK(previous_direction_change(c, 250.0) == 0);
    CHECK(next_direction_change(c, 390.0) == 2);
    CHECK(previous_direction_change(c, 390.0) == 0);
    CHECK(next_direction_change(c, 520.0) == -1);
    CHECK(previous_direction_change(c, 520.0) == 1);
    CHECK(next_direction_change(c, 9999.0) == -1);
    CHECK(previous_direction_change(c, 9999.0) == 2);
    CHECK(next_direction_change(none, 0.0) == -1);
    CHECK(previous_direction_change(none, 0.0) == -1);
    CHECK(rally::tests::global_new_count() == before);
}

TEST_CASE("direction changes: same result twice and cost at load") {
    const StageAxis lazadas = load(rally::tests::lazadas_stage_text());
    const StageAxis big = load(rally::tests::synthetic_stage_30km());
    for (const StageAxis* axis : {&lazadas, &big}) {
        const auto t0 = std::chrono::steady_clock::now(); // test: el reloj solo mide el coste
        const std::vector<DirectionChange> a = direction_changes(*axis);
        const auto t1 = std::chrono::steady_clock::now();
        const std::vector<DirectionChange> b = direction_changes(*axis);
        MESSAGE(std::string(axis == &lazadas ? "lazadas" : "sintético 30 km"), ": ", a.size(),
                " cambios en ", std::chrono::duration<double, std::milli>(t1 - t0).count(), " ms");
        REQUIRE(a.size() == b.size());
        REQUIRE(!a.empty());
        for (const DirectionChange& d : a) {
            CHECK(std::abs((axis->pose(d.s_end).heading - axis->pose(d.s_start).heading) -
                           d.angle) <= 1e-9);
        }
        if (axis == &big) {
            // Una línea `spiral L inf R` por curva (los tramos de menos de 5° no se escriben):
            // un cambio por línea, con el signo de su R.
            const std::string text = rally::tests::synthetic_stage_30km();
            std::vector<bool> right;
            for (std::size_t p = text.find("spiral "); p != std::string::npos;
                 p = text.find("spiral ", p + 1)) {
                const std::size_t eol = text.find('\n', p);
                const std::string line = text.substr(p, eol - p);
                const std::size_t inf = line.find(" inf ");
                if (inf != std::string::npos) {
                    right.push_back(line[inf + 5] != '-');
                }
            }
            REQUIRE(a.size() == right.size());
            for (std::size_t i = 0; i < a.size(); ++i) {
                CHECK((a[i].angle > 0.0) == right[i]);
            }
        }
        for (std::size_t i = 0; i < a.size(); ++i) {
            CHECK(same_bits(a[i].s_start, b[i].s_start));
            CHECK(same_bits(a[i].s_end, b[i].s_end));
            CHECK(same_bits(a[i].angle, b[i].angle));
            CHECK(a[i].s_end > a[i].s_start);
            CHECK(std::abs(a[i].angle) >= kDirectionChangeMinAngle);
            if (i > 0) {
                CHECK(a[i].s_start >= a[i - 1].s_end);
            }
        }
    }
}
