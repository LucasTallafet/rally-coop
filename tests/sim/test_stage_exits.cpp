#include "sim/jolt_runtime.h"
#include "stage_exits.h"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// Criterio 1a de formato-2 (lote 6; mensajes/lote-6-respuesta-formato-2.md §3 y la respuesta a la
// parada de la parte B, §1): las salidas guionizadas de common/stage_exits.h por el suelo del
// formato 2. El 1b salió (respuesta a la parada de la parte B, §2): la costura es el invariante 7b
// (test_stage_ground.cpp).

namespace {

using rally::tests::ExitBand;
using rally::tests::ExitStats;
using rally::tests::kExitPlaces;
using rally::tests::kExitSpeedsKmh;

const rally::sim::StageTrack& lazadas_track() {
    static const rally::sim::StageTrack track = [] {
        rally::sim::StageTrack t;
        const rally::sim::StageParseResult r =
            rally::sim::StageTrack::build(rally::tests::lazadas_stage_text(), t);
        RALLY_CHECK(r.ok, "test_stage_exits: lazadas no vale");
        return t;
    }();
    return track;
}

std::string band_line(const ExitBand& b) {
    char text[64];
    std::snprintf(text, sizeof(text), "%.2f %% / %.0f N", b.percent(), b.p99());
    return text;
}

// Las 8 salidas que deciden 1a: la recta y la curva de R 80 a 30, 60 y 90 km/h y las dos
// horquillas a 30 km/h (con el formato 1, la malla y su referencia densa salen por el mismo sitio).
bool decides_1a(std::size_t place, std::size_t speed) { return place < 2 || speed == 0; }

} // namespace

#ifdef NDEBUG
TEST_CASE("stage_exits: 1a the band against the dense band without micro relief") {
    // Criterio 1a: Design (40 tramos de cuneta, cuerda de 2 mm) frente a BandDense (la malla con 72
    // tramos y 0,5 mm sobre la misma rejilla de 0,75 m), los dos sin microrrelieve, por el
    // recorrido congelado. Las 8 salidas que deciden, sumadas por bandas: en 0–4 m y en 4–15 m, el
    // % sin contacto y el p99 del salto ≤ 1,10 × la densa. Premortem (§4): cada salida que decide
    // sale por el mismo sitio (el «más lejos» de las dos a ≤ 0,5 m y ninguna recolocada); si no,
    // pasa a diagnóstico, y con menos de 6, la premisa falla.
    const rally::sim::JoltRuntime runtime;
    const std::string& text = rally::tests::lazadas_stage_text();
    const rally::sim::StageTrack& track = lazadas_track();
    ExitStats band;
    ExitStats dense;
    int deciding = 0;
    for (std::size_t p = 0; p < kExitPlaces.size(); ++p) {
        for (std::size_t v = 0; v < kExitSpeedsKmh.size(); ++v) {
            const ExitStats a =
                rally::tests::run_exit(text, track, rally::sim::StageGroundVariant::Design,
                                       kExitPlaces[p], kExitSpeedsKmh[v], nullptr, false);
            const ExitStats b =
                rally::tests::run_exit(text, track, rally::sim::StageGroundVariant::BandDense,
                                       kExitPlaces[p], kExitSpeedsKmh[v], nullptr, false);
            const bool same_path =
                std::abs(a.farthest - b.farthest) <= 0.5 && !a.placed && !b.placed;
            const bool decides = decides_1a(p, v) && same_path;
            MESSAGE(std::string(kExitPlaces[p].name), " a ", kExitSpeedsKmh[v], " km/h: 0–4 m ",
                    band_line(a.near_band()), " (densa ", band_line(b.near_band()), "), 4–15 m ",
                    band_line(a.rest_band()), " (densa ", band_line(b.rest_band()), "), más lejos ",
                    a.farthest, " m (densa ", b.farthest, " m)",
                    decides ? ""
                            : (decides_1a(p, v) ? " — diverge: diagnóstico" : " — diagnóstico"));
            if (decides) {
                band.add(a);
                dense.add(b);
                ++deciding;
            }
        }
    }
    const ExitBand bn = band.near_band();
    const ExitBand dn = dense.near_band();
    const ExitBand br = band.rest_band();
    const ExitBand dr = dense.rest_band();
    MESSAGE("1a, ", deciding, " salidas juntas: 0–4 m ", band_line(bn), " (densa ", band_line(dn),
            "); 4–15 m ", band_line(br), " (densa ", band_line(dr), ")");
    REQUIRE(deciding >= 6);
    CHECK(bn.percent() <= 1.10 * dn.percent());
    CHECK(bn.p99() <= 1.10 * dn.p99());
    CHECK(br.percent() <= 1.10 * dr.percent());
    CHECK(br.p99() <= 1.10 * dr.p99());
}
#endif

TEST_CASE("stage_exits: the scripted exit runs the same twice") {
    // El recorrido es determinista (también en Debug): la misma salida da las mismas cifras.
    const rally::sim::JoltRuntime runtime;
    const std::string& text = rally::tests::lazadas_stage_text();
    const rally::sim::StageTrack& track = lazadas_track();
    rally::tests::ExitPlace place = kExitPlaces[0];
    place.seconds = 1.5;
    const ExitStats a =
        rally::tests::run_exit(text, track, rally::sim::StageGroundVariant::Design, place, 60.0);
    const ExitStats b =
        rally::tests::run_exit(text, track, rally::sim::StageGroundVariant::Design, place, 60.0);
    CHECK(a.farthest == b.farthest);
    CHECK(a.near_band().wheel_steps == b.near_band().wheel_steps);
    CHECK(a.near_band().no_contact == b.near_band().no_contact);
    CHECK(a.near_band().p99() == b.near_band().p99());
}
