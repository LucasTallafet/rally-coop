#include "sim/stage_axis.h"

#include "sim/fnv1a.h"
#include "sim/rmath.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <limits>
#include <string>
#include <utility>

namespace rally::sim {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDegToRad = kPi / 180.0;
// Radio mínimo de arcos y clotoides (|R|, m).
constexpr double kMinRadius = 5.0;
// Margen de las s que tienen que caer en la longitud del tramo: la longitud es la suma de las L
// en double y no tiene por qué coincidir con el decimal escrito en el archivo.
constexpr double kLengthTolerance = 1.0e-6;
// Distancia máxima entre nodos de posición de una clotoide (m).
constexpr double kNodeSpacing = 1.0;

// Gauss–Legendre de 5 puntos en [−1, 1]: nodos y pesos.
constexpr double kGaussNode1 = 0.538469310105683091036314420700208805; // ⅓·√(5 − 2·√(10/7))
constexpr double kGaussNode2 = 0.906179845938663992797626878299392965; // ⅓·√(5 + 2·√(10/7))
constexpr double kGaussWeight0 = 128.0 / 225.0;
constexpr double kGaussWeight1 = 0.478628670499366468030017719272106683; // (322 + 13·√70)/900
constexpr double kGaussWeight2 = 0.236926885056189087514264040719917363; // (322 − 13·√70)/900

constexpr std::string_view kWhitespace = " \t\r";

// Igual que en car_data.cpp.
std::string_view trim(std::string_view s) {
    const std::size_t first = s.find_first_not_of(kWhitespace);
    if (first == std::string_view::npos) {
        return {};
    }
    const std::size_t last = s.find_last_not_of(kWhitespace);
    return s.substr(first, last - first + 1);
}

StageParseResult fail(int line, const std::string& message) {
    StageParseResult result;
    result.error = "línea " + std::to_string(line) + ": " + message;
    return result;
}

// Un número finito que ocupa todo el texto (std::from_chars acepta "inf" y "nan": no valen).
bool parse_number(std::string_view text, double& value) {
    const char* first = text.data();
    const char* last = text.data() + text.size();
    const std::from_chars_result parsed = std::from_chars(first, last, value);
    return !text.empty() && parsed.ec == std::errc{} && parsed.ptr == last &&
           rmath::abs(value) <= std::numeric_limits<double>::max();
}

// Rumbo de una primitiva a u metros de su inicio (forma cerrada).
double spiral_heading(double heading0, const StagePrimitive& prim, double u) {
    return heading0 + prim.curvature0 * u +
           (prim.curvature1 - prim.curvature0) * u * u / (2.0 * prim.length);
}

// Avance (dx, dz) de una clotoide de ua a ub por Gauss–Legendre de 5 puntos.
void integrate_spiral(double heading0, const StagePrimitive& prim, double ua, double ub, double& dx,
                      double& dz) {
    const double half = 0.5 * (ub - ua);
    const double mid = 0.5 * (ua + ub);
    const std::array<double, 5> nodes = {-kGaussNode2, -kGaussNode1, 0.0, kGaussNode1, kGaussNode2};
    const std::array<double, 5> weights = {kGaussWeight2, kGaussWeight1, kGaussWeight0,
                                           kGaussWeight1, kGaussWeight2};
    double sx = 0.0;
    double sz = 0.0;
    for (std::size_t i = 0; i < nodes.size(); ++i) {
        const double theta = spiral_heading(heading0, prim, mid + half * nodes[i]);
        sx += weights[i] * rmath::sin(theta);
        sz -= weights[i] * rmath::cos(theta);
    }
    dx = half * sx;
    dz = half * sz;
}

// Índice del último elemento con clave ≤ s. El primero tiene clave 0 y s ≥ 0.
template <typename T, typename Key>
std::size_t last_at_or_before(const std::vector<T>& items, double s, Key key) {
    const auto it =
        std::upper_bound(items.begin(), items.end(), s,
                         [&key](double value, const T& item) { return value < key(item); });
    return static_cast<std::size_t>(it - items.begin()) - 1;
}

// Secciones del archivo.
enum class Section : std::uint8_t { None, Plan, Profile, Bank, Width, Surface };
constexpr std::array<std::string_view, 5> kSectionNames = {"plan", "profile", "bank", "width",
                                                           "surface"};

// Claves de la cabecera (después de format).
constexpr std::array<std::string_view, 5> kHeaderKeys = {"name", "start_x", "start_z",
                                                         "start_heading_deg", "start_height"};

// Partes de una línea de sección: la primitiva y hasta kMaxValues valores (se cuentan todos).
constexpr std::size_t kMaxValues = 4;
struct Tokens {
    std::string_view word;
    std::array<std::string_view, kMaxValues> values{};
    std::size_t count = 0; // valores, sin la primitiva
};

Tokens split(std::string_view line) {
    Tokens tokens;
    std::size_t pos = 0;
    bool first = true;
    while (pos < line.size()) {
        const std::size_t start = line.find_first_not_of(kWhitespace, pos);
        if (start == std::string_view::npos) {
            break;
        }
        std::size_t end = line.find_first_of(kWhitespace, start);
        if (end == std::string_view::npos) {
            end = line.size();
        }
        const std::string_view token = line.substr(start, end - start);
        if (first) {
            tokens.word = token;
            first = false;
        } else {
            if (tokens.count < kMaxValues) {
                tokens.values[tokens.count] = token;
            }
            ++tokens.count;
        }
        pos = end;
    }
    return tokens;
}

// Lo que se lee de cada línea de sección, con su línea para los errores del final.
struct PrimitiveLine {
    StagePrimitive prim;
    double radius = 0.0; // arcos: R con signo
    int line = 0;
};
struct PviLine {
    double s = 0.0;
    double height = 0.0;
    double curve_length = 0.0;
    int line = 0;
};
struct PointLine {
    double s = 0.0;
    double a = 0.0; // peralte (tanto por uno) o semiancho izquierdo
    double b = 0.0; // semiancho derecho
    Surface surface = Surface::Gravel;
    int line = 0;
};

} // namespace

StageParseResult StageAxis::parse(std::string_view text, StageAxis& out) {
    std::array<int, kHeaderKeys.size()> header_line{};
    std::array<double, kHeaderKeys.size()> header_value{};
    std::string name;
    int format_line = 0;
    std::array<int, kSectionNames.size()> section_line{};
    Section section = Section::None;
    std::vector<PrimitiveLine> plan;
    std::vector<PviLine> profile;
    std::vector<PointLine> bank;
    std::vector<PointLine> width;
    std::vector<PointLine> surface;
    int line_number = 0;

    // Al empezar la primera sección (o al final, si no hay ninguna), la cabecera está completa.
    const auto check_header = [&](int line) -> StageParseResult {
        for (std::size_t k = 0; k < kHeaderKeys.size(); ++k) {
            if (header_line[k] == 0) {
                return fail(line, "falta la clave '" + std::string(kHeaderKeys[k]) + "'");
            }
        }
        StageParseResult ok;
        ok.ok = true;
        return ok;
    };

    std::size_t pos = 0;
    while (pos <= text.size()) {
        const std::size_t end = text.find('\n', pos);
        std::string_view line =
            text.substr(pos, end == std::string_view::npos ? std::string_view::npos : end - pos);
        pos = end == std::string_view::npos ? text.size() + 1 : end + 1;
        ++line_number;

        const std::size_t comment = line.find('#');
        if (comment != std::string_view::npos) {
            line = line.substr(0, comment);
        }
        line = trim(line);
        if (line.empty()) {
            continue;
        }

        // format: la primera clave, antes que nada (el número de versión es el gancho).
        if (format_line == 0) {
            const std::size_t equals = line.find('=');
            if (equals == std::string_view::npos || trim(line.substr(0, equals)) != "format") {
                return fail(line_number, "falta 'format' (tiene que ser la primera clave)");
            }
            const std::string_view value = trim(line.substr(equals + 1));
            int format = 0;
            const std::from_chars_result parsed =
                std::from_chars(value.data(), value.data() + value.size(), format);
            if (value.empty() || parsed.ec != std::errc{} ||
                parsed.ptr != value.data() + value.size()) {
                return fail(line_number,
                            "número mal escrito en 'format': '" + std::string(value) + "'");
            }
            if (format != kStageFormatVersion) {
                return fail(line_number, "formato " + std::to_string(format) +
                                             " no soportado: este juego lee el " +
                                             std::to_string(kStageFormatVersion));
            }
            format_line = line_number;
            continue;
        }

        if (line.front() == '[') {
            if (line.back() != ']') {
                return fail(line_number, "sección mal escrita: falta ']'");
            }
            const std::string_view section_name = trim(line.substr(1, line.size() - 2));
            std::size_t index = kSectionNames.size();
            for (std::size_t s = 0; s < kSectionNames.size(); ++s) {
                if (kSectionNames[s] == section_name) {
                    index = s;
                }
            }
            if (index == kSectionNames.size()) {
                return fail(line_number, "sección desconocida [" + std::string(section_name) + "]");
            }
            if (section_line[index] != 0) {
                return fail(line_number, "sección [" + std::string(section_name) +
                                             "] repetida (ya en la línea " +
                                             std::to_string(section_line[index]) + ")");
            }
            if (section == Section::None) {
                const StageParseResult header = check_header(line_number);
                if (!header.ok) {
                    return header;
                }
            }
            section_line[index] = line_number;
            section = static_cast<Section>(index + 1);
            continue;
        }

        const std::size_t equals = line.find('=');
        if (section == Section::None) {
            // Cabecera: clave = valor.
            if (equals == std::string_view::npos) {
                return fail(line_number, "se esperaba 'clave = valor' antes de la primera sección");
            }
            const std::string_view key = trim(line.substr(0, equals));
            const std::string_view value = trim(line.substr(equals + 1));
            if (key == "format") {
                return fail(line_number, "clave 'format' repetida (ya en la línea " +
                                             std::to_string(format_line) + ")");
            }
            std::size_t k = kHeaderKeys.size();
            for (std::size_t i = 0; i < kHeaderKeys.size(); ++i) {
                if (kHeaderKeys[i] == key) {
                    k = i;
                }
            }
            if (k == kHeaderKeys.size()) {
                return fail(line_number, "clave desconocida '" + std::string(key) + "'");
            }
            if (header_line[k] != 0) {
                return fail(line_number, "clave '" + std::string(key) +
                                             "' repetida (ya en la línea " +
                                             std::to_string(header_line[k]) + ")");
            }
            if (k == 0) {
                if (value.empty()) {
                    return fail(line_number, "falta el valor de 'name'");
                }
                name = std::string(value);
            } else if (!parse_number(value, header_value[k])) {
                return fail(line_number, "número mal escrito en '" + std::string(key) + "': '" +
                                             std::string(value) + "'");
            }
            header_line[k] = line_number;
            continue;
        }

        // Línea de sección: primitiva y valores.
        const std::string_view section_name = kSectionNames[static_cast<std::size_t>(section) - 1];
        if (equals != std::string_view::npos) {
            return fail(line_number, "se esperaba una línea de [" + std::string(section_name) +
                                         "], no 'clave = valor'");
        }
        const Tokens tokens = split(line);
        // Primitivas de cada sección y sus valores.
        struct Shape {
            Section section;
            std::string_view word;
            std::size_t count;
            std::string_view values;
        };
        constexpr std::array<Shape, 7> kShapes = {
            Shape{Section::Plan, "straight", 1, "L"},
            Shape{Section::Plan, "arc", 2, "L R"},
            Shape{Section::Plan, "spiral", 3, "L R0 R1"},
            Shape{Section::Profile, "pvi", 3, "s altura L_curva"},
            Shape{Section::Bank, "bank", 2, "s %"},
            Shape{Section::Width, "width", 3, "s izquierda derecha"},
            Shape{Section::Surface, "surface", 2, "s superficie"},
        };
        const Shape* shape = nullptr;
        for (const Shape& candidate : kShapes) {
            if (candidate.section == section && candidate.word == tokens.word) {
                shape = &candidate;
            }
        }
        if (shape == nullptr) {
            return fail(line_number, "primitiva desconocida '" + std::string(tokens.word) +
                                         "' en [" + std::string(section_name) + "]");
        }
        if (tokens.count != shape->count) {
            return fail(line_number, "'" + std::string(shape->word) + "' lleva " +
                                         std::to_string(shape->count) + " valores (" +
                                         std::string(shape->values) + "), no " +
                                         std::to_string(tokens.count));
        }
        // Los valores numéricos (la superficie es un nombre; los R de spiral pueden ser inf).
        std::array<double, kMaxValues> v{};
        for (std::size_t i = 0; i < tokens.count; ++i) {
            if (section == Section::Surface && i == 1) {
                continue;
            }
            if (tokens.word == "spiral" && i > 0 && tokens.values[i] == "inf") {
                v[i] = std::numeric_limits<double>::infinity();
                continue;
            }
            if (!parse_number(tokens.values[i], v[i])) {
                std::string message = "número mal escrito: '" + std::string(tokens.values[i]) + "'";
                if (tokens.values[i] == "inf") {
                    message += " (inf solo vale como R0 o R1 de 'spiral')";
                }
                return fail(line_number, message);
            }
        }

        switch (section) {
        case Section::Plan: {
            PrimitiveLine p;
            p.line = line_number;
            p.prim.length = v[0];
            if (!(v[0] > 0.0)) {
                return fail(line_number, "L tiene que ser > 0");
            }
            // Curvatura de cada R (inf → 0).
            for (std::size_t i = 1; i < tokens.count; ++i) {
                if (v[i] == 0.0) {
                    return fail(line_number, "R = 0 no vale: una recta es 'straight' (o R = inf "
                                             "en 'spiral')");
                }
                if (rmath::abs(v[i]) < kMinRadius) {
                    return fail(line_number, "|R| tiene que ser ≥ 5 m");
                }
            }
            if (tokens.word == "arc") {
                p.radius = v[1];
                p.prim.curvature0 = p.prim.curvature1 = 1.0 / v[1];
            } else if (tokens.word == "spiral") {
                // 1/inf = 0 en IEEE 754.
                p.prim.curvature0 = 1.0 / v[1];
                p.prim.curvature1 = 1.0 / v[2];
                // Una clotoide de curvatura constante es un arco (o una recta): exacta.
                if (p.prim.curvature0 == p.prim.curvature1 && p.prim.curvature0 != 0.0) {
                    p.radius = v[1];
                }
            }
            plan.push_back(p);
            break;
        }
        case Section::Profile: {
            PviLine p{v[0], v[1], v[2], line_number};
            if (p.curve_length < 0.0) {
                return fail(line_number, "L_curva tiene que ser ≥ 0");
            }
            if (profile.empty()) {
                if (p.s != 0.0) {
                    return fail(line_number, "el primer pvi tiene que ir en s = 0");
                }
                if (p.curve_length != 0.0) {
                    return fail(line_number, "el primer pvi tiene que tener L_curva = 0");
                }
                if (p.height != header_value[4]) {
                    return fail(line_number, "la altura del primer pvi tiene que ser start_height");
                }
            } else {
                const PviLine& prev = profile.back();
                if (!(p.s > prev.s)) {
                    return fail(line_number, "las s de [profile] tienen que crecer");
                }
                if (p.s - 0.5 * p.curve_length < prev.s + 0.5 * prev.curve_length) {
                    return fail(line_number,
                                "la curva vertical se solapa con la del pvi de la línea " +
                                    std::to_string(prev.line));
                }
            }
            profile.push_back(p);
            break;
        }
        case Section::Bank:
        case Section::Width:
        case Section::Surface: {
            std::vector<PointLine>& points =
                section == Section::Bank ? bank : (section == Section::Width ? width : surface);
            PointLine p;
            p.s = v[0];
            p.line = line_number;
            if (points.empty() ? p.s != 0.0 : !(p.s > points.back().s)) {
                return fail(line_number,
                            points.empty()
                                ? "la primera línea de [" + std::string(section_name) +
                                      "] tiene que ir en s = 0"
                                : "las s de [" + std::string(section_name) + "] tienen que crecer");
            }
            if (section == Section::Bank) {
                p.a = v[1] / 100.0; // % → tanto por uno
            } else if (section == Section::Width) {
                if (!(v[1] > 0.0) || !(v[2] > 0.0)) {
                    return fail(line_number, "los semianchos tienen que ser > 0");
                }
                p.a = v[1];
                p.b = v[2];
            } else {
                bool known = false;
                for (int k = 0; k < kSurfaceCount; ++k) {
                    const auto candidate = static_cast<Surface>(k);
                    if (tokens.values[1] == surface_key(candidate)) {
                        p.surface = candidate;
                        known = true;
                    }
                }
                if (!known) {
                    return fail(line_number, "superficie desconocida '" +
                                                 std::string(tokens.values[1]) +
                                                 "' (asphalt, gravel, dirt, snow o sand)");
                }
            }
            points.push_back(p);
            break;
        }
        case Section::None:
            break;
        }
    }

    // Final del archivo: lo que falta y lo que depende de la longitud.
    if (format_line == 0) {
        return fail(line_number, "falta 'format' (tiene que ser la primera clave)");
    }
    if (section == Section::None) {
        const StageParseResult header = check_header(line_number);
        if (!header.ok) {
            return header;
        }
    }
    for (std::size_t s = 0; s < kSectionNames.size(); ++s) {
        if (section_line[s] == 0) {
            return fail(line_number, "falta la sección [" + std::string(kSectionNames[s]) + "]");
        }
    }
    const std::array<std::size_t, 5> counts = {plan.size(), profile.size(), bank.size(),
                                               width.size(), surface.size()};
    for (std::size_t s = 0; s < kSectionNames.size(); ++s) {
        if (counts[s] == 0) {
            return fail(section_line[s],
                        "la sección [" + std::string(kSectionNames[s]) + "] está vacía");
        }
    }

    StageAxis axis;
    axis.name_ = name;
    axis.start_x_ = header_value[1];
    axis.start_z_ = header_value[2];
    axis.start_heading_ = header_value[3] * kDegToRad;
    axis.start_height_ = header_value[4];
    double s0 = 0.0;
    for (const PrimitiveLine& p : plan) {
        StagePrimitive prim = p.prim;
        prim.s0 = s0;
        s0 += prim.length;
        axis.prims_.push_back(prim);
        PlanStart start;
        start.radius = p.radius;
        axis.starts_.push_back(start);
    }
    axis.length_ = s0;
    const double length = axis.length_;

    const PviLine& last = profile.back();
    if (profile.size() < 2 || rmath::abs(last.s - length) > kLengthTolerance) {
        return fail(last.line,
                    "el último pvi tiene que ir en s = longitud (" + std::to_string(length) + ")");
    }
    if (last.curve_length != 0.0) {
        return fail(last.line, "el último pvi tiene que tener L_curva = 0");
    }
    for (const PviLine& p : profile) {
        Pvi pvi;
        pvi.s = p.s;
        pvi.height = p.height;
        pvi.curve_length = p.curve_length;
        axis.pvi_.push_back(pvi);
    }
    axis.pvi_.back().s = length;

    for (const std::vector<PointLine>* points : {&bank, &width, &surface}) {
        if (points->back().s > length + kLengthTolerance) {
            return fail(points->back().line,
                        "s fuera del tramo (longitud " + std::to_string(length) + ")");
        }
    }
    for (const PointLine& p : bank) {
        axis.bank_.push_back({rmath::min(p.s, length), p.a});
    }
    for (const PointLine& p : width) {
        axis.width_.push_back({rmath::min(p.s, length), p.a, p.b});
    }
    for (const PointLine& p : surface) {
        axis.surface_.push_back({rmath::min(p.s, length), p.surface});
    }

    axis.build();
    out = std::move(axis);
    StageParseResult result;
    result.ok = true;
    return result;
}

void StageAxis::build() {
    double x = start_x_;
    double z = start_z_;
    double heading = start_heading_;
    for (std::size_t i = 0; i < prims_.size(); ++i) {
        const StagePrimitive& prim = prims_[i];
        PlanStart& start = starts_[i];
        start.x0 = x;
        start.z0 = z;
        start.heading0 = heading;
        if (start.radius != 0.0) {
            // Arco: el centro como AxisBuilder::arc, con R con signo.
            start.cx = x + start.radius * rmath::cos(heading);
            start.cz = z + start.radius * rmath::sin(heading);
        } else if (prim.curvature0 != prim.curvature1) {
            // Clotoide: nodos cada metro como mucho, por Gauss–Legendre.
            const auto count = static_cast<std::size_t>(rmath::ceil(prim.length / kNodeSpacing));
            start.node_first = node_x_.size();
            start.node_count = count;
            start.node_step = prim.length / static_cast<double>(count);
            double nx = x;
            double nz = z;
            node_x_.push_back(nx);
            node_z_.push_back(nz);
            for (std::size_t k = 0; k < count; ++k) {
                double dx = 0.0;
                double dz = 0.0;
                integrate_spiral(heading, prim, static_cast<double>(k) * start.node_step,
                                 static_cast<double>(k + 1) * start.node_step, dx, dz);
                nx += dx;
                nz += dz;
                node_x_.push_back(nx);
                node_z_.push_back(nz);
            }
        }
        // El inicio de la siguiente es el final de esta, con la misma función que pose().
        const PlanPoint end = plan_point(i, prim.length);
        x = end.x;
        z = end.z;
        heading = end.heading;
    }

    for (std::size_t i = 0; i + 1 < pvi_.size(); ++i) {
        pvi_[i].grade_after = (pvi_[i + 1].height - pvi_[i].height) / (pvi_[i + 1].s - pvi_[i].s);
    }
    pvi_.back().grade_after = pvi_[pvi_.size() - 2].grade_after;
}

StageAxis::PlanPoint StageAxis::plan_point(std::size_t i, double u) const {
    const StagePrimitive& prim = prims_[i];
    const PlanStart& start = starts_[i];
    u = rmath::clamp(u, 0.0, prim.length);
    PlanPoint p;
    if (start.radius != 0.0) {
        // Arco: como segment_point.
        p.heading = start.heading0 + u / start.radius;
        p.curvature = prim.curvature0;
        p.x = start.cx - start.radius * rmath::cos(p.heading);
        p.z = start.cz - start.radius * rmath::sin(p.heading);
        return p;
    }
    if (prim.curvature0 == prim.curvature1) {
        // Recta.
        p.heading = start.heading0;
        p.x = start.x0 + u * rmath::sin(start.heading0);
        p.z = start.z0 - u * rmath::cos(start.heading0);
        return p;
    }
    // Clotoide: rumbo en forma cerrada; posición desde el nodo anterior.
    p.heading = spiral_heading(start.heading0, prim, u);
    p.curvature = prim.curvature0 + (prim.curvature1 - prim.curvature0) * u / prim.length;
    const auto k = rmath::min(static_cast<std::size_t>(rmath::floor(u / start.node_step)),
                              start.node_count - 1);
    double dx = 0.0;
    double dz = 0.0;
    integrate_spiral(start.heading0, prim, static_cast<double>(k) * start.node_step, u, dx, dz);
    p.x = node_x_[start.node_first + k] + dx;
    p.z = node_z_[start.node_first + k] + dz;
    return p;
}

StagePose StageAxis::pose(double s) const {
    // NaN da 0: la comparación falla.
    if (!(s > 0.0)) {
        s = 0.0;
    } else if (s > length_) {
        s = length_;
    }
    StagePose pose;
    pose.s = s;

    // Planta.
    const std::size_t i =
        last_at_or_before(prims_, s, [](const StagePrimitive& p) { return p.s0; });
    const PlanPoint plan = plan_point(i, s - prims_[i].s0);
    pose.x = plan.x;
    pose.z = plan.z;
    pose.heading = plan.heading;
    pose.curvature = plan.curvature;

    // Alzado: cada curva vertical ocupa [s_PIV − L/2, s_PIV + L/2).
    const std::size_t v = last_at_or_before(pvi_, s, [](const Pvi& p) { return p.s; });
    const Pvi& here = pvi_[v];
    const Pvi* curve = nullptr;
    double grade_in = 0.0;
    if (v + 1 < pvi_.size() && pvi_[v + 1].curve_length > 0.0 &&
        s >= pvi_[v + 1].s - 0.5 * pvi_[v + 1].curve_length) {
        curve = &pvi_[v + 1];
        grade_in = here.grade_after;
    } else if (here.curve_length > 0.0 && s < here.s + 0.5 * here.curve_length) {
        curve = &here;
        grade_in = pvi_[v - 1].grade_after; // v ≥ 1: el primer PIV no tiene curva
    }
    if (curve != nullptr) {
        const double lc = curve->curve_length;
        const double grade_out = curve->grade_after;
        const double d = s - (curve->s - 0.5 * lc);
        pose.y = curve->height - grade_in * 0.5 * lc + grade_in * d +
                 (grade_out - grade_in) * d * d / (2.0 * lc);
        pose.grade = grade_in + (grade_out - grade_in) * d / lc;
        pose.vertical_curvature = (grade_out - grade_in) / lc;
    } else {
        pose.y = here.height + here.grade_after * (s - here.s);
        pose.grade = here.grade_after;
    }

    // Peralte y semianchos, lineales entre puntos; constantes tras el último.
    const std::size_t b = last_at_or_before(bank_, s, [](const BankPoint& p) { return p.s; });
    pose.bank = bank_[b].value;
    if (b + 1 < bank_.size()) {
        const double t = (s - bank_[b].s) / (bank_[b + 1].s - bank_[b].s);
        pose.bank = bank_[b].value + (bank_[b + 1].value - bank_[b].value) * t;
    }
    const std::size_t w = last_at_or_before(width_, s, [](const WidthPoint& p) { return p.s; });
    pose.half_width_left = width_[w].left;
    pose.half_width_right = width_[w].right;
    if (w + 1 < width_.size()) {
        const double t = (s - width_[w].s) / (width_[w + 1].s - width_[w].s);
        pose.half_width_left = width_[w].left + (width_[w + 1].left - width_[w].left) * t;
        pose.half_width_right = width_[w].right + (width_[w + 1].right - width_[w].right) * t;
    }

    // Superficie: la del último punto con s_j ≤ s.
    pose.surface =
        surface_[last_at_or_before(surface_, s, [](const SurfacePoint& p) { return p.s; })].surface;
    return pose;
}

std::uint64_t StageAxis::hash() const {
    Fnv1a64 hash;
    hash.add_u32(static_cast<std::uint32_t>(kStageFormatVersion));
    hash.add_f64(start_x_);
    hash.add_f64(start_z_);
    hash.add_f64(start_heading_);
    hash.add_f64(start_height_);
    hash.add_u32(static_cast<std::uint32_t>(prims_.size()));
    for (const StagePrimitive& p : prims_) {
        hash.add_f64(p.length);
        hash.add_f64(p.curvature0);
        hash.add_f64(p.curvature1);
    }
    hash.add_u32(static_cast<std::uint32_t>(pvi_.size()));
    for (const Pvi& p : pvi_) {
        hash.add_f64(p.s);
        hash.add_f64(p.height);
        hash.add_f64(p.curve_length);
    }
    hash.add_u32(static_cast<std::uint32_t>(bank_.size()));
    for (const BankPoint& p : bank_) {
        hash.add_f64(p.s);
        hash.add_f64(p.value);
    }
    hash.add_u32(static_cast<std::uint32_t>(width_.size()));
    for (const WidthPoint& p : width_) {
        hash.add_f64(p.s);
        hash.add_f64(p.left);
        hash.add_f64(p.right);
    }
    hash.add_u32(static_cast<std::uint32_t>(surface_.size()));
    for (const SurfacePoint& p : surface_) {
        hash.add_f64(p.s);
        hash.add_byte(static_cast<std::uint8_t>(p.surface));
    }
    return hash.value();
}

} // namespace rally::sim
