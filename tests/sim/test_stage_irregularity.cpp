#include "band_report.h"
#include "new_counter.h"
#include "sim/sim_world.h"
#include "sim/stage_axis.h"
#include "sim/stage_ground.h"
#include "sim/stage_irregularity.h"
#include "sim/stage_track.h"
#include "stage_driver.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// La capa de irregularidad del trazado (tarea irregularidad, lote 7; sim/stage_irregularity.h):
// el formato 2 y su sección [irregularity], la validación, la calzada real (StageTrack::road), la
// salida en el centro real, el apagado en curvas, el texto sin la irregularidad y la
// reproducibilidad del suelo de data/stages/lazadas_irregular.stage. Los criterios del mundo (1–4)
// están en test_stage_world.cpp y los del suelo de lazadas (1 y 5) en test_stage_ground.cpp.

using rally::sim::StageAxis;
using rally::sim::StageChannel;
using rally::sim::StageGround;
using rally::sim::StageParseResult;
using rally::sim::StageTrack;

namespace {

// StageGround::hash() de lazadas_irregular.stage (criterio 2): el mismo en Debug y en Release.
// Cambia si cambia la capa, el suelo del tramo o el archivo; solo lo copian los tests de tests/sim
// (lote-7.md §2.1).
constexpr std::uint64_t kExpectedIrregularGroundHash = 0x02a6'755f'b2ba'b28cull;
// StageAxis::hash() de lazadas.stage (formato 1): el de antes del formato 2 (el hash empieza por el
// formato leído, que es 1, igual que la constante de entonces).
constexpr std::uint64_t kExpectedLazadasAxisHash = 0x36ab'8f5f'b9da'df6aull;

std::string hex(std::uint64_t v) {
    char text[32];
    std::snprintf(text, sizeof(text), "0x%016llx", static_cast<unsigned long long>(v));
    return text;
}

StageTrack load(const std::string& text) {
    StageTrack track;
    const StageParseResult r = StageTrack::build(text, track);
    INFO(r.error);
    REQUIRE(r.ok);
    return track;
}

// Un tramo recto de 300 m (formato 1) para los casos del formato 2.
std::string straight_stage() { return rally::tests::simple_stage("straight 300\n", 300.0); }

std::string irregular(const std::string& body) {
    return rally::tests::with_irregularity(straight_stage(), body);
}

// Un tramo del formato 2 que no tiene que cargar: el error empieza por `prefix` y contiene `what`.
void fails(const std::string& text, const std::string& prefix, const std::string& what) {
    StageTrack track;
    const StageParseResult r = StageTrack::build(text, track);
    INFO(r.error);
    CHECK_FALSE(r.ok);
    CHECK(r.error.rfind(prefix, 0) == 0);
    CHECK(r.error.find(what) != std::string::npos);
}

int line_of(const std::string& text, const std::string& line) {
    const std::size_t at = text.find(line);
    REQUIRE(at != std::string::npos);
    return 1 + static_cast<int>(
                   std::count(text.begin(), text.begin() + static_cast<std::ptrdiff_t>(at), '\n'));
}

} // namespace

// --- El formato 2
// ---------------------------------------------------------------------------------

TEST_CASE("stage_irregularity: format 2 reads the section and keeps the bank in percent") {
    const std::string text =
        irregular("seed 18446744073709551615\naxis 0.4 40 120\nwidth 0.2 50 150\n"
                  "edge 0.1 4 8\nbank 1.5 30 150\nheight 0.05 20 60\nscale 0 1\nscale 100 0.5\n"
                  "scale 200 1.5\n");
    StageAxis axis;
    const StageParseResult r = StageAxis::parse(text, axis);
    INFO(r.error);
    REQUIRE(r.ok);
    const rally::sim::StageIrregularitySpec& spec = axis.irregularity();
    CHECK(axis.format() == 2);
    CHECK(spec.present);
    CHECK(spec.seed == 18446744073709551615ull);
    CHECK(spec.axis.amplitude == 0.4);
    CHECK(spec.width.wavelength_min == 50.0);
    CHECK(spec.edge.wavelength_max == 8.0);
    CHECK(spec.bank.amplitude == doctest::Approx(0.015).epsilon(1e-12));
    CHECK(spec.height.amplitude == 0.05);
    REQUIRE(spec.scale.size() == 3);
    CHECK(spec.scale[2].s == 200.0);
    CHECK(spec.scale[2].value == 1.5);
    // Sin la sección, un formato 2 carga y no tiene irregularidad; una componente que falta vale 0.
    StageAxis plain;
    REQUIRE(StageAxis::parse(rally::tests::with_irregularity(straight_stage(), ""), plain).ok);
    CHECK(plain.format() == 2);
    CHECK_FALSE(plain.irregularity().present);
    StageAxis partial;
    REQUIRE(StageAxis::parse(irregular("seed 1\nheight 0.05 20 60\n"), partial).ok);
    CHECK(partial.irregularity().axis.amplitude == 0.0);
}

TEST_CASE("stage_irregularity: every rule of the section fails with its line") {
    struct Case {
        const char* name;
        std::string body;
        std::string bad_line; // la línea que da el error
        const char* what;
    };
    const std::vector<Case> cases = {
        {"semilla que falta", "axis 0.4 40 120\n", "[irregularity]", "falta 'seed'"},
        {"semilla repetida", "seed 1\nseed 2\n", "seed 2", "'seed' repetida"},
        {"semilla mal escrita", "seed -1\n", "seed -1", "semilla mal escrita"},
        {"semilla con decimales", "seed 1.5\n", "seed 1.5", "semilla mal escrita"},
        {"componente repetida", "seed 1\nedge 0.1 4 8\nedge 0.2 4 8\n", "edge 0.2 4 8",
         "'edge' repetida"},
        {"amplitud negativa", "seed 1\naxis -0.1 40 120\n", "axis -0.1", "amplitud"},
        {"λmin menor que 1 m", "seed 1\nheight 0.05 0.5 60\n", "height 0.05 0.5",
         "λmin tiene que ser ≥ 1 m"},
        {"λmax menor que λmin", "seed 1\nwidth 0.2 60 40\n", "width 0.2 60 40",
         "λmax tiene que ser ≥ λmin"},
        {"número de valores", "seed 1\nbank 1 30\n", "bank 1 30", "lleva 3 valores"},
        {"palabra desconocida", "seed 1\nwave 1 2 3\n", "wave 1 2 3", "primitiva desconocida"},
        {"escala que no empieza en 0", "seed 1\nscale 10 1\n", "scale 10 1",
         "la primera 'scale' tiene que ir en s = 0"},
        {"escala que no crece", "seed 1\nscale 0 1\nscale 0 0.5\n", "scale 0 0.5",
         "tienen que crecer"},
        {"escala negativa", "seed 1\nscale 0 -1\n", "scale 0 -1", "≥ 0"},
        {"escala fuera del tramo", "seed 1\nscale 0 1\nscale 301 1\n", "scale 301",
         "fuera del tramo"},
    };
    for (const Case& c : cases) {
        INFO(c.name);
        const std::string text = irregular(c.body);
        StageAxis axis;
        const StageParseResult r = StageAxis::parse(text, axis);
        INFO(r.error);
        CHECK_FALSE(r.ok);
        const int line = c.bad_line == "[irregularity]"
                             ? line_of(text, "[irregularity]")
                             : line_of(text, std::string("\n") + c.bad_line) + 1;
        CHECK(r.error.rfind("línea " + std::to_string(line) + ":", 0) == 0);
        CHECK(r.error.find(c.what) != std::string::npos);
    }
}

TEST_CASE("stage_irregularity: the axis hash of format 1 is unchanged and format 2 hashes the "
          "section") {
    // Contrato (lote-7.md §2.1): StageAxis::hash() de un formato 1 da lo mismo que antes; en el
    // formato 2, los datos de la sección entran en el hash.
    StageAxis lazadas;
    REQUIRE(StageAxis::parse(rally::tests::lazadas_stage_text(), lazadas).ok);
    MESSAGE("hash del eje de lazadas.stage: ", hex(lazadas.hash()));
    CHECK(lazadas.hash() == kExpectedLazadasAxisHash);
    const auto hash_of = [](const std::string& text) {
        StageAxis axis;
        const StageParseResult r = StageAxis::parse(text, axis);
        INFO(r.error);
        REQUIRE(r.ok);
        return axis.hash();
    };
    const std::string body = "seed 5\naxis 0.4 40 120\nscale 0 1\nscale 100 0.5\n";
    const std::uint64_t base = hash_of(irregular(body));
    CHECK(base == hash_of(irregular(body)));
    CHECK(base != hash_of(rally::tests::with_irregularity(straight_stage(), "")));
    CHECK(hash_of(straight_stage()) !=
          hash_of(rally::tests::with_irregularity(straight_stage(), "")));
    for (const std::string& other :
         {std::string("seed 6\naxis 0.4 40 120\nscale 0 1\nscale 100 0.5\n"),
          std::string("seed 5\naxis 0.41 40 120\nscale 0 1\nscale 100 0.5\n"),
          std::string("seed 5\naxis 0.4 41 120\nscale 0 1\nscale 100 0.5\n"),
          std::string("seed 5\naxis 0.4 40 120\nscale 0 1\nscale 100 0.6\n"),
          std::string("seed 5\naxis 0.4 40 120\nscale 0 1\n")}) {
        CHECK(hash_of(irregular(other)) != base);
    }
}

// --- La validación ------------------------------------------------------------------------------

TEST_CASE("stage_irregularity: each limit stops the load with its s, the value and what to lower") {
    // Respuesta del consultor a la parte A, §1.4: lo medido × 1,02 frente al límite; el error dice
    // la s, lo medido, el límite y qué bajar.
    // Amplitudes de sobra (con las cuatro ondas de la misma λ, su suma depende de las fases).
    fails(irregular("seed 3\naxis 4 20 20\n"), "s = ", "la desviación del eje curva");
    fails(irregular("seed 3\naxis 4 20 20\n"), "s = ", "y el límite es 0.0100 m⁻¹: baja 'axis'");
    fails(irregular("seed 3\nheight 2 20 20\n"), "s = ", "la ondulación curva");
    fails(irregular("seed 3\nheight 2 20 20\n"), "s = ", "baja 'height'");
    fails(irregular("seed 3\nbank 50 5 5\n"), "s = ", "el peralte añadido cambia");
    fails(irregular("seed 3\nbank 50 5 5\n"), "s = ", "y el límite es 1.50 %/m: baja 'bank'");
    // Un centro que se aparta más que el semiancho con d'' por debajo del límite
    // (39·(2π/400)² < 0,01) y un arcén de menos de 0,5 m (el borde no tiene límite de curvatura).
    fails(irregular("seed 3\naxis 39 400 400\n"), "s = ", "queda fuera de la calzada");
    fails(irregular("seed 3\nedge 5 200 200\n"), "s = ", "(mínimo 0.50)");
    // Con poca amplitud, carga.
    StageTrack track;
    const StageParseResult r =
        StageTrack::build(irregular("seed 3\naxis 0.2 60 120\nheight 0.03 30 60\n"), track);
    INFO(r.error);
    CHECK(r.ok);
}

TEST_CASE("stage_irregularity: the limits of lazadas_irregular are met by what is measured") {
    // La validación de lazadas_irregular mide por debajo de los límites (premortem del §4 de la
    // respuesta del consultor: d y z, × 1,02); la cota analítica (las ondas en fase) se informa.
    const StageTrack track = load(rally::tests::lazadas_irregular_stage_text());
    const rally::sim::StageIrregularity& irr = track.irregularity();
    struct Row {
        StageChannel c;
        bool second;
        double limit;
        const char* name;
    };
    for (const Row& r :
         {Row{StageChannel::Axis, true, rally::sim::kStageAxisOffsetMaxCurvature, "|d''|"},
          Row{StageChannel::Height, true, rally::sim::kStageHeightMaxCurvature, "|z''|"},
          Row{StageChannel::Bank, false, rally::sim::kStageBankMaxRate, "|β'|"}}) {
        const double step = std::min(0.25, irr.wavelength_min(r.c) / 16.0);
        double worst = 0.0;
        for (double s = track.s_begin(); s <= track.s_end(); s += step) {
            const rally::sim::StageJet j = irr.jet(r.c, s);
            worst = std::max(worst, std::abs(r.second ? j.d2 : j.d1));
        }
        const rally::sim::StageChannelBound b = irr.bound(r.c, track.s_begin(), track.s_end());
        MESSAGE(r.name, ": medido ", worst, " (× 1,02: ", worst * 1.02, "), cota ",
                r.second ? b.d2 : b.d1, ", límite ", r.limit);
        CHECK(worst * rally::sim::kStageIrregularityProbeMargin <= r.limit);
    }
}

// --- La calzada real
// ------------------------------------------------------------------------------

TEST_CASE("stage_irregularity: without the layer the real road is the skeleton bit for bit") {
    // P1 en la calzada: con la sección a cero, road() y road_edges() dan los valores del esqueleto
    // con los mismos bits, en todo el tramo y en las prolongaciones.
    const StageTrack plain = load(rally::tests::lazadas_stage_text());
    const StageTrack zero =
        load(rally::tests::with_zero_irregularity(rally::tests::lazadas_stage_text()));
    int checked = 0;
    for (double s = plain.s_begin(); s <= plain.s_end(); s += 0.37) {
        const rally::sim::StagePose p = plain.pose(s);
        const rally::sim::StageRoadPose r = zero.road(s);
        CHECK(r.edges.center == 0.0);
        CHECK(r.pose.y == p.y);
        CHECK(r.pose.grade == p.grade);
        CHECK(r.pose.vertical_curvature == p.vertical_curvature);
        CHECK(r.pose.bank == p.bank);
        CHECK(r.center_heading == p.heading);
        CHECK(r.center_y == p.y);
        CHECK(r.center_grade == p.grade);
        for (const int side : {-1, 1}) {
            const double skeleton = (side > 0 ? p.half_width_right : p.half_width_left) +
                                    rally::sim::kStageShoulderWidth;
            CHECK(zero.core_half_width(s, side) == skeleton);
            CHECK(plain.core_half_width(s, side) == skeleton);
        }
        ++checked;
    }
    CHECK(checked > 3000);
}

TEST_CASE("stage_irregularity: on road uses the real outer edge of the core") {
    // Donde el borde real se aparta del del esqueleto más de 20 cm: 5 cm dentro del real, en
    // carretera; 5 cm fuera, no (y al revés con el del esqueleto, que ya no cuenta).
    const StageTrack track = load(rally::tests::lazadas_irregular_stage_text());
    double best_s = 0.0;
    int best_side = 1;
    double best_gap = 0.0;
    for (double s = 0.0; s <= 600.0; s += 0.5) {
        const rally::sim::StagePose p = track.pose(s);
        for (const int side : {-1, 1}) {
            const double skeleton = (side > 0 ? p.half_width_right : p.half_width_left) +
                                    rally::sim::kStageShoulderWidth;
            const double gap = track.core_half_width(s, side) - skeleton;
            if (std::abs(gap) > std::abs(best_gap)) {
                best_gap = gap;
                best_s = s;
                best_side = side;
            }
        }
    }
    MESSAGE("el borde real se aparta ", best_gap, " m del del esqueleto en s = ", best_s, " (lado ",
            best_side, ")");
    REQUIRE(std::abs(best_gap) > 0.2);
    const rally::sim::StagePose p = track.pose(best_s);
    const double outer = track.core_half_width(best_s, best_side);
    const auto at = [&](double t) {
        const double x = p.x + best_side * t * std::cos(p.heading);
        const double z = p.z + best_side * t * std::sin(p.heading);
        return track.project(x, z, best_s - 5.0, best_s + 5.0);
    };
    CHECK(at(outer - 0.05).on_road);
    CHECK_FALSE(at(outer + 0.05).on_road);
}

TEST_CASE("stage_irregularity: the car starts on the real centre line, along it and on its plane") {
    // Revisión del plan, cambio 3: stage_spawn en t = d, con el rumbo de la línea central
    // (rumbo + atan2(d', 1 − κ·d)) y a la altura del centro real.
    const StageTrack track = load(rally::tests::lazadas_irregular_stage_text());
    double worst_turn = 0.0;
    for (double s = 5.0; s <= 600.0; s += 7.3) {
        const rally::sim::CarSpawn spawn = rally::sim::stage_spawn(track, s);
        const rally::sim::StageRoadPose r = track.road(s);
        const rally::sim::StageProjection q = track.project(spawn.x, spawn.z, s - 1.0, s + 1.0);
        CHECK(std::abs(q.s - s) < 1e-6);
        CHECK(std::abs(q.t - r.edges.center) < 1e-6);
        CHECK(spawn.y == r.center_y);
        // El rumbo de la línea central, por diferencias de sus puntos.
        constexpr double h = 0.05;
        const rally::sim::CarSpawn a = rally::sim::stage_spawn(track, s - h);
        const rally::sim::CarSpawn b = rally::sim::stage_spawn(track, s + h);
        const double heading = std::atan2(b.x - a.x, -(b.z - a.z));
        CHECK(std::abs(rally::tests::wrap_angle(heading - r.center_heading)) < 1e-4);
        CHECK(spawn.yaw == static_cast<float>(-r.center_heading));
        worst_turn = std::max(worst_turn, std::abs(r.center_heading - r.pose.heading));
    }
    MESSAGE("la línea central llega a girar ", worst_turn * 180.0 / 3.14159265358979323846,
            "° respecto al eje");
    CHECK(worst_turn > 0.005);
}

TEST_CASE("stage_irregularity: in the tight curves the axis, the width and the edges do not move") {
    // Apagado en curvas (lote-7.md §8.2, ampliado al borde en la respuesta del consultor a la parte
    // A, §1.3): donde |κ| del esqueleto pasa de 1/30, d, ω y ε valen 0; el peralte y la ondulación
    // no se apagan.
    const std::string text = rally::tests::with_irregularity(
        rally::tests::lazadas_stage_text(),
        "seed 4\naxis 0.4 40 120\nwidth 0.2 40 150\nedge 0.1 4 8\nbank 1 30 150\n"
        "height 0.05 20 60\n");
    const StageTrack track = load(text);
    const rally::sim::StageIrregularity& irr = track.irregularity();
    REQUIRE_FALSE(irr.gate_intervals().empty());
    int inside = 0;
    double bank_or_height = 0.0;
    for (const auto& r : irr.gate_intervals()) {
        for (double s = r[0]; s <= r[1]; s += 0.25) {
            const auto v = irr.values(s);
            for (const StageChannel c :
                 {StageChannel::Axis, StageChannel::WidthLeft, StageChannel::WidthRight,
                  StageChannel::EdgeLeft, StageChannel::EdgeRight}) {
                CHECK(v[static_cast<std::size_t>(c)] == 0.0);
            }
            bank_or_height = std::max(bank_or_height,
                                      std::abs(v[static_cast<std::size_t>(StageChannel::Height)]));
            ++inside;
        }
    }
    MESSAGE(irr.gate_intervals().size(), " intervalos apagados, ", inside, " puntos");
    // Las dos horquillas de R 6 de lazadas.
    CHECK(irr.gate_intervals().size() == 2);
    CHECK(inside > 50);
    CHECK(bank_or_height > 0.0);
    // Lejos de ellas (más de 45 m), nada se apaga: G = 1.
    const auto far = irr.values(300.0);
    CHECK(far[static_cast<std::size_t>(StageChannel::Axis)] != 0.0);
}

TEST_CASE("stage_irregularity: the real road and its edges allocate nothing") {
    const StageTrack track = load(rally::tests::lazadas_irregular_stage_text());
    double sum = 0.0;
    const std::uint64_t before = rally::tests::global_new_count();
    for (int i = 0; i < 20000; ++i) {
        const double s = track.s_begin() + (track.s_end() - track.s_begin()) * i / 20000.0;
        const rally::sim::StagePose p = track.pose(s);
        sum += track.road_edges(p).outer(1) + track.road(s).center_y +
               track.irregularity().jet(StageChannel::Height, s).d2;
        sum += track.core_half_width(s, -1);
    }
    CHECK(rally::tests::global_new_count() == before);
    CHECK(sum != 0.0);
}

// --- El texto sin la irregularidad
// ----------------------------------------------------------------

TEST_CASE("stage_irregularity: the text without the layer drops only the section") {
    // Revisión del plan, cambio 9: la cabecera con las reglas del parser (espacios y comentario),
    // CRLF, la sección al final o en medio, y un «[irregularity]» dentro de un comentario, que se
    // queda.
    const std::string base = rally::tests::with_irregularity(straight_stage(), "");
    const std::string body = "seed 3\naxis 0.2 60 120\n";
    // Al final.
    const std::string at_end = base + "\n[irregularity]\n" + body;
    CHECK(rally::sim::stage_text_without_irregularity(at_end) == base + "\n");
    // Cabecera con espacios y comentario.
    const std::string spaced = base + "  [ irregularity ]  # la capa\r\n" + body;
    CHECK(rally::sim::stage_text_without_irregularity(spaced) == base);
    // En medio, con CRLF: la sección siguiente se queda.
    std::string crlf = "format = 2\r\nname = x\r\nstart_x = 0\r\nstart_z = 0\r\n"
                       "start_heading_deg = 0\r\nstart_height = 0\r\n[irregularity]\r\nseed 1\r\n"
                       "axis 0.2 60 120\r\n[plan]\r\nstraight 100\r\n[profile]\r\npvi 0 0 0\r\n"
                       "pvi 100 0 0\r\n[bank]\r\nbank 0 0\r\n[width]\r\nwidth 0 3 3\r\n"
                       "[surface]\r\nsurface 0 gravel\r\n";
    const std::string crlf_out = rally::sim::stage_text_without_irregularity(crlf);
    CHECK(crlf_out.find("irregularity") == std::string::npos);
    CHECK(crlf_out.find("seed") == std::string::npos);
    CHECK(crlf_out.find("[plan]\r\nstraight 100\r\n") != std::string::npos);
    StageAxis axis;
    const StageParseResult parsed = StageAxis::parse(crlf_out, axis);
    INFO(parsed.error);
    CHECK(parsed.ok);
    CHECK_FALSE(axis.irregularity().present);
    // Un «[irregularity]» en un comentario no es una cabecera.
    const std::string commented = base + "# [irregularity] va aquí\n";
    CHECK(rally::sim::stage_text_without_irregularity(commented) == commented);
    // Sin la sección, igual; y el de lazadas_irregular carga sin ella.
    CHECK(rally::sim::stage_text_without_irregularity(base) == base);
    StageAxis lazadas;
    REQUIRE(StageAxis::parse(rally::sim::stage_text_without_irregularity(
                                 rally::tests::lazadas_irregular_stage_text()),
                             lazadas)
                .ok);
    CHECK(lazadas.format() == 2);
    CHECK_FALSE(lazadas.irregularity().present);
}

// --- Criterio 2: el suelo de lazadas_irregular
// ------------------------------------------------------

TEST_CASE("stage_irregularity: the same seed gives the same ground and another seed another") {
    const std::string& text = rally::tests::lazadas_irregular_stage_text();
    const std::uint64_t first = StageGround::build(load(text)).hash();
    const std::uint64_t second = StageGround::build(load(text)).hash();
    std::string other = text;
    other.replace(other.find("seed 20261113"), 13, "seed 20261114");
    const std::uint64_t reseeded = StageGround::build(load(other)).hash();
    MESSAGE("suelo de lazadas_irregular: ", hex(first),
            " (kExpectedIrregularGroundHash); con otra "
            "semilla, ",
            hex(reseeded));
    CHECK(first == second);
    CHECK(first == kExpectedIrregularGroundHash);
    CHECK(reseeded != first);
}

// --- Criterio 8 (diagnóstico): el espectro de lo construido
// -----------------------------------------

TEST_CASE("stage_irregularity: what is built has the amplitudes and wavelengths of the file") {
    // En s 0–590 de lazadas_irregular (escala 1 y nada apagado), cada componente medida en los
    // vértices del núcleo de la malla: su pico entre la mitad y la amplitud pedida, y su longitud
    // de onda media (por los cruces por cero) dentro de lo pedido. Diagnóstico.
    const StageTrack track = load(rally::tests::lazadas_irregular_stage_text());
    const StageGround g = StageGround::build(track);
    const std::size_t per = g.vertices_per_section();
    const std::size_t n = (per - rally::sim::kStageCoreVertices - 2) / 2;
    struct Asked {
        const char* name;
        double amplitude, lambda_min, lambda_max;
    };
    const Asked asked[] = {{"eje", 0.45, 40, 120},        {"ancho izq.", 0.25, 40, 150},
                           {"ancho der.", 0.25, 40, 150}, {"borde izq.", 0.12, 4, 8},
                           {"borde der.", 0.12, 4, 8},    {"peralte", 0.015, 30, 150},
                           {"ondulación", 0.06, 20, 60}};
    std::vector<double> s_of;
    std::vector<std::array<double, 7>> values;
    for (std::size_t i = 0; i < g.sections().size(); ++i) {
        const double s = g.sections()[i];
        if (s < 0.0 || s > 590.0) {
            continue;
        }
        const rally::sim::StagePose p = track.pose(s);
        double t[5];
        double y[5];
        for (std::size_t j = 0; j < 5; ++j) {
            const rally::sim::StageVertex& v = g.vertices()[i * per + n + 1 + j];
            t[j] = (v.x + g.origin_x() - p.x) * std::cos(p.heading) +
                   (v.z + g.origin_z() - p.z) * std::sin(p.heading);
            y[j] = v.y + g.origin_y();
        }
        std::array<double, 7> o{};
        o[0] = t[2];
        o[1] = (t[2] - t[1]) - p.half_width_left;
        o[2] = (t[3] - t[2]) - p.half_width_right;
        o[3] = (t[1] - t[0]) - rally::sim::kStageShoulderWidth;
        o[4] = (t[4] - t[3]) - rally::sim::kStageShoulderWidth;
        o[5] = (y[3] - y[1]) / (t[3] - t[1]) - p.bank;
        o[6] = y[2] - (p.y + p.bank * t[2]);
        s_of.push_back(s);
        values.push_back(o);
    }
    REQUIRE(values.size() > 100);
    for (int c = 0; c < 7; ++c) {
        double peak = 0.0;
        double mean = 0.0;
        for (const auto& o : values) {
            peak = std::max(peak, std::abs(o[static_cast<std::size_t>(c)]));
            mean += o[static_cast<std::size_t>(c)];
        }
        mean /= static_cast<double>(values.size());
        int crossings = 0;
        double first = 0.0;
        double last = 0.0;
        for (std::size_t i = 1; i < values.size(); ++i) {
            if ((values[i - 1][static_cast<std::size_t>(c)] - mean) *
                    (values[i][static_cast<std::size_t>(c)] - mean) <
                0.0) {
                first = crossings == 0 ? s_of[i] : first;
                last = s_of[i];
                ++crossings;
            }
        }
        const double lambda = crossings > 1 ? 2.0 * (last - first) / (crossings - 1) : 0.0;
        const Asked& a = asked[c];
        MESSAGE(a.name, ": pico ", peak, " (pedido ", a.amplitude, "), λ media ", lambda,
                " m (pedido ", a.lambda_min, "–", a.lambda_max, ")");
        CHECK(peak <= a.amplitude * 1.01 + 1e-6);
        CHECK(peak >= 0.5 * a.amplitude);
        CHECK(lambda >= 0.8 * a.lambda_min);
        CHECK(lambda <= 1.2 * a.lambda_max);
    }
}
