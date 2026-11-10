#include "band_report.h"
#include "sim/stage_ground.h"
#include "sim/stage_track.h"
#include "stage_driver.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <string>

// Prototipo de la banda de entorno del formato 2 (tarea formato-2, lote 6, parte A;
// mensajes/formato-2-plan.md): StageGroundKind::Band, que aún no es el suelo del juego. Las
// premisas P1, P2 y P3 y el criterio 9 sobre la geometría, sin Jolt.

using rally::sim::StageGround;
using rally::sim::StageTrack;

namespace {

#ifdef NDEBUG
constexpr bool kFull = true;
#else
constexpr bool kFull = false;
#endif

StageTrack load(const std::string& text) {
    StageTrack track;
    const rally::sim::StageParseResult r = StageTrack::build(text, track);
    INFO(r.error);
    REQUIRE(r.ok);
    return track;
}

StageGround band(const StageTrack& track) {
    return StageGround::build(track, rally::sim::kStageCell, rally::sim::StageGroundKind::Band);
}

void check_faces(const char* name, const std::string& text) {
    const StageTrack track = load(text);
    const StageGround g = band(track);
    const rally::tests::BandGeometry b = rally::tests::band_geometry(g);
    MESSAGE(std::string(name), ": ", g.sections().size(), " secciones, ", b.triangles,
            " triángulos; ", b.inverted, " invertidos, ", b.zero_area, " sin área (la menor ",
            b.min_area, " m²), la más empinada ", b.steepest, "°; abanicos ", b.fan_sides,
            ", enfrentados ", b.meet_sides, ", en rampa ", b.ramp_sides, ", recortados ",
            b.clamped_sides, "; pies de más ", g.band_feet_overflows(), "; cuerda pendiente ",
            g.band_refine_pending());
    CHECK(g.band_error().empty());
    CHECK(g.band_bad_triangles() == 0);
    CHECK(b.inverted == 0);
    CHECK(b.zero_area == 0);
    CHECK(b.over_80 == 0);
    CHECK(b.slope_over_material == 0); // las más empinadas que la tierra son muro
    CHECK(b.clamped_sides == 0);
    CHECK(g.band_feet_overflows() == 0);
    CHECK(g.band_refine_pending() == 0);
}

} // namespace

TEST_CASE("stage_band: no band face is inverted, flat or steeper than 80 degrees") {
    check_faces("lazadas", rally::tests::lazadas_stage_text());
    check_faces("hairpin_flat", rally::tests::stage_file("hairpin_flat.stage"));
    check_faces("curva de R 6", rally::tests::tight_corner_stage());
    check_faces("peralte 10 %", rally::tests::max_bank_stage());
    check_faces("horquilla apilada al 3 %", rally::tests::stacked_stage(0.03));
    if constexpr (kFull) {
        check_faces("sintético 30 km", rally::tests::synthetic_stage_30km());
    }
}

TEST_CASE("stage_band: the ditch starts at the shoulder edge vertex of the core") {
    // Criterio 4 (sin escalón arcén–cuneta): el primer tramo de la cuneta usa el mismo índice de
    // vértice que el borde del arcén del núcleo, a los dos lados de cada sección.
    const StageTrack track = load(rally::tests::lazadas_stage_text());
    const StageGround g = band(track);
    const std::size_t per = g.vertices_per_section();
    const std::size_t n = (per - 7) / 2;
    int shared = 0;
    for (const rally::sim::StageTriangle& t : g.triangles()) {
        if (t.look != rally::sim::StageLook::Ditch && t.look != rally::sim::StageLook::Wall) {
            continue;
        }
        for (const std::uint32_t v : {t.a, t.b, t.c}) {
            const std::size_t column = v % per;
            if (column == n + 1 || column == n + 5) {
                ++shared; // E del núcleo
            }
        }
    }
    MESSAGE(shared, " esquinas de la cuneta en el vértice del borde del arcén");
    // Cada tramo entre secciones tiene, a cada lado, dos triángulos de cuneta con E.
    CHECK(shared >= static_cast<int>(3 * 2 * (g.sections().size() - 1)));
}

TEST_CASE("stage_band: no band face is above the road or the shoulder") {
    // Criterio 3 (P3): 10 000 rayos verticales en los núcleos, ninguna cara de la banda encima.
    for (const bool hairpin : {false, true}) {
        const StageTrack track = load(hairpin ? rally::tests::stage_file("hairpin_flat.stage")
                                              : rally::tests::lazadas_stage_text());
        const StageGround g = band(track);
        double above = 0.0;
        const int wrong = rally::tests::core_rays_wrong(g, track, kFull ? 10000 : 1000, above);
        MESSAGE(std::string(hairpin ? "hairpin_flat" : "lazadas"), ": ", wrong,
                " rayos con la banda encima del núcleo; lo más alto, ",
                1000.0 * std::max(above, -1.0),
                " mm (−1000 si ninguna cara de la banda cubre los puntos)");
        CHECK(wrong == 0);
    }
}

TEST_CASE("stage_band: the wall between the arms of lazadas stays under 80 degrees") {
    // P1: la pendiente de la tira de la cabeza al eje medio, sección a sección; la mayor, con su
    // s (la medida de la parte A: 77,92° en s ≈ 657,75 y 830,71, entre el brazo de entrada y el
    // central).
    const StageTrack track = load(rally::tests::lazadas_stage_text());
    const StageGround g = band(track);
    const rally::tests::BandGeometry b = rally::tests::band_geometry(g);
    const double degrees = std::atan(b.worst.slope) * 180.0 / 3.14159265358979323846;
    MESSAGE("muro más empinado: ", degrees, "° en s = ", b.worst.s, " (lado ", b.worst.side,
            "), frente a s = ", b.worst.other_s);
    CHECK(b.worst.slope <= rally::sim::kStageWallMaxSlope);
    CHECK(b.walls > 0);
}

TEST_CASE("stage_band: two passes that need a wall over 80 degrees do not fit and the error has "
          "its s") {
    // Criterio 9: al 10 % entre brazos de una horquilla de R 6 con 2 m entre cabezas de cuneta, el
    // muro pasa de 80°; al 3 %, cabe.
    const StageTrack steep = load(rally::tests::stacked_stage(0.10));
    const std::string error = rally::sim::stage_band_error(steep);
    MESSAGE("al 10 %: ", error);
    CHECK(error.rfind("s = ", 0) == 0);
    CHECK(error.find("no cabe el talud entre pasadas") != std::string::npos);
    const StageTrack gentle = load(rally::tests::stacked_stage(0.03));
    CHECK(rally::sim::stage_band_error(gentle).empty());
    // Dos núcleos a 2,9 m (hoy valen: validación de 1,5 m): la cuneta no cabe.
    const std::string plan = "straight 100\narc " +
                             rally::tests::stage_number(10.0 * 3.14159265358979323846) +
                             " 10\narc 30 100\narc 30 -100\nstraight 20\narc " +
                             rally::tests::stage_number(15.0 * 3.14159265358979323846) + " -30\n";
    const double length =
        100.0 + 10.0 * 3.14159265358979323846 + 60.0 + 20.0 + 15.0 * 3.14159265358979323846;
    const StageTrack close = load(rally::tests::simple_stage(plan, length));
    const std::string close_error = rally::sim::stage_band_error(close);
    MESSAGE("núcleos a 2,9 m: ", close_error);
    CHECK(close_error.find("no cabe el talud entre pasadas") != std::string::npos);
}

TEST_CASE("stage_band: other_feet sees the other arm and counts what does not fit") {
    // Entre el brazo de entrada y el central de lazadas (s 700 y su enfrente): un pie de otra
    // parte; con capacidad 0, se cuenta sin escribir.
    const StageTrack track = load(rally::tests::lazadas_stage_text());
    const rally::sim::StagePose p = track.pose(700.0);
    const double t = p.half_width_right + rally::sim::kStageShoulderWidth + 3.0;
    const double x = p.x + t * std::cos(p.heading);
    const double z = p.z + t * std::sin(p.heading);
    rally::sim::StageProjection feet[4];
    const int count = track.other_feet(x, z, 700.0, 1.0, feet, 4);
    REQUIRE(count >= 1);
    bool central = false;
    for (int k = 0; k < count && k < 4; ++k) {
        central = central || (feet[k].s > 760.0 && feet[k].s < 830.0);
        CHECK(std::abs(feet[k].s - 700.0) > 1.0);
    }
    CHECK(central);
    CHECK(track.other_feet(x, z, 700.0, 1.0, feet, 0) == count);
}
