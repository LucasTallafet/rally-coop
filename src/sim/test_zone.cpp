#include "sim/test_zone.h"

#include "sim/check.h"
#include "sim/fnv1a.h"
#include "sim/pcg32.h"
#include "sim/rmath.h"

#include <charconv>
#include <cstring>
#include <utility>

namespace rally::sim {

const char* surface_name(Surface surface) {
    switch (surface) {
    case Surface::Asphalt:
        return "asfalto";
    case Surface::Gravel:
        return "grava";
    case Surface::Dirt:
        return "tierra";
    case Surface::Snow:
        return "nieve";
    case Surface::Sand:
        return "arena";
    }
    return "?";
}

const char* surface_key(Surface surface) {
    switch (surface) {
    case Surface::Asphalt:
        return "asphalt";
    case Surface::Gravel:
        return "gravel";
    case Surface::Dirt:
        return "dirt";
    case Surface::Snow:
        return "snow";
    case Surface::Sand:
        return "sand";
    }
    return "?";
}

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

// Posición de las áreas (centros y medias medidas de los rectángulos, en metros).
// Recta: x = 0, de z = 650 a z = -350; los baches siguen hacia el norte hasta z = -650.
constexpr double kStraightHalfWidth = 6.0;
constexpr double kStraightStartZ = 650.0;
constexpr double kStraightEndZ = -350.0;
constexpr double kBumpsSplitZ = -500.0; // asfalto → grava
constexpr double kBumpsEndZ = -650.0;
// Las áreas que se tocan de punta a punta (recta y baches) se alargan este tanto por debajo
// de la siguiente al mezclar la amplitud de h: así en la junta no aparece el ruido del fondo.
constexpr double kBlendOverlap = 0.5 * zone::kBlendWidth;

// Rampa (asfalto, 8 m) con 10 m llanos antes y 20 m después: kinks en 10, 50, 70 y 110 m.
constexpr double kRampCenterX = -50.0;
constexpr double kRampStartZ = -505.0;
constexpr double kRampLead = 10.0;
constexpr double kRampTail = 20.0;
constexpr double kRampLength =
    kRampLead + 2.0 * zone::kRampRun + zone::kRampLanding + kRampTail; // 130 m
// Cresta (grava, 12 m): 40 m de entrada, el perfil de 25 m y 40 m de salida.
constexpr double kCrestCenterX = 42.0;
constexpr double kCrestStartZ = -450.0;
constexpr double kCrestHalfWidth = 6.0;
constexpr double kCrestLead = 40.0;
constexpr double kCrestTotal = 2.0 * kCrestLead + zone::kCrestLength; // 105 m
// Baches de asfalto: las ondulaciones empiezan al principio y los baches sueltos van después.
constexpr double kBump1Center = 112.0;
constexpr double kBump2Center = 132.0;

// Pista ciega: marco local (u al este, v al norte) con la meta en (0, 0), llevado al mundo
// con x = kBlindX + u, z = kBlindZ - v. Circuito en el sentido de las agujas del reloj:
// recta de meta, curva rápida, curva media, chicana, curva, recta de atrás, horquilla y
// curva a la izquierda de vuelta a la recta de meta.
constexpr double kBlindX = 100.0;
constexpr double kBlindZ = -150.0;
constexpr double kBlindStartV = -71.0; // inicio del eje en la recta de meta
constexpr double kBlindFastRadius = 80.0;
constexpr double kBlindMediumRadius = 35.0;
constexpr double kBlindCornerRadius = 25.0;
constexpr double kBlindHairpinRadius = 12.0;
constexpr double kChicaneRadius = 25.0;
constexpr double kChicaneAngle = kPi / 6.0;

double smoothstep01(double t) {
    t = rmath::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
}

// Rampa suavizada: max(0, s) con una parábola de longitud `width` alrededor de 0 (una curva
// vertical que conserva las tangentes).
double soft_ramp(double s, double width) {
    const double half = 0.5 * width;
    if (s <= -half) {
        return 0.0;
    }
    if (s >= half) {
        return s;
    }
    return (s + half) * (s + half) / (2.0 * width);
}

double ramp_profile(double s) {
    constexpr double g = zone::kRampGrade;
    constexpr double w = zone::kRampTransition;
    constexpr double k1 = kRampLead;
    constexpr double k2 = k1 + zone::kRampRun;
    constexpr double k3 = k2 + zone::kRampLanding;
    constexpr double k4 = k3 + zone::kRampRun;
    return g * (soft_ramp(s - k1, w) - soft_ramp(s - k2, w) - soft_ramp(s - k3, w) +
                soft_ramp(s - k4, w));
}

double crest_profile(double s) {
    const double t = (s - kCrestLead) / zone::kCrestLength;
    if (t <= 0.0 || t >= 1.0) {
        return 0.0;
    }
    return zone::kCrestHeight * 0.5 * (1.0 - rmath::cos(kTwoPi * t));
}

double raised_cosine(double s, double center, double length, double height) {
    const double t = (s - center) / length + 0.5;
    if (t <= 0.0 || t >= 1.0) {
        return 0.0;
    }
    return height * 0.5 * (1.0 - rmath::cos(kTwoPi * t));
}

double bumps_profile(double s) {
    double h = 0.0;
    const double wave_end = zone::kWavePeriods * zone::kWaveWavelength;
    if (s > 0.0 && s < wave_end) {
        h += zone::kWaveAmplitude * rmath::sin(kTwoPi * s / zone::kWaveWavelength);
    }
    h += raised_cosine(s, kBump1Center, zone::kBumpLength, zone::kBumpHeight);
    h += raised_cosine(s, kBump2Center, zone::kBumpLength, zone::kBumpHeight);
    return h;
}

// Perfil a lo largo de una franja hacia el norte (s = start_z - z) con taludes de 45° a los
// lados: fuera de la franja, la altura baja (o sube) un metro por metro hasta 0.
template <typename Profile>
double strip_height(double x, double z, double center_x, double half_width, double start_z,
                    double length, Profile profile) {
    const double s = start_z - z;
    if (s <= 0.0 || s >= length) {
        return 0.0;
    }
    const double v = profile(s);
    const double outside = rmath::max(0.0, rmath::abs(x - center_x) - half_width);
    const double magnitude = rmath::max(0.0, rmath::abs(v) - outside);
    return v < 0.0 ? -magnitude : magnitude;
}

double wrap_angle(double a, double low) { return a - kTwoPi * rmath::floor((a - low) / kTwoPi); }

// Constructor de ejes: encadena rectas y arcos desde una posición y un rumbo.
class AxisBuilder {
public:
    AxisBuilder(double x, double z, double psi) : x_(x), z_(z), psi_(psi) {}

    void straight(double length) {
        AxisSegment seg;
        seg.x0 = x_;
        seg.z0 = z_;
        seg.psi0 = psi_;
        seg.length = length;
        segments_.push_back(seg);
        x_ += length * rmath::sin(psi_);
        z_ -= length * rmath::cos(psi_);
    }

    // turn > 0 gira a la derecha.
    void arc(double radius, double turn) {
        AxisSegment seg;
        seg.x0 = x_;
        seg.z0 = z_;
        seg.psi0 = psi_;
        seg.radius = radius;
        seg.turn = turn;
        seg.length = radius * rmath::abs(turn);
        const double side = turn > 0.0 ? 1.0 : -1.0;
        seg.cx = x_ + side * radius * rmath::cos(psi_);
        seg.cz = z_ + side * radius * rmath::sin(psi_);
        segments_.push_back(seg);
        psi_ += turn;
        x_ = seg.cx - side * radius * rmath::cos(psi_);
        z_ = seg.cz - side * radius * rmath::sin(psi_);
    }

    double x() const { return x_; }
    double z() const { return z_; }
    std::vector<AxisSegment> take() { return std::move(segments_); }

private:
    double x_;
    double z_;
    double psi_;
    std::vector<AxisSegment> segments_;
};

AxisPoint segment_point(const AxisSegment& seg, double s) {
    if (seg.radius == 0.0) {
        return {seg.x0 + s * rmath::sin(seg.psi0), seg.z0 - s * rmath::cos(seg.psi0), seg.psi0};
    }
    const double side = seg.turn > 0.0 ? 1.0 : -1.0;
    const double psi = seg.psi0 + side * s / seg.radius;
    return {seg.cx - side * seg.radius * rmath::cos(psi),
            seg.cz - side * seg.radius * rmath::sin(psi), psi};
}

double segment_distance(const AxisSegment& seg, double x, double z) {
    if (seg.radius == 0.0) {
        const double dx = rmath::sin(seg.psi0);
        const double dz = -rmath::cos(seg.psi0);
        const double t = rmath::clamp((x - seg.x0) * dx + (z - seg.z0) * dz, 0.0, seg.length);
        const double px = x - (seg.x0 + t * dx);
        const double pz = z - (seg.z0 + t * dz);
        return rmath::sqrt(px * px + pz * pz);
    }
    const double side = seg.turn > 0.0 ? 1.0 : -1.0;
    const double vx = x - seg.cx;
    const double vz = z - seg.cz;
    const double r = rmath::sqrt(vx * vx + vz * vz);
    // Rumbo del punto del arco en la dirección de (x, z) desde el centro.
    const double psi = rmath::atan2(-side * vz, -side * vx);
    const double delta = wrap_angle(psi - seg.psi0, 0.5 * seg.turn - kPi);
    const bool inside =
        seg.turn > 0.0 ? delta >= 0.0 && delta <= seg.turn : delta <= 0.0 && delta >= seg.turn;
    if (inside) {
        return rmath::abs(r - seg.radius);
    }
    const AxisPoint a = segment_point(seg, 0.0);
    const AxisPoint b = segment_point(seg, seg.length);
    const double da = rmath::sqrt((x - a.x) * (x - a.x) + (z - a.z) * (z - a.z));
    const double db = rmath::sqrt((x - b.x) * (x - b.x) + (z - b.z) * (z - b.z));
    return rmath::min(da, db);
}

ZoneArea rect_area(AreaId id, const char* key, const char* name, Surface surface, double cx,
                   double cz, double half_x, double half_z) {
    ZoneArea a;
    a.id = id;
    a.key = key;
    a.name = name;
    a.surface = surface;
    a.cx = cx;
    a.cz = cz;
    a.half_x = half_x;
    a.half_z = half_z;
    a.min_x = cx - half_x;
    a.max_x = cx + half_x;
    a.min_z = cz - half_z;
    a.max_z = cz + half_z;
    return a;
}

// Rectángulo de x0..x1 y z0..z1 (en cualquier orden).
ZoneArea rect_span(AreaId id, const char* key, const char* name, Surface surface, double x0,
                   double x1, double z0, double z1) {
    return rect_area(id, key, name, surface, 0.5 * (x0 + x1), 0.5 * (z0 + z1),
                     0.5 * rmath::abs(x1 - x0), 0.5 * rmath::abs(z1 - z0));
}

ZoneArea road_area(AreaId id, const char* key, const char* name, Surface surface,
                   std::vector<AxisSegment> axis, double width, bool closed) {
    ZoneArea a;
    a.id = id;
    a.key = key;
    a.name = name;
    a.surface = surface;
    a.road = true;
    a.axis = std::move(axis);
    a.half_width = 0.5 * width;
    a.closed = closed;
    // Caja por muestreo del eje cada metro (y los extremos de cada tramo).
    a.min_x = a.max_x = a.axis.front().x0;
    a.min_z = a.max_z = a.axis.front().z0;
    for (AxisSegment& seg : a.axis) {
        const int steps = static_cast<int>(rmath::ceil(seg.length));
        seg.min_x = seg.max_x = seg.x0;
        seg.min_z = seg.max_z = seg.z0;
        for (int i = 0; i <= steps; ++i) {
            const AxisPoint p = segment_point(seg, seg.length * i / steps);
            seg.min_x = rmath::min(seg.min_x, p.x);
            seg.max_x = rmath::max(seg.max_x, p.x);
            seg.min_z = rmath::min(seg.min_z, p.z);
            seg.max_z = rmath::max(seg.max_z, p.z);
        }
        // El muestreo de un arco puede quedarse corto por menos de un milímetro.
        seg.min_x -= 0.01;
        seg.max_x += 0.01;
        seg.min_z -= 0.01;
        seg.max_z += 0.01;
        a.min_x = rmath::min(a.min_x, seg.min_x);
        a.max_x = rmath::max(a.max_x, seg.max_x);
        a.min_z = rmath::min(a.min_z, seg.min_z);
        a.max_z = rmath::max(a.max_z, seg.max_z);
    }
    a.min_x -= a.half_width;
    a.max_x += a.half_width;
    a.min_z -= a.half_width;
    a.max_z += a.half_width;
    return a;
}

// Distancia de (x, z) al eje de una calzada, o `cutoff` si es mayor. Los tramos cuya caja
// queda a `best` o más ni se miran: el resultado es el mismo mínimo, bit a bit, siempre que
// sea menor que `cutoff`.
double axis_distance(const ZoneArea& a, double x, double z, double cutoff) {
    double best = cutoff;
    for (const AxisSegment& seg : a.axis) {
        const double bx = rmath::max(0.0, rmath::max(seg.min_x - x, x - seg.max_x));
        const double bz = rmath::max(0.0, rmath::max(seg.min_z - z, z - seg.max_z));
        if (bx >= best || bz >= best || bx * bx + bz * bz >= best * best) {
            continue;
        }
        best = rmath::min(best, segment_distance(seg, x, z));
    }
    return best;
}

bool in_box(const ZoneArea& a, double x, double z, double margin) {
    return x >= a.min_x - margin && x <= a.max_x + margin && z >= a.min_z - margin &&
           z <= a.max_z + margin;
}

double rect_distance(double x, double z, double cx, double cz, double half_x, double half_z) {
    const double qx = rmath::abs(x - cx) - half_x;
    const double qz = rmath::abs(z - cz) - half_z;
    const double ox = rmath::max(qx, 0.0);
    const double oz = rmath::max(qz, 0.0);
    return rmath::sqrt(ox * ox + oz * oz) + rmath::min(rmath::max(qx, qz), 0.0);
}

// Distancia con signo al borde de un área, exacta mientras sea menor que `cutoff`.
double signed_distance_within(const ZoneArea& a, double x, double z, double cutoff) {
    if (!a.road) {
        return rect_distance(x, z, a.cx, a.cz, a.half_x, a.half_z);
    }
    return axis_distance(a, x, z, cutoff + a.half_width) - a.half_width;
}

// Distancia para mezclar h: la de signed_distance, salvo en las áreas que se tocan de punta a
// punta con la siguiente, alargadas kBlendOverlap por su extremo norte.
double blend_distance(const ZoneArea& a, double x, double z) {
    if (a.id == AreaId::Straight || a.id == AreaId::BumpsAsphalt) {
        const double half_z = a.half_z + 0.5 * kBlendOverlap;
        return rect_distance(x, z, a.cx, a.cz - 0.5 * kBlendOverlap, a.half_x, half_z);
    }
    return signed_distance_within(a, x, z, 0.5 * zone::kBlendWidth);
}

// Escritura de JSON con números en su representación más corta (std::to_chars), sin depender
// del idioma de Windows.
class Json {
public:
    void boolean(bool value) {
        comma();
        out_ += value ? "true" : "false";
    }
    void key(const char* name) {
        comma();
        out_ += '"';
        out_ += name;
        out_ += "\":";
        fresh_ = true;
    }
    void string(const char* text) {
        comma();
        out_ += '"';
        out_ += text;
        out_ += '"';
    }
    void number(double value) {
        comma();
        char buffer[32];
        const std::to_chars_result r = std::to_chars(buffer, buffer + sizeof(buffer), value);
        out_.append(buffer, r.ptr);
    }
    void point(double x, double z) {
        begin_array();
        number(x);
        number(z);
        end_array();
    }
    void begin_object() {
        comma();
        out_ += '{';
        fresh_ = true;
    }
    void end_object() {
        out_ += '}';
        fresh_ = false;
    }
    void begin_array() {
        comma();
        out_ += '[';
        fresh_ = true;
    }
    void end_array() {
        out_ += ']';
        fresh_ = false;
    }
    std::string take() { return std::move(out_); }

private:
    void comma() {
        if (!fresh_) {
            out_ += ',';
        }
        fresh_ = false;
    }
    std::string out_;
    bool fresh_ = true;
};

// Desplazamiento lateral de un punto del eje (positivo a la derecha).
AxisPoint offset_point(const AxisPoint& p, double offset) {
    return {p.x + offset * rmath::cos(p.psi), p.z + offset * rmath::sin(p.psi), p.psi};
}

// Puntos del eje cada `step` metros como mucho, con los extremos de cada tramo.
std::vector<AxisPoint> sample_axis(const ZoneArea& area, double step) {
    std::vector<AxisPoint> points;
    for (std::size_t i = 0; i < area.axis.size(); ++i) {
        const AxisSegment& seg = area.axis[i];
        const int steps = rmath::max(1, static_cast<int>(rmath::ceil(seg.length / step)));
        // El primer punto de cada tramo es el último del anterior.
        for (int k = i == 0 ? 0 : 1; k <= steps; ++k) {
            points.push_back(segment_point(seg, seg.length * k / steps));
        }
    }
    if (area.closed) {
        points.pop_back(); // coincide con el primero
    }
    return points;
}

void write_ring(Json& json, const std::vector<AxisPoint>& points, double offset, bool reverse) {
    json.begin_array();
    for (std::size_t i = 0; i < points.size(); ++i) {
        const AxisPoint& p = points[reverse ? points.size() - 1 - i : i];
        const AxisPoint q = offset_point(p, offset);
        json.point(q.x, q.z);
    }
    json.end_array();
}

void write_axis(Json& json, const ZoneArea& area) {
    json.begin_object();
    json.key("width");
    json.number(2.0 * area.half_width);
    json.key("closed");
    json.boolean(area.closed);
    json.key("length");
    json.number(TestZone::axis_length(area));
    json.key("segments");
    json.begin_array();
    for (const AxisSegment& seg : area.axis) {
        json.begin_object();
        json.key("type");
        json.string(seg.radius == 0.0 ? "straight" : "arc");
        json.key("start");
        json.point(seg.x0, seg.z0);
        json.key("heading");
        json.number(seg.psi0);
        json.key("length");
        json.number(seg.length);
        if (seg.radius != 0.0) {
            json.key("center");
            json.point(seg.cx, seg.cz);
            json.key("radius");
            json.number(seg.radius);
            json.key("turn");
            json.number(seg.turn);
        }
        json.end_object();
    }
    json.end_array();
    json.key("points");
    json.begin_array();
    for (const AxisPoint& p : sample_axis(area, 1.0)) {
        json.point(p.x, p.z);
    }
    json.end_array();
    json.end_object();
}

} // namespace

TestZone::TestZone(Surface blind_surface) : blind_surface_(blind_surface) {
    using zone::kHairpinRadius;
    using zone::kHairpinStraight;
    areas_.reserve(kAreaCount);
    areas_.push_back(rect_span(AreaId::Straight, "straight", "recta", Surface::Asphalt,
                               -kStraightHalfWidth, kStraightHalfWidth, kStraightStartZ,
                               kStraightEndZ));
    areas_.push_back(rect_span(AreaId::BumpsAsphalt, "bumps_asphalt", "baches de asfalto",
                               Surface::Asphalt, -kStraightHalfWidth, kStraightHalfWidth,
                               kStraightEndZ, kBumpsSplitZ));
    areas_.push_back(rect_span(AreaId::BumpsGravel, "bumps_gravel", "baches de grava",
                               Surface::Gravel, -kStraightHalfWidth, kStraightHalfWidth,
                               kBumpsSplitZ, kBumpsEndZ));
    areas_.push_back(rect_span(AreaId::Circle, "circle", "círculo", Surface::Gravel, -156.0, -36.0,
                               480.0, 600.0));
    areas_.push_back(rect_span(AreaId::Plain, "plain", "explanada", Surface::Gravel, -116.0, -36.0,
                               40.0, 440.0));
    areas_.push_back(rect_span(
        AreaId::Slalom, "slalom", "eslalon", Surface::Gravel, -56.0, -36.0, 5.0,
        5.0 - zone::kSlalomEntry - (zone::kSlalomCones - 1) * zone::kSlalomSpacing - 50.0));
    {
        // Horquilla: sube hacia el norte por x = -39, gira 180° a la izquierda alrededor de
        // (-49, -460) y vuelve hacia el sur por x = -59.
        AxisBuilder b(-39.0, -360.0, 0.0);
        b.straight(kHairpinStraight);
        b.arc(kHairpinRadius, -kPi);
        b.straight(kHairpinStraight);
        areas_.push_back(road_area(AreaId::Hairpin, "hairpin", "horquilla", Surface::Gravel,
                                   b.take(), zone::kHairpinRoadWidth, false));
    }
    areas_.push_back(rect_span(
        AreaId::Ramp, "ramp", "rampa", Surface::Asphalt, kRampCenterX - 0.5 * zone::kRampWidth,
        kRampCenterX + 0.5 * zone::kRampWidth, kRampStartZ, kRampStartZ - kRampLength));
    areas_.push_back(rect_span(AreaId::Crest, "crest", "cresta", Surface::Gravel,
                               kCrestCenterX - kCrestHalfWidth, kCrestCenterX + kCrestHalfWidth,
                               kCrestStartZ, kCrestStartZ - kCrestTotal));
    areas_.push_back(rect_span(AreaId::PatchDirt, "patch_dirt", "parche de tierra", Surface::Dirt,
                               36.0, 156.0, 480.0, 600.0));
    areas_.push_back(rect_span(AreaId::PatchSnow, "patch_snow", "parche de nieve", Surface::Snow,
                               36.0, 156.0, 320.0, 440.0));
    areas_.push_back(rect_span(AreaId::PatchSand, "patch_sand", "parche de arena", Surface::Sand,
                               36.0, 156.0, 160.0, 280.0));
    {
        // Las rectas de atrás cierran el circuito: la de abajo mide lo que necesita para
        // llegar a la horquilla, que deja la vuelta 24 m más arriba.
        constexpr double kBackStraight = 20.0; // tras la curva rápida
        constexpr double kInner = 20.0;        // tras la horquilla
        // La chicana (izquierda, derecha el doble, izquierda) vuelve a la línea de entrada.
        const double start_leg = -kBlindStartV + 40.0;
        AxisBuilder b(kBlindX, kBlindZ - kBlindStartV, 0.0);
        b.straight(start_leg);
        b.arc(kBlindFastRadius, 0.5 * kPi);
        b.straight(kBackStraight);
        b.arc(kBlindMediumRadius, 0.5 * kPi);
        b.straight(65.0);
        b.arc(kChicaneRadius, -kChicaneAngle);
        b.arc(kChicaneRadius, 2.0 * kChicaneAngle);
        b.arc(kChicaneRadius, -kChicaneAngle);
        b.straight(65.0);
        b.arc(kBlindCornerRadius, 0.5 * kPi);
        b.straight(kBlindFastRadius + kBlindMediumRadius + kBackStraight + kInner);
        b.arc(kBlindHairpinRadius, kPi);
        b.straight(kInner);
        b.arc(kBlindCornerRadius, -0.5 * kPi);
        RALLY_CHECK(rmath::abs(b.x() - kBlindX) < 1e-6 &&
                        rmath::abs(b.z() - (kBlindZ - kBlindStartV)) < 1e-6,
                    "TestZone: la pista ciega no cierra");
        areas_.push_back(road_area(AreaId::BlindTrack, "blind_track", "pista ciega", blind_surface,
                                   b.take(), zone::kBlindRoadWidth, true));
    }
    RALLY_CHECK(static_cast<int>(areas_.size()) == kAreaCount, "TestZone: faltan áreas");

    // Puntos de recolocación: al principio de cada área, mirando hacia donde se recorre.
    constexpr double kNorth = 0.0;
    constexpr double kSouth = kPi;
    constexpr double kEast = -0.5 * kPi;
    respawns_ = {
        {"straight", 0.0, kStraightStartZ - 10.0, kNorth},
        {"bumps", 0.0, kStraightEndZ + 5.0, kNorth},
        {"bumps_return", 0.0, kBumpsEndZ + 5.0, kSouth},
        {"circle", -96.0 + zone::kCircleRadius, 540.0, kNorth},
        {"plain", -76.0, 435.0, kNorth},
        {"slalom", -46.0, 0.0, kNorth},
        {"hairpin", -39.0, -365.0, kNorth},
        {"ramp", kRampCenterX, kRampStartZ - 3.0, kNorth},
        {"crest", kCrestCenterX, kCrestStartZ - 3.0, kNorth},
        {"patch_dirt", 40.0, 540.0, kEast},
        {"patch_snow", 40.0, 380.0, kEast},
        {"patch_sand", 40.0, 220.0, kEast},
        {"blind_track", kBlindX, kBlindZ + 50.0, kNorth},
    };

    finish_.x = kBlindX;
    finish_.z = kBlindZ;
    finish_.forward_x = 0.0;
    finish_.forward_z = -1.0;
    finish_.half_length = 0.5 * zone::kBlindRoadWidth + zone::kFinishMargin;

    // Microrrelieve: por superficie, una onda en cada franja logarítmica de la banda, con
    // posición en la franja, dirección y fase de PCG32. La amplitud da el RMS de partida.
    for (int s = 0; s < kSurfaceCount; ++s) {
        Pcg32 rng(zone::kMicroSeed, static_cast<std::uint64_t>(s));
        const double amplitude =
            surface_rms(static_cast<Surface>(s)) * rmath::sqrt(2.0 / zone::kMicroWaves);
        const double ratio = zone::kMicroMaxWavelength / zone::kMicroMinWavelength;
        for (int i = 0; i < zone::kMicroWaves; ++i) {
            const double band = (i + static_cast<double>(rng.next_float01())) / zone::kMicroWaves;
            const double wavelength = zone::kMicroMinWavelength * rmath::pow(ratio, band);
            const double angle = kTwoPi * static_cast<double>(rng.next_float01());
            const double phase = kTwoPi * static_cast<double>(rng.next_float01());
            const double k = kTwoPi / wavelength;
            waves_[static_cast<std::size_t>(s)][static_cast<std::size_t>(i)] =
                MicroWave{amplitude, k * rmath::cos(angle), k * rmath::sin(angle), phase};
        }
    }
}

double TestZone::surface_rms(Surface surface) {
    switch (surface) {
    case Surface::Asphalt:
        return 0.002;
    case Surface::Snow:
        return 0.004;
    case Surface::Dirt:
        return 0.005;
    case Surface::Gravel:
        return 0.006;
    case Surface::Sand:
        return 0.008;
    }
    return 0.0;
}

double TestZone::ground_height(double x, double z) const {
    double h = 0.0;
    h += strip_height(x, z, 0.0, kStraightHalfWidth, kStraightEndZ, kStraightEndZ - kBumpsSplitZ,
                      bumps_profile);
    h += strip_height(x, z, kRampCenterX, 0.5 * zone::kRampWidth, kRampStartZ, kRampLength,
                      ramp_profile);
    h += strip_height(x, z, kCrestCenterX, kCrestHalfWidth, kCrestStartZ, kCrestTotal,
                      crest_profile);
    // Talud perimetral: sube un metro por metro desde su pie hasta kBermHeight.
    const double beyond = rmath::max(rmath::abs(x), rmath::abs(z)) - zone::kUsableHalfSize;
    h += rmath::clamp(beyond, 0.0, zone::kBermHeight);
    return h;
}

double TestZone::signed_distance(const ZoneArea& area, double x, double z) {
    if (!area.road) {
        return rect_distance(x, z, area.cx, area.cz, area.half_x, area.half_z);
    }
    double d = segment_distance(area.axis.front(), x, z);
    for (std::size_t i = 1; i < area.axis.size(); ++i) {
        d = rmath::min(d, segment_distance(area.axis[i], x, z));
    }
    return d - area.half_width;
}

int TestZone::area_at(double x, double z) const {
    for (int i = kAreaCount - 1; i >= 0; --i) {
        const ZoneArea& a = areas_[static_cast<std::size_t>(i)];
        if (in_box(a, x, z, 0.0) && signed_distance_within(a, x, z, 0.0) < 0.0) {
            return i;
        }
    }
    return -1;
}

Surface TestZone::surface_at(double x, double z) const {
    const int index = area_at(x, z);
    return index < 0 ? Surface::Dirt : areas_[static_cast<std::size_t>(index)].surface;
}

double TestZone::surface_noise(Surface surface, double x, double z) const {
    double h = 0.0;
    for (const MicroWave& w : waves_[static_cast<std::size_t>(surface)]) {
        h += w.amplitude * rmath::sin(w.kx * x + w.kz * z + w.phase);
    }
    return h;
}

double TestZone::micro_height(double x, double z) const { return micro_sample(x, z).micro; }

TestZone::MicroSample TestZone::micro_sample(double x, double z) const {
    // La superficie es la del área de más prioridad que contiene el punto (como area_at).
    // El fondo es tierra; cada área pinta su banda encima con un peso que pasa de 0 a 1 en
    // kBlendWidth alrededor de su borde (smoothstep sobre la distancia con signo).
    constexpr double half_blend = 0.5 * zone::kBlendWidth;
    MicroSample out;
    double h = surface_noise(Surface::Dirt, x, z);
    for (const ZoneArea& a : areas_) {
        if (!in_box(a, x, z, half_blend + kBlendOverlap)) {
            continue;
        }
        const double d = blend_distance(a, x, z);
        if (d >= half_blend) {
            continue;
        }
        // Dentro del área (sin el alargue de la mezcla).
        const bool overlapped = a.id == AreaId::Straight || a.id == AreaId::BumpsAsphalt;
        if ((overlapped ? signed_distance_within(a, x, z, 0.0) : d) < 0.0) {
            out.surface = a.surface;
        }
        const double w = smoothstep01((half_blend - d) / zone::kBlendWidth);
        double target = surface_noise(a.surface, x, z);
        if (a.id == AreaId::BumpsGravel) {
            const double s = kBumpsSplitZ - z;
            target += zone::kRippleAmplitude * rmath::sin(kTwoPi * s / zone::kRippleWavelength);
        }
        h += w * (target - h);
    }
    out.micro = h;
    return out;
}

int TestZone::nearest_respawn(double x, double z) const {
    int best = 0;
    double best_d2 = 0.0;
    for (std::size_t i = 0; i < respawns_.size(); ++i) {
        const double dx = respawns_[i].x - x;
        const double dz = respawns_[i].z - z;
        const double d2 = dx * dx + dz * dz;
        if (i == 0 || d2 < best_d2) {
            best = static_cast<int>(i);
            best_d2 = d2;
        }
    }
    return best;
}

double TestZone::axis_length(const ZoneArea& area) {
    double length = 0.0;
    for (const AxisSegment& seg : area.axis) {
        length += seg.length;
    }
    return length;
}

AxisPoint TestZone::axis_point(const ZoneArea& area, double s) {
    for (const AxisSegment& seg : area.axis) {
        if (s <= seg.length) {
            return segment_point(seg, rmath::max(s, 0.0));
        }
        s -= seg.length;
    }
    const AxisSegment& last = area.axis.back();
    return segment_point(last, last.length);
}

std::vector<AxisPoint> TestZone::slalom_cones() const {
    const ZoneArea& slalom = area(AreaId::Slalom);
    std::vector<AxisPoint> cones;
    for (int i = 0; i < zone::kSlalomCones; ++i) {
        cones.push_back(
            {slalom.cx, slalom.max_z - zone::kSlalomEntry - i * zone::kSlalomSpacing, 0.0});
    }
    return cones;
}

void TestZone::fill_ground(std::vector<float>& heights, std::vector<std::uint8_t>& surfaces) const {
    constexpr int n = zone::kGroundSamples;
    heights.resize(static_cast<std::size_t>(n) * n);
    surfaces.resize(static_cast<std::size_t>(n - 1) * (n - 1));
    for (int iz = 0; iz < n; ++iz) {
        const double z = zone::kGroundOrigin + iz * zone::kGroundSpacing;
        for (int ix = 0; ix < n; ++ix) {
            const double x = zone::kGroundOrigin + ix * zone::kGroundSpacing;
            heights[static_cast<std::size_t>(iz) * n + ix] =
                static_cast<float>(ground_height(x, z));
        }
    }
    for (int iz = 0; iz < n - 1; ++iz) {
        const double z = zone::kGroundOrigin + (iz + 0.5) * zone::kGroundSpacing;
        for (int ix = 0; ix < n - 1; ++ix) {
            const double x = zone::kGroundOrigin + (ix + 0.5) * zone::kGroundSpacing;
            surfaces[static_cast<std::size_t>(iz) * (n - 1) + ix] =
                static_cast<std::uint8_t>(surface_at(x, z));
        }
    }
}

std::string TestZone::to_json() const {
    Json json;
    json.begin_object();
    json.key("format");
    json.number(1);
    json.key("axes");
    json.string("x east, z south, metres; forward is -z; heading 0 = -z, positive turns right");
    json.key("ground");
    json.begin_object();
    json.key("samples");
    json.number(zone::kGroundSamples);
    json.key("spacing");
    json.number(zone::kGroundSpacing);
    json.key("origin");
    json.point(zone::kGroundOrigin, zone::kGroundOrigin);
    json.key("background");
    json.string(surface_key(Surface::Dirt));
    json.end_object();
    json.key("blind_surface");
    json.string(surface_key(blind_surface_));

    const ZoneArea& circle = area(AreaId::Circle);
    json.key("circle");
    json.begin_object();
    json.key("center");
    json.point(circle.cx, circle.cz);
    json.key("radius");
    json.number(zone::kCircleRadius);
    json.end_object();

    json.key("areas");
    json.begin_array();
    for (const ZoneArea& a : areas_) {
        json.begin_object();
        json.key("name");
        json.string(a.key);
        json.key("surface");
        json.string(surface_key(a.surface));
        // Anillos del polígono: el primero es el contorno exterior; los demás, huecos.
        json.key("polygon");
        json.begin_array();
        if (!a.road) {
            json.begin_array();
            json.point(a.min_x, a.min_z);
            json.point(a.max_x, a.min_z);
            json.point(a.max_x, a.max_z);
            json.point(a.min_x, a.max_z);
            json.end_array();
        } else {
            const std::vector<AxisPoint> points = sample_axis(a, 1.0);
            if (a.closed) {
                // Circuito en el sentido de las agujas del reloj: el exterior queda a la
                // izquierda del eje.
                write_ring(json, points, -a.half_width, false);
                write_ring(json, points, a.half_width, false);
            } else {
                json.begin_array();
                for (const AxisPoint& p : points) {
                    const AxisPoint q = offset_point(p, -a.half_width);
                    json.point(q.x, q.z);
                }
                for (std::size_t i = points.size(); i-- > 0;) {
                    const AxisPoint q = offset_point(points[i], a.half_width);
                    json.point(q.x, q.z);
                }
                json.end_array();
            }
        }
        json.end_array();
        json.end_object();
    }
    json.end_array();

    json.key("hairpin_axis");
    write_axis(json, area(AreaId::Hairpin));
    json.key("blind_track_axis");
    write_axis(json, area(AreaId::BlindTrack));

    json.key("finish_line");
    json.begin_object();
    json.key("center");
    json.point(finish_.x, finish_.z);
    json.key("forward");
    json.point(finish_.forward_x, finish_.forward_z);
    json.key("half_length");
    json.number(finish_.half_length);
    json.end_object();

    json.key("respawns");
    json.begin_array();
    for (const RespawnPoint& r : respawns_) {
        json.begin_object();
        json.key("name");
        json.string(r.key);
        json.key("position");
        json.point(r.x, r.z);
        json.key("yaw");
        json.number(r.yaw);
        json.end_object();
    }
    json.end_array();

    json.key("ramp");
    json.begin_object();
    json.key("grade");
    json.number(zone::kRampGrade);
    json.key("run");
    json.number(zone::kRampRun);
    json.key("height");
    json.number(ground_height(kRampCenterX,
                              kRampStartZ - kRampLead - zone::kRampRun - 0.5 * zone::kRampLanding));
    json.end_object();
    json.key("crest");
    json.begin_object();
    json.key("height");
    json.number(ground_height(kCrestCenterX, kCrestStartZ - kCrestLead - 0.5 * zone::kCrestLength));
    json.key("length");
    json.number(zone::kCrestLength);
    json.end_object();

    json.key("slalom_cones");
    json.begin_array();
    for (const AxisPoint& c : slalom_cones()) {
        json.point(c.x, c.z);
    }
    json.end_array();
    json.end_object();
    return json.take();
}

namespace {

void add_ground(Fnv1a64& hash, const std::vector<float>& heights,
                const std::vector<std::uint8_t>& surfaces) {
    for (const float h : heights) {
        std::uint32_t bits = 0;
        std::memcpy(&bits, &h, sizeof(bits));
        hash.add_u32(bits);
    }
    for (const std::uint8_t s : surfaces) {
        hash.add_byte(s);
    }
}

void add_double(Fnv1a64& hash, double value) {
    std::uint64_t bits = 0;
    std::memcpy(&bits, &value, sizeof(bits));
    hash.add_u32(static_cast<std::uint32_t>(bits));
    hash.add_u32(static_cast<std::uint32_t>(bits >> 32));
}

} // namespace

std::uint64_t ground_hash(const std::vector<float>& heights,
                          const std::vector<std::uint8_t>& surfaces) {
    Fnv1a64 hash;
    add_ground(hash, heights, surfaces);
    return hash.value();
}

std::uint64_t zone_hash(const std::vector<float>& heights,
                        const std::vector<std::uint8_t>& surfaces, const TestZone& zone) {
    Fnv1a64 hash;
    add_ground(hash, heights, surfaces);
    for (const auto& waves : zone.waves()) {
        for (const MicroWave& w : waves) {
            add_double(hash, w.amplitude);
            add_double(hash, w.kx);
            add_double(hash, w.kz);
            add_double(hash, w.phase);
        }
    }
    add_double(hash, zone::kRippleWavelength);
    add_double(hash, zone::kRippleAmplitude);
    return hash.value();
}

std::array<Surface, kBlindTrials> blind_series(std::uint64_t seed) {
    std::array<Surface, kBlindTrials> series{};
    for (int i = 0; i < kBlindTrials; ++i) {
        series[static_cast<std::size_t>(i)] = static_cast<Surface>(i % kSurfaceCount);
    }
    // Stream propio para que la serie no comparta secuencia con otros usos de la semilla.
    Pcg32 rng(seed, 0x51e5);
    for (int i = kBlindTrials - 1; i > 0; --i) {
        const auto j = static_cast<int>(rng.bounded(static_cast<std::uint32_t>(i + 1)));
        std::swap(series[static_cast<std::size_t>(i)], series[static_cast<std::size_t>(j)]);
    }
    return series;
}

} // namespace rally::sim
