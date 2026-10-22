#include "io/recording.h"
#include "new_counter.h"
#include "sim/fnv1a.h"
#include "sim/stage_axis.h"
#include "sim/zone_axis.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

// Formato de tramo 1 y geometría del eje (tarea tramo-eje, lote 3): sim/stage_axis.h.

using rally::sim::StageAxis;
using rally::sim::StageParseResult;
using rally::sim::StagePose;
using rally::sim::StagePrimitive;
using rally::sim::Surface;

namespace {

constexpr double kPi = 3.14159265358979323846;

// FNV-1a de 10 000 poses equidistantes de tests/sim/stages/lazadas.stage (todos los campos de
// StagePose). Cambia si cambia el tramo de test o la geometría del eje: se actualiza como la
// huella, con una línea en el commit.
constexpr std::uint64_t kExpectedStageHash = 0x5876'c61a'3090'19a0ull;

// Número con el menor número de cifras que se lee como el mismo double.
std::string num(double value) {
    std::array<char, 64> buffer{};
    const std::to_chars_result r =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    return std::string(buffer.data(), r.ptr);
}

// Un tramo con la planta `plan` (líneas de [plan]), de longitud `length`, llano y sin peralte,
// salvo que se den el alzado, el peralte, los anchos o las superficies.
struct StageText {
    std::string plan;
    double length = 0.0;
    double heading_deg = 0.0;
    std::string profile;
    std::string bank = "bank 0 0\n";
    std::string width = "width 0 3 3\n";
    std::string surface = "surface 0 gravel\n";
    double start_height = 0.0;

    std::string text() const {
        const std::string prof =
            profile.empty() ? "pvi 0 0 0\npvi " + num(length) + " 0 0\n" : profile;
        return "format = 1\nname = test\nstart_x = 0\nstart_z = 0\nstart_heading_deg = " +
               num(heading_deg) + "\nstart_height = " + num(start_height) + "\n[plan]\n" + plan +
               "[profile]\n" + prof + "[bank]\n" + bank + "[width]\n" + width + "[surface]\n" +
               surface;
    }
};

StageAxis load(const std::string& text) {
    StageAxis axis;
    const StageParseResult result = StageAxis::parse(text, axis);
    INFO(result.error);
    REQUIRE(result.ok);
    return axis;
}

const std::string& lazadas_text() {
    static const std::string text = [] {
        std::string bytes;
        std::string error;
        const bool ok =
            rally::io::read_file(RALLY_SOURCE_DIR "/tests/sim/stages/lazadas.stage", bytes, error);
        INFO(error);
        REQUIRE(ok);
        return bytes;
    }();
    return text;
}

bool same_bits(double a, double b) {
    return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}

bool same_pose(const StagePose& a, const StagePose& b) {
    return same_bits(a.s, b.s) && same_bits(a.x, b.x) && same_bits(a.y, b.y) &&
           same_bits(a.z, b.z) && same_bits(a.heading, b.heading) &&
           same_bits(a.curvature, b.curvature) && same_bits(a.grade, b.grade) &&
           same_bits(a.vertical_curvature, b.vertical_curvature) && same_bits(a.bank, b.bank) &&
           same_bits(a.half_width_left, b.half_width_left) &&
           same_bits(a.half_width_right, b.half_width_right) && a.surface == b.surface;
}

double distance(const StagePose& a, const StagePose& b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) +
                     (a.z - b.z) * (a.z - b.z));
}

// Rumbo de una clotoide en forma cerrada.
double closed_heading(double heading0, const StagePrimitive& p, double u) {
    return heading0 + p.curvature0 * u + (p.curvature1 - p.curvature0) * u * u / (2.0 * p.length);
}

} // namespace

// --- Criterio 1: rectas y arcos frente a AxisBuilder ---------------------------------------------

TEST_CASE("stage_axis: straights and arcs match AxisBuilder and segment_point") {
    StageText st;
    st.plan = "straight 100\narc 80 60\narc 50 -35\nstraight 40\n";
    st.length = 270.0;
    st.heading_deg = 30.0;
    const StageAxis axis = load(st.text());
    REQUIRE(axis.primitives().size() == 4);
    CHECK(axis.length() == 270.0);

    rally::sim::AxisBuilder builder(0.0, 0.0, 30.0 * (kPi / 180.0));
    builder.straight(100.0);
    builder.arc(60.0, 80.0 / 60.0);
    builder.arc(35.0, -50.0 / 35.0);
    builder.straight(40.0);
    const std::vector<rally::sim::AxisSegment> segments = builder.take();

    double max_position = 0.0;
    double max_heading = 0.0;
    for (int k = 0; k < 1000; ++k) {
        const double s = 270.0 * k / 999.0;
        std::size_t i = 0;
        while (i + 1 < axis.primitives().size() && axis.primitives()[i + 1].s0 <= s) {
            ++i;
        }
        const rally::sim::AxisPoint ref = rally::sim::segment_point(
            segments[i], std::min(s - axis.primitives()[i].s0, segments[i].length));
        const StagePose p = axis.pose(s);
        max_position = std::max(max_position, std::hypot(p.x - ref.x, p.z - ref.z));
        max_heading = std::max(max_heading, std::abs(p.heading - ref.psi));
    }
    MESSAGE("rectas y arcos: posición ", max_position, " m, rumbo ", max_heading, " rad");
    CHECK(max_position <= 1e-9);
    CHECK(max_heading <= 1e-12);
}

// --- Criterio 2: clotoides ---------------------------------------------------------------------

TEST_CASE("stage_axis: spirals follow the closed-form heading and a Simpson reference") {
    struct Case {
        std::string plan;
        double length;
    };
    for (const Case& c : {Case{"spiral 300 inf 30\n", 300.0}, Case{"spiral 120 -40 40\n", 120.0}}) {
        CAPTURE(c.plan);
        StageText st;
        st.plan = c.plan;
        st.length = c.length;
        const StageAxis axis = load(st.text());
        REQUIRE(axis.primitives().size() == 1);
        const StagePrimitive& prim = axis.primitives()[0];

        // Simpson compuesto de 200 000 intervalos; un punto de control cada 200.
        constexpr int kIntervals = 200'000;
        constexpr int kPoints = 1000;
        const double h = c.length / kIntervals;
        double x = 0.0;
        double z = 0.0;
        double max_position = 0.0;
        double max_heading = 0.0;
        for (int k = 1; k <= kPoints; ++k) {
            const int first = (k - 1) * (kIntervals / kPoints);
            for (int j = first; j < first + kIntervals / kPoints; j += 2) {
                const double a = closed_heading(0.0, prim, j * h);
                const double m = closed_heading(0.0, prim, (j + 1) * h);
                const double b = closed_heading(0.0, prim, (j + 2) * h);
                x += h / 3.0 * (std::sin(a) + 4.0 * std::sin(m) + std::sin(b));
                z -= h / 3.0 * (std::cos(a) + 4.0 * std::cos(m) + std::cos(b));
            }
            const double u = k * (kIntervals / kPoints) * h;
            const StagePose p = axis.pose(u);
            max_position = std::max(max_position, std::hypot(p.x - x, p.z - z));
            max_heading = std::max(max_heading, std::abs(p.heading - closed_heading(0.0, prim, u)));
        }
        MESSAGE(c.plan, ": posición ", max_position, " m, rumbo ", max_heading, " rad");
        CHECK(max_position <= 1e-3);
        CHECK(max_heading <= 1e-12);
    }
    // La curvatura pasa por 0 en la mitad de la clotoide de inflexión.
    StageText st;
    st.plan = "spiral 120 -40 40\n";
    st.length = 120.0;
    const StageAxis axis = load(st.text());
    CHECK(axis.pose(60.0).curvature == 0.0);
    CHECK(axis.pose(0.0).curvature == -1.0 / 40.0);
    CHECK(axis.pose(120.0).curvature == doctest::Approx(1.0 / 40.0).epsilon(1e-15));
}

// --- Criterio 3: juntas ------------------------------------------------------------------------

TEST_CASE("stage_axis: joints of the test stage are continuous and curvature jumps as written") {
    const StageAxis axis = load(lazadas_text());
    const std::vector<StagePrimitive>& prims = axis.primitives();
    REQUIRE(prims.size() == 23);
    double max_position = 0.0;
    double max_heading = 0.0;
    for (std::size_t i = 1; i < prims.size(); ++i) {
        CAPTURE(i);
        const double s0 = prims[i].s0;
        const StagePose before = axis.pose(s0 - 1e-9);
        const StagePose at = axis.pose(s0);
        max_position = std::max(max_position, distance(before, at));
        max_heading = std::max(max_heading, std::abs(at.heading - before.heading));
        // Justo en la junta, la primitiva nueva, con la curvatura del archivo.
        CHECK(same_bits(at.curvature, prims[i].curvature0));
        // Antes de la junta, la curvatura final de la anterior salvo dκ/ds·10⁻⁹ (≤ 1,7·10⁻¹¹).
        CHECK(std::abs((at.curvature - before.curvature) -
                       (prims[i].curvature0 - prims[i - 1].curvature1)) <= 1e-10);
    }
    MESSAGE("juntas: salto máximo ", max_position, " m y ", max_heading, " rad");
    CHECK(max_position <= 1e-6);
    CHECK(max_heading <= 1e-8);
}

// --- Criterio 4: círculo cerrado ----------------------------------------------------------------

TEST_CASE("stage_axis: four 90 degree arcs of radius 50 close the circle") {
    const double quarter = 25.0 * kPi;
    StageText st;
    for (int i = 0; i < 4; ++i) {
        st.plan += "arc " + num(quarter) + " 50\n";
    }
    st.length = 4.0 * quarter;
    st.profile = "pvi 0 0 0\npvi " + num(quarter + quarter + quarter + quarter) + " 0 0\n";
    const StageAxis axis = load(st.text());
    const StagePose start = axis.pose(0.0);
    const StagePose end = axis.pose(axis.length());
    MESSAGE("círculo: ", distance(start, end), " m");
    CHECK(distance(start, end) <= 1e-9);
    CHECK(end.heading == doctest::Approx(2.0 * kPi).epsilon(1e-14));
}

// --- Criterio 5: alzado --------------------------------------------------------------------------

TEST_CASE("stage_axis: a vertical curve from +5 to -7 percent drops 0.6 m at its PVI") {
    StageText st;
    st.plan = "straight 200\n";
    st.length = 200.0;
    st.start_height = 10.0;
    st.profile = "pvi 0 10 0\npvi 100 15 40\npvi 200 8 0\n";
    const StageAxis axis = load(st.text());
    // h + (g2 − g1)·L/8 = 15 − 0,12·40/8.
    CHECK(std::abs(axis.pose(100.0).y - 14.4) <= 1e-12);
    // Curva en [80, 120): curvatura (g2 − g1)/L = −0,003 dentro y 0 fuera.
    for (const double s : {80.0, 90.0, 100.0, 119.9}) {
        CAPTURE(s);
        CHECK(std::abs(axis.pose(s).vertical_curvature + 0.003) <= 1e-15);
    }
    for (const double s : {0.0, 50.0, 79.9, 120.0, 150.0, 200.0}) {
        CAPTURE(s);
        CHECK(axis.pose(s).vertical_curvature == 0.0);
    }
    CHECK(axis.pose(50.0).grade == doctest::Approx(0.05).epsilon(1e-14));
    CHECK(axis.pose(150.0).grade == doctest::Approx(-0.07).epsilon(1e-14));
    // Pendiente y altura continuas en los extremos de la curva.
    for (const double edge : {80.0, 120.0}) {
        CAPTURE(edge);
        const StagePose before = axis.pose(edge - 1e-10);
        const StagePose at = axis.pose(edge);
        CHECK(std::abs(at.grade - before.grade) <= 1e-12);
        CHECK(std::abs(at.y - before.y) <= 1e-11);
    }
}

TEST_CASE("stage_axis: a PVI without curve breaks the grade exactly at its s") {
    StageText st;
    st.plan = "straight 200\n";
    st.length = 200.0;
    st.profile = "pvi 0 0 0\npvi 100 5 0\npvi 200 0 0\n";
    const StageAxis axis = load(st.text());
    const StagePose before = axis.pose(100.0 - 1e-9);
    const StagePose at = axis.pose(100.0);
    CHECK(at.y == 5.0);
    CHECK(std::abs(before.y - 5.0) <= 1e-10);
    CHECK(before.grade == doctest::Approx(0.05).epsilon(1e-14));
    CHECK(at.grade == doctest::Approx(-0.05).epsilon(1e-14));
    for (const double s : {0.0, 50.0, 100.0, 150.0, 200.0}) {
        CHECK(axis.pose(s).vertical_curvature == 0.0);
    }
}

// --- Criterio 6: peralte, anchos y superficies ---------------------------------------------------

TEST_CASE("stage_axis: bank, widths and surfaces are exact at their points and linear between") {
    StageText st;
    st.plan = "straight 300\n";
    st.length = 300.0;
    st.bank = "bank 0 2\nbank 100 -4\nbank 200 6\n";
    st.width = "width 0 3 3.5\nwidth 200 2 2.5\n";
    st.surface = "surface 0 asphalt\nsurface 120 snow\nsurface 250 sand\n";
    const StageAxis axis = load(st.text());
    // Peralte: el archivo en %, la pose en tanto por uno.
    CHECK(axis.pose(0.0).bank == 0.02);
    CHECK(axis.pose(100.0).bank == -0.04);
    CHECK(axis.pose(200.0).bank == 0.06);
    CHECK(axis.pose(50.0).bank == doctest::Approx(-0.01).epsilon(1e-14));
    CHECK(axis.pose(150.0).bank == doctest::Approx(0.01).epsilon(1e-14));
    // El último valor sigue hasta el final.
    CHECK(axis.pose(250.0).bank == 0.06);
    CHECK(axis.pose(300.0).bank == 0.06);

    CHECK(axis.pose(0.0).half_width_left == 3.0);
    CHECK(axis.pose(0.0).half_width_right == 3.5);
    CHECK(axis.pose(100.0).half_width_left == doctest::Approx(2.5).epsilon(1e-15));
    CHECK(axis.pose(100.0).half_width_right == doctest::Approx(3.0).epsilon(1e-15));
    CHECK(axis.pose(200.0).half_width_left == 2.0);
    CHECK(axis.pose(300.0).half_width_left == 2.0);
    CHECK(axis.pose(300.0).half_width_right == 2.5);

    CHECK(axis.pose(0.0).surface == Surface::Asphalt);
    CHECK(axis.pose(120.0 - 1e-9).surface == Surface::Asphalt);
    CHECK(axis.pose(120.0).surface == Surface::Snow);
    CHECK(axis.pose(250.0 - 1e-9).surface == Surface::Snow);
    CHECK(axis.pose(250.0).surface == Surface::Sand);
    CHECK(axis.pose(300.0).surface == Surface::Sand);
}

// --- Regímenes ---------------------------------------------------------------------------------

TEST_CASE("stage_axis: s outside the stage or NaN is clamped") {
    const StageAxis axis = load(lazadas_text());
    CHECK(same_pose(axis.pose(-5.0), axis.pose(0.0)));
    CHECK(same_pose(axis.pose(axis.length() + 5.0), axis.pose(axis.length())));
    CHECK(same_pose(axis.pose(std::nan("")), axis.pose(0.0)));
    CHECK(axis.pose(-5.0).s == 0.0);
    CHECK(axis.pose(axis.length() + 5.0).s == axis.length());
    // Salida: la del archivo.
    const StagePose start = axis.pose(0.0);
    CHECK(start.x == -262.5);
    CHECK(start.z == 217.5);
    CHECK(start.y == 0.0);
    CHECK(start.heading == 0.0);
    CHECK(axis.length() == doctest::Approx(1150.0).epsilon(1e-12));
}

TEST_CASE("stage_axis: a stage of a single straight") {
    StageText st;
    st.plan = "straight 100\n";
    st.length = 100.0;
    const StageAxis axis = load(st.text());
    REQUIRE(axis.primitives().size() == 1);
    const StagePose mid = axis.pose(50.0);
    CHECK(mid.x == 0.0);
    CHECK(mid.z == -50.0);
    CHECK(mid.curvature == 0.0);
    CHECK(axis.pose(100.0).z == -100.0);
}

// --- Criterio 7: errores ------------------------------------------------------------------------

namespace {

// Tramo válido de 17 líneas; cada caso de error cambia una o varias.
const std::vector<std::string> kValidLines = {
    "format = 1",            // 1
    "name = t",              // 2
    "start_x = 0",           // 3
    "start_z = 0",           // 4
    "start_heading_deg = 0", // 5
    "start_height = 0",      // 6
    "[plan]",                // 7
    "straight 100",          // 8
    "[profile]",             // 9
    "pvi 0 0 0",             // 10
    "pvi 100 0 0",           // 11
    "[bank]",                // 12
    "bank 0 0",              // 13
    "[width]",               // 14
    "width 0 3 3",           // 15
    "[surface]",             // 16
    "surface 0 gravel",      // 17
};

std::string join(const std::vector<std::string>& lines) {
    std::string text;
    for (std::size_t i = 0; i < lines.size(); ++i) {
        text += lines[i];
        if (i + 1 < lines.size()) {
            text += '\n';
        }
    }
    return text;
}

// Cambios sobre kValidLines (líneas desde 1).
std::vector<std::string> replaced(int line, const std::string& text) {
    std::vector<std::string> lines = kValidLines;
    lines[static_cast<std::size_t>(line - 1)] = text;
    return lines;
}
std::vector<std::string> inserted(int line, const std::string& text) {
    std::vector<std::string> lines = kValidLines;
    lines.insert(lines.begin() + (line - 1), text);
    return lines;
}
std::vector<std::string> erased(int line, int count = 1) {
    std::vector<std::string> lines = kValidLines;
    lines.erase(lines.begin() + (line - 1), lines.begin() + (line - 1 + count));
    return lines;
}

struct ErrorCase {
    std::string rule;
    std::vector<std::string> lines;
    int line;
    std::string message; // parte del mensaje
};

} // namespace

TEST_CASE("stage_axis: the valid base of the error cases loads") {
    StageAxis axis;
    const StageParseResult result = StageAxis::parse(join(kValidLines), axis);
    INFO(result.error);
    CHECK(result.ok);
}

TEST_CASE("stage_axis: every format rule fails with its line") {
    std::vector<std::string> swapped = kValidLines;
    std::swap(swapped[0], swapped[1]);
    std::vector<std::string> overlap = inserted(11, "pvi 45 0 20");
    overlap.insert(overlap.begin() + 10, "pvi 30 0 20");
    const std::vector<ErrorCase> cases = {
        {"format que falta", erased(1), 1, "falta 'format'"},
        {"format que no es la primera clave", swapped, 1, "falta 'format'"},
        {"format = 2", replaced(1, "format = 2"), 1, "formato 2 no soportado"},
        {"format mal escrito", replaced(1, "format = uno"), 1, "número mal escrito"},
        {"clave desconocida", inserted(3, "start_y = 0"), 3, "clave desconocida 'start_y'"},
        {"clave repetida", inserted(4, "start_x = 1"), 4, "repetida (ya en la línea 3)"},
        {"clave que falta", erased(4), 6, "falta la clave 'start_z'"},
        {"valor de name vacío", replaced(2, "name ="), 2, "falta el valor de 'name'"},
        {"número mal escrito en la cabecera", replaced(3, "start_x = 1,5"), 3,
         "número mal escrito"},
        {"línea sin '=' en la cabecera", replaced(3, "start_x 0"), 3,
         "se esperaba 'clave = valor'"},
        {"sección desconocida", replaced(14, "[widths]"), 14, "sección desconocida [widths]"},
        {"sección repetida", inserted(12, "[plan]"), 12, "repetida (ya en la línea 7)"},
        {"sección que falta", erased(16, 2), 15, "falta la sección [surface]"},
        {"sección vacía", erased(13), 12, "[bank] está vacía"},
        {"clave = valor en una sección", replaced(8, "length = 100"), 8, "se esperaba una línea"},
        {"primitiva desconocida", replaced(8, "clothoid 100"), 8, "primitiva desconocida"},
        {"primitiva de otra sección", replaced(8, "pvi 0 0 0"), 8, "primitiva desconocida 'pvi'"},
        {"número de valores", replaced(8, "arc 100"), 8, "'arc' lleva 2 valores"},
        {"número mal escrito", replaced(8, "straight 1O0"), 8, "número mal escrito: '1O0'"},
        {"nan", replaced(8, "straight nan"), 8, "número mal escrito: 'nan'"},
        {"inf fuera de spiral", replaced(8, "arc 100 inf"), 8, "inf solo vale"},
        {"L <= 0", replaced(8, "straight 0"), 8, "L tiene que ser > 0"},
        {"L negativa", replaced(8, "arc -10 50"), 8, "L tiene que ser > 0"},
        {"|R| < 5", replaced(8, "arc 100 -4.9"), 8, "|R| tiene que ser ≥ 5 m"},
        {"R = 0", replaced(8, "spiral 100 inf 0"), 8, "R = 0 no vale"},
        {"primer pvi fuera de s = 0", replaced(10, "pvi 1 0 0"), 10, "el primer pvi tiene que ir"},
        {"primer pvi con curva", replaced(10, "pvi 0 0 10"), 10, "L_curva = 0"},
        {"start_height distinta del primer pvi", replaced(10, "pvi 0 1 0"), 10,
         "tiene que ser start_height"},
        {"L_curva < 0", inserted(11, "pvi 50 0 -1"), 11, "L_curva tiene que ser ≥ 0"},
        {"curvas verticales solapadas", overlap, 12, "se solapa con la del pvi de la línea 11"},
        {"s de pvi que no crece", inserted(11, "pvi 0 0 0"), 11, "tienen que crecer"},
        {"último pvi fuera de la longitud", replaced(11, "pvi 90 0 0"), 11,
         "el último pvi tiene que ir en s = longitud"},
        {"último pvi con curva", replaced(11, "pvi 100 0 10"), 11, "L_curva = 0"},
        {"primer peralte fuera de s = 0", replaced(13, "bank 5 0"), 13, "tiene que ir en s = 0"},
        {"s de peralte que no crece", inserted(14, "bank 0 1"), 14, "tienen que crecer"},
        {"s de peralte fuera del tramo", inserted(14, "bank 150 1"), 14, "fuera del tramo"},
        {"ancho <= 0", replaced(15, "width 0 0 3"), 15, "semianchos tienen que ser > 0"},
        {"superficie desconocida", replaced(17, "surface 0 mud"), 17,
         "superficie desconocida 'mud'"},
    };
    CHECK(cases.size() >= 14);
    for (const ErrorCase& c : cases) {
        CAPTURE(c.rule);
        StageAxis axis;
        const StageParseResult result = StageAxis::parse(join(c.lines), axis);
        CAPTURE(result.error);
        CHECK_FALSE(result.ok);
        const std::string prefix = "línea " + std::to_string(c.line) + ": ";
        CHECK(result.error.rfind(prefix, 0) == 0);
        CHECK(result.error.find(c.message) != std::string::npos);
    }
}

TEST_CASE("stage_axis: a failed parse leaves the axis as it was") {
    StageAxis axis = load(lazadas_text());
    const std::uint64_t before = axis.hash();
    const StageParseResult result = StageAxis::parse(join(replaced(8, "straight 0")), axis);
    CHECK_FALSE(result.ok);
    CHECK(axis.hash() == before);
    CHECK(axis.primitives().size() == 23);
}

// --- hash() ------------------------------------------------------------------------------------

TEST_CASE("stage_axis: hash ignores comments, spacing and name but sees every value") {
    const std::uint64_t base = load(join(kValidLines)).hash();
    // Mismos datos escritos de otra forma.
    std::vector<std::string> same = kValidLines;
    same[1] = "name = otro nombre";
    same[7] = "  straight\t100.0   # comentario";
    same.insert(same.begin() + 7, "# comentario");
    CHECK(load(join(same)).hash() == base);
    // Un valor de cada grupo.
    const std::vector<std::vector<std::string>> changed = {
        replaced(3, "start_x = 1"),        replaced(5, "start_heading_deg = 1"),
        replaced(8, "straight 101"),       replaced(11, "pvi 100 1 0"),
        replaced(13, "bank 0 1"),          replaced(15, "width 0 3 3.1"),
        replaced(17, "surface 0 asphalt"),
    };
    for (std::size_t i = 0; i < changed.size(); ++i) {
        CAPTURE(i);
        std::vector<std::string> lines = changed[i];
        if (i == 2) {
            lines[10] = "pvi 101 0 0"; // la longitud cambia con la recta
        }
        CHECK(load(join(lines)).hash() != base);
    }
}

// --- Criterio 8: sin reservas ------------------------------------------------------------------

TEST_CASE("stage_axis: pose does not allocate") {
    const StageAxis axis = load(lazadas_text());
    double sum = 0.0;
    const std::uint64_t before = rally::tests::global_new_count();
    for (int i = 0; i < 100'000; ++i) {
        const StagePose p = axis.pose(axis.length() * i / 99'999.0);
        sum += p.x + p.y + p.z;
    }
    const std::uint64_t after = rally::tests::global_new_count();
    CHECK(after - before == 0);
    CHECK(sum != 0.0);
}

// --- Criterio 9: coste ---------------------------------------------------------------------------

TEST_CASE("stage_axis: pose costs at most 2 microseconds in Release") {
    const StageAxis axis = load(lazadas_text());
    constexpr int kCalls = 1'000'000;
    double sum = 0.0;
    const auto t0 = std::chrono::steady_clock::now();
    for (int i = 0; i < kCalls; ++i) {
        // s repartidas por el tramo, sin orden (7919 es primo con 10⁶).
        const double s = axis.length() * static_cast<double>((i * 7919LL) % kCalls) / kCalls;
        const StagePose p = axis.pose(s);
        sum += p.x + p.heading;
    }
    const auto t1 = std::chrono::steady_clock::now();
    const double us = std::chrono::duration<double, std::micro>(t1 - t0).count() / kCalls;
    CHECK(sum != 0.0);
#ifdef NDEBUG
    MESSAGE("pose: ", us, " µs por llamada (Release)");
    CHECK(us <= 2.0);
#else
    MESSAGE("pose: ", us, " µs por llamada (Debug, sin límite)");
#endif
}

// --- Criterio 10: kExpectedStageHash -------------------------------------------------------------

TEST_CASE("stage_axis: 10000 poses of the test stage give the reference hash") {
    const StageAxis axis = load(lazadas_text());
    rally::sim::Fnv1a64 hash;
    constexpr int kPoses = 10'000;
    for (int i = 0; i < kPoses; ++i) {
        const StagePose p = axis.pose(axis.length() * i / (kPoses - 1.0));
        hash.add_f64(p.s);
        hash.add_f64(p.x);
        hash.add_f64(p.y);
        hash.add_f64(p.z);
        hash.add_f64(p.heading);
        hash.add_f64(p.curvature);
        hash.add_f64(p.grade);
        hash.add_f64(p.vertical_curvature);
        hash.add_f64(p.bank);
        hash.add_f64(p.half_width_left);
        hash.add_f64(p.half_width_right);
        hash.add_byte(static_cast<std::uint8_t>(p.surface));
    }
    char text[32];
    std::snprintf(text, sizeof(text), "0x%016llx", static_cast<unsigned long long>(hash.value()));
    MESSAGE("hash de las poses de lazadas.stage: ", text);
    CHECK(hash.value() == kExpectedStageHash);
}

// --- Criterio 11: lazadas ----------------------------------------------------------------------

TEST_CASE("stage_axis: the hairpins of the test stage pass 8 to 15 m from each other") {
    const StageAxis axis = load(lazadas_text());
    constexpr double kGap = 50.0;
    constexpr double kStep = 0.5;
    const auto count = static_cast<int>(axis.length() / kStep) + 1;
    std::vector<StagePose> poses;
    for (int i = 0; i < count; ++i) {
        poses.push_back(axis.pose(i * kStep));
    }
    const auto plan_distance = [](const StagePose& a, const StagePose& b) {
        return std::hypot(a.x - b.x, a.z - b.z);
    };
    // Muestreo cada 0,5 m.
    double best = 1e9;
    double best_a = 0.0;
    double best_b = 0.0;
    for (int i = 0; i < count; ++i) {
        for (int j = i + 1; j < count; ++j) {
            if (poses[j].s - poses[i].s <= kGap) {
                continue;
            }
            const double d = plan_distance(poses[i], poses[j]);
            if (d < best) {
                best = d;
                best_a = poses[i].s;
                best_b = poses[j].s;
            }
        }
    }
    // Refino local: búsqueda ternaria alterna en cada punto (±0,5 m), con |Δs| > 50.
    const auto refine = [&](double& moving, double fixed) {
        double lo = moving - kStep;
        double hi = moving + kStep;
        for (int it = 0; it < 80; ++it) {
            const double m1 = lo + (hi - lo) / 3.0;
            const double m2 = hi - (hi - lo) / 3.0;
            const auto cost = [&](double s) {
                return std::abs(s - fixed) > kGap ? plan_distance(axis.pose(s), axis.pose(fixed))
                                                  : 1e9;
            };
            if (cost(m1) < cost(m2)) {
                hi = m2;
            } else {
                lo = m1;
            }
        }
        moving = 0.5 * (lo + hi);
    };
    for (int round = 0; round < 10; ++round) {
        refine(best_a, best_b);
        refine(best_b, best_a);
    }
    const double refined = plan_distance(axis.pose(best_a), axis.pose(best_b));
    MESSAGE("lazadas: distancia mínima ", refined, " m entre s = ", best_a, " y s = ", best_b,
            " (muestreo: ", best, " m)");
    CHECK(refined <= best);
    CHECK(refined >= 8.0);
    CHECK(refined <= 15.0);
}
