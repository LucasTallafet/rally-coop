#include "sim/test_road.h"

#include "sim/check.h"
#include "sim/pcg32.h"
#include "sim/rmath.h"

namespace rally::sim {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kDeg = kPi / 180.0;

// Junta de las dos patas de la carretera (hasta la bajada y desde la horquilla), que se acercan
// junto a la horquilla: en trinchera las dos, gana la más baja; en terraplén, la más alta; con
// signos distintos, la suma. Es continua (en h = 0 las tres coinciden) y no tiene más pendiente
// que la de cada pata donde las dos tienen el mismo signo.
double join_legs(double a, double b) {
    if (a <= 0.0 && b <= 0.0) {
        return rmath::min(a, b);
    }
    if (a >= 0.0 && b >= 0.0) {
        return rmath::max(a, b);
    }
    return a + b;
}

double clamp01(double t) { return rmath::clamp(t, 0.0, 1.0); }

// Entrada en pendiente: 0 hasta u = 0, parábola hasta u = w y después u − w/2 (la curva vertical
// queda dentro del tramo con pendiente y conserva las tangentes).
double ramp_in(double u, double w) {
    if (u <= 0.0) {
        return 0.0;
    }
    if (u < w) {
        return u * u / (2.0 * w);
    }
    return u - 0.5 * w;
}

// max(0, s) con una parábola de longitud `width` alrededor de 0.
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

// Fracción del talud recorrida a una distancia `a` del eje: 0 hasta el final de la berma y 1
// desde kRoadFootprint, lineal entre los redondeos.
double talud_fraction(double a) {
    const double u = a - zone::kDitchEdge - zone::kRoadBerm - 0.5 * zone::kRoadTaludRound;
    return (soft_ramp(u, zone::kRoadTaludRound) -
            soft_ramp(u - zone::kRoadTalud, zone::kRoadTaludRound)) /
           zone::kRoadTalud;
}

// Fondo de la cuneta bajo el plano de la calzada (negativo), con u desde el borde del arcén.
double ditch_depth(double u) {
    const double center = zone::kDitchSide + 0.5 * zone::kDitchFlat;
    const double e = rmath::abs(u - center) - 0.5 * zone::kDitchFlat;
    if (e <= 0.0) {
        return -zone::kDitchDepth;
    }
    if (e >= zone::kDitchSide) {
        return 0.0;
    }
    return -zone::kDitchDepth * 0.5 * (1.0 + rmath::cos(kPi * e / zone::kDitchSide));
}

// Cresta de tres parábolas (vaguada, cumbre, vaguada), con u desde su inicio.
double crest_profile(double u) {
    constexpr double length = zone::kRoadCrestLength;
    if (u <= 0.0 || u >= length) {
        return 0.0;
    }
    constexpr double half = 0.5 * length;
    constexpr double sag = zone::kRoadCrestSag;
    constexpr double grade = 2.0 * zone::kRoadCrestHeight / half; // pendiente máxima
    constexpr double k_sag = grade / sag;
    constexpr double k_top = grade / (half - sag);
    const double v = rmath::min(u, length - u);
    if (v <= sag) {
        return 0.5 * k_sag * v * v;
    }
    const double w = v - sag;
    return 0.5 * k_sag * sag * sag + grade * w - 0.5 * k_top * w * w;
}

struct PieceDesc {
    RoadPiece piece;
    const char* key;
    double length; // rectas
    double radius; // arcos (0 en las rectas)
    double turn;   // con signo, positivo a la derecha
    double bank;   // peralte pleno hacia el interior
};

// Secuencia de la carretera en el sentido de la marcha (prompt 2.6b).
constexpr PieceDesc kPieces[kRoadPieceCount] = {
    {RoadPiece::LeadIn, "lead_in", zone::kRoadLeadIn, 0.0, 0.0, 0.0},
    {RoadPiece::Start, "start", 150.0, 0.0, 0.0, 0.0},
    {RoadPiece::FastRight, "fast_right", 0.0, 150.0, 40.0 * kDeg, -0.03},
    {RoadPiece::Straight80, "straight_80", 80.0, 0.0, 0.0, 0.0},
    {RoadPiece::Tightening50, "tightening_50", 0.0, 50.0, -60.0 * kDeg, 0.04},
    {RoadPiece::Tightening35, "tightening_35", 0.0, 35.0, -30.0 * kDeg, 0.04},
    {RoadPiece::SRight, "s_right", 0.0, 40.0, 45.0 * kDeg, 0.0},
    {RoadPiece::SLeft, "s_left", 0.0, 40.0, -45.0 * kDeg, 0.0},
    {RoadPiece::Crest, "crest", zone::kRoadCrestLength, 0.0, 0.0, 0.0},
    {RoadPiece::CrestLeft, "crest_left", 0.0, 60.0, -50.0 * kDeg, 0.04},
    {RoadPiece::Descent, "descent", 200.0, 0.0, 0.0, 0.0},
    {RoadPiece::Hairpin, "hairpin", 0.0, 12.0, 180.0 * kDeg, 0.0},
    {RoadPiece::ClimbLeft, "climb_left", 0.0, 90.0, -60.0 * kDeg, 0.0},
    {RoadPiece::Finish, "finish", 100.0, 0.0, 0.0, 0.0},
    {RoadPiece::RunOff, "run_off", zone::kRoadRunOff, 0.0, 0.0, 0.0},
};

} // namespace

TestRoad::TestRoad() {
    AxisBuilder b(zone::kRoadStartX, zone::kRoadStartZ + zone::kRoadLeadIn, 0.0);
    for (const PieceDesc& p : kPieces) {
        if (p.radius == 0.0) {
            b.straight(p.length);
        } else {
            b.arc(p.radius, p.turn);
        }
    }
    std::vector<AxisSegment> axis = b.take();
    fit_segment_boxes(axis);
    double s = -zone::kRoadLeadIn;
    segments_.reserve(kRoadPieceCount);
    for (int i = 0; i < kRoadPieceCount; ++i) {
        const PieceDesc& p = kPieces[i];
        RALLY_CHECK(static_cast<int>(p.piece) == i, "TestRoad: piezas fuera de orden");
        RoadSegment seg;
        seg.piece = p.piece;
        seg.key = p.key;
        seg.s0 = s;
        seg.bank = p.bank;
        seg.axis = axis[static_cast<std::size_t>(i)];
        segments_.push_back(seg);
        s += seg.axis.length;
    }
    length_ = segment(RoadPiece::RunOff).s0;

    // Muestras del eje cada metro, para nearest_s y la caja.
    const int count = static_cast<int>(rmath::floor(last_s() - first_s())) + 1;
    samples_.reserve(static_cast<std::size_t>(count) + 1);
    for (int i = 0; i < count; ++i) {
        samples_.push_back(point(first_s() + i));
    }
    if (samples_.back().s < last_s()) {
        samples_.push_back(point(last_s()));
    }
    min_x_ = max_x_ = samples_.front().x;
    min_z_ = max_z_ = samples_.front().z;
    for (const RoadPoint& p : samples_) {
        min_x_ = rmath::min(min_x_, p.x);
        max_x_ = rmath::max(max_x_, p.x);
        min_z_ = rmath::min(min_z_, p.z);
        max_z_ = rmath::max(max_z_, p.z);
    }
    // Media muestra de holgura en los arcos, más el talud.
    const double margin = zone::kRoadFootprint + 1.0;
    min_x_ -= margin;
    max_x_ += margin;
    min_z_ -= margin;
    max_z_ += margin;

    dense_ = {
        // Exterior de la curva 4 (izquierda: el exterior es la derecha) y de la horquilla
        // (derecha: el exterior es la izquierda).
        {1, segment(RoadPiece::Tightening50).s0 - zone::kTreeDenseMargin,
         segment(RoadPiece::SRight).s0 + zone::kTreeDenseMargin},
        {-1, segment(RoadPiece::Hairpin).s0 - zone::kTreeDenseMargin,
         segment(RoadPiece::ClimbLeft).s0 + zone::kTreeDenseMargin},
    };
    place_trees();
}

std::vector<AxisSegment> TestRoad::axis() const {
    std::vector<AxisSegment> axis;
    axis.reserve(segments_.size());
    for (const RoadSegment& seg : segments_) {
        axis.push_back(seg.axis);
    }
    return axis;
}

const RoadSegment& TestRoad::segment_at(double s) const {
    const RoadSegment* seg = &segments_.front();
    for (const RoadSegment& candidate : segments_) {
        if (s >= candidate.s0) {
            seg = &candidate;
        }
    }
    return *seg;
}

RoadPoint TestRoad::point(double s) const {
    const RoadSegment* seg = &segment_at(s);
    const double u = rmath::clamp(s - seg->s0, 0.0, seg->axis.length);
    const AxisPoint p = segment_point(seg->axis, u);
    return {s, p.x, p.z, p.psi, axis_height(s), bank(s)};
}

double TestRoad::axis_height(double s) const {
    constexpr double w = zone::kGradeTransition;
    const RoadSegment& descent = segment(RoadPiece::Descent);
    const RoadSegment& climb = segment(RoadPiece::ClimbLeft);
    const RoadSegment& crest = segment(RoadPiece::Crest);
    const double d0 = descent.s0;
    const double d1 = d0 + descent.axis.length - w;
    const double c0 = climb.s0;
    const double c1 = c0 + climb.axis.length - w;
    return zone::kRoadBaseHeight + crest_profile(s - crest.s0) -
           zone::kDescentGrade * (ramp_in(s - d0, w) - ramp_in(s - d1, w)) +
           zone::kClimbGrade * (ramp_in(s - c0, w) - ramp_in(s - c1, w));
}

double TestRoad::bank(double s) const {
    // Los arcos seguidos con el mismo peralte (la curva 4) forman una sola curva.
    constexpr double half = 0.5 * zone::kBankTransition;
    double slope = 0.0;
    for (std::size_t i = 0; i < segments_.size(); ++i) {
        const RoadSegment& seg = segments_[i];
        if (seg.bank == 0.0 || (i > 0 && segments_[i - 1].bank == seg.bank)) {
            continue;
        }
        std::size_t last = i;
        while (last + 1 < segments_.size() && segments_[last + 1].bank == seg.bank) {
            ++last;
        }
        const double s0 = seg.s0;
        const double s1 = segments_[last].s0 + segments_[last].axis.length;
        const double weight = clamp01((s - (s0 - half)) / zone::kBankTransition) *
                              clamp01(((s1 + half) - s) / zone::kBankTransition);
        // Hacia el interior: en una curva a la derecha (turn > 0) baja hacia la derecha.
        const double inward = seg.axis.turn > 0.0 ? -1.0 : 1.0;
        slope += inward * seg.bank * weight;
    }
    return slope;
}

TestRoad::Candidate TestRoad::project(const RoadSegment& seg, double x, double z) const {
    const AxisSegment& a = seg.axis;
    Candidate c;
    if (a.radius == 0.0) {
        const double dx = rmath::sin(a.psi0);
        const double dz = -rmath::cos(a.psi0);
        const double u = rmath::clamp((x - a.x0) * dx + (z - a.z0) * dz, 0.0, a.length);
        const double px = x - (a.x0 + u * dx);
        const double pz = z - (a.z0 + u * dz);
        c.distance = rmath::sqrt(px * px + pz * pz);
        c.s = seg.s0 + u;
        c.at_start = u <= 0.0;
        c.at_end = u >= a.length;
        // A la derecha: (cos psi, sin psi).
        c.t = px * rmath::cos(a.psi0) + pz * rmath::sin(a.psi0) < 0.0 ? -c.distance : c.distance;
        return c;
    }
    const double side = a.turn > 0.0 ? 1.0 : -1.0;
    const double vx = x - a.cx;
    const double vz = z - a.cz;
    const double r = rmath::sqrt(vx * vx + vz * vz);
    const double psi = rmath::atan2(-side * vz, -side * vx);
    const double delta = wrap_angle(psi - a.psi0, 0.5 * a.turn - kPi);
    const bool inside =
        a.turn > 0.0 ? delta >= 0.0 && delta <= a.turn : delta <= 0.0 && delta >= a.turn;
    if (inside) {
        c.distance = rmath::abs(r - a.radius);
        c.s = seg.s0 + rmath::abs(delta) * a.radius;
        // El centro queda a la derecha en una curva a la derecha.
        c.t = side * (a.radius - r);
        return c;
    }
    // Fuera del abanico del arco: el extremo más cercano.
    const AxisPoint p0 = segment_point(a, 0.0);
    const AxisPoint p1 = segment_point(a, a.length);
    const double d0 = rmath::sqrt((x - p0.x) * (x - p0.x) + (z - p0.z) * (z - p0.z));
    const double d1 = rmath::sqrt((x - p1.x) * (x - p1.x) + (z - p1.z) * (z - p1.z));
    const AxisPoint& p = d0 <= d1 ? p0 : p1;
    c.distance = d0 <= d1 ? d0 : d1;
    c.s = seg.s0 + (d0 <= d1 ? 0.0 : a.length);
    c.at_start = d0 <= d1;
    c.at_end = !c.at_start;
    const double lateral = (x - p.x) * rmath::cos(p.psi) + (z - p.z) * rmath::sin(p.psi);
    c.t = lateral < 0.0 ? -c.distance : c.distance;
    return c;
}

double TestRoad::section_height(double s, double t) const {
    const double a = rmath::abs(t);
    if (a >= zone::kRoadFootprint) {
        return 0.0;
    }
    const double plane =
        axis_height(s) + bank(s) * rmath::clamp(t, -zone::kDitchEdge, zone::kDitchEdge);
    if (a <= zone::kShoulderEdge) {
        return plane;
    }
    if (a < zone::kDitchEdge) {
        return plane + ditch_depth(a - zone::kShoulderEdge);
    }
    const double f = talud_fraction(a);
    if (f >= 1.0) {
        return 0.0;
    }
    return plane * (1.0 - f);
}

double TestRoad::height(double x, double z) const {
    if (x < min_x_ || x > max_x_ || z < min_z_ || z > max_z_) {
        return 0.0;
    }
    // La sección del tramo más cercano de cada pata: hasta la bajada (A) y desde la horquilla
    // (B). Dentro de una pata, en la junta de dos tramos coinciden punto, s y lateral. El final
    // de la bajada y el inicio de la horquilla no cuentan como extremos de su pata: más allá de
    // la junta manda la otra (y en la línea de la junta las dos dan lo mismo).
    const std::size_t split = static_cast<std::size_t>(RoadPiece::Hairpin);
    Candidate best[2];
    bool found[2] = {false, false};
    for (std::size_t i = 0; i < segments_.size(); ++i) {
        const AxisSegment& a = segments_[i].axis;
        const double bx = rmath::max(0.0, rmath::max(a.min_x - x, x - a.max_x));
        const double bz = rmath::max(0.0, rmath::max(a.min_z - z, z - a.max_z));
        if (bx * bx + bz * bz >= zone::kRoadFootprint * zone::kRoadFootprint) {
            continue;
        }
        const int leg = i < split ? 0 : 1;
        const Candidate c = project(segments_[i], x, z);
        if ((i + 1 == split && c.at_end) || (i == split && c.at_start)) {
            continue;
        }
        if (!found[leg] || c.distance < best[leg].distance) {
            best[leg] = c;
            found[leg] = true;
        }
    }
    for (int leg = 0; leg < 2; ++leg) {
        found[leg] = found[leg] && best[leg].distance < zone::kRoadFootprint;
    }
    if (!found[0] && !found[1]) {
        return 0.0;
    }
    if (!found[0] || !found[1]) {
        const Candidate& c = found[0] ? best[0] : best[1];
        return section_height(c.s, c.t);
    }
    return join_legs(section_height(best[0].s, best[0].t), section_height(best[1].s, best[1].t));
}

double TestRoad::nearest_s(double x, double z, double& distance) const {
    double best_d2 = 0.0;
    double best_s = samples_.front().s;
    for (std::size_t i = 0; i < samples_.size(); ++i) {
        const double dx = samples_[i].x - x;
        const double dz = samples_[i].z - z;
        const double d2 = dx * dx + dz * dz;
        if (i == 0 || d2 < best_d2) {
            best_d2 = d2;
            best_s = samples_[i].s;
        }
    }
    distance = rmath::sqrt(best_d2);
    return best_s;
}

void TestRoad::place_trees() {
    // Por lado (izquierda y después derecha), a lo largo de la carretera: separación medida en
    // la fila (más densa en los tramos de dense_) y distancia lateral, de PCG32. Los sorteos se
    // hacen siempre; un árbol que caería sobre otra parte de la carretera (o en el interior de
    // la horquilla) no se crea.
    for (const int side : {-1, 1}) {
        Pcg32 rng(zone::kTreeSeed, side < 0 ? 0u : 1u);
        double s = first_s();
        while (true) {
            bool dense = false;
            for (const DenseRange& d : dense_) {
                dense = dense || (d.side == side && s >= d.s0 && s <= d.s1);
            }
            const double lo = dense ? zone::kTreeDenseGapMin : zone::kTreeGapMin;
            const double hi = dense ? zone::kTreeDenseGapMax : zone::kTreeGapMax;
            const double gap = lo + (hi - lo) * static_cast<double>(rng.next_float01());
            const double lateral =
                zone::kTreeInner + zone::kTreeBand * static_cast<double>(rng.next_float01());
            // La separación es la de la fila: en un arco, la fila tiene otro radio.
            const AxisSegment& here = segment_at(s).axis;
            double scale = 1.0;
            if (here.radius != 0.0) {
                // turn > 0: centro a la derecha (side = +1 por dentro).
                const double inward = here.turn > 0.0 ? 1.0 : -1.0;
                const double row_radius = here.radius - inward * side * lateral;
                scale = here.radius / rmath::max(row_radius, 0.25 * here.radius);
            }
            s += gap * scale;
            if (s > last_s()) {
                break;
            }
            const RoadPoint p = point(s);
            const double t = side * lateral;
            const double x = p.x + t * rmath::cos(p.psi);
            const double z = p.z + t * rmath::sin(p.psi);
            double axis_d = 1e18;
            for (const RoadSegment& other : segments_) {
                axis_d = rmath::min(axis_d, segment_distance(other.axis, x, z));
            }
            // Ninguno dentro del círculo de un arco cerrado (la horquilla): ni en su isla ni en
            // su boca, entre las dos patas.
            bool clear = axis_d >= zone::kTreeInner;
            for (const RoadSegment& other : segments_) {
                const AxisSegment& arc = other.axis;
                if (arc.radius != 0.0 && arc.radius < 2.0 * zone::kTreeInner) {
                    const double dx = x - arc.cx;
                    const double dz = z - arc.cz;
                    clear = clear && dx * dx + dz * dz >= arc.radius * arc.radius;
                }
            }
            for (const TreeSpot& tree : trees_) {
                const double dx = tree.x - x;
                const double dz = tree.z - z;
                clear = clear && dx * dx + dz * dz >= 1.0;
            }
            if (clear) {
                trees_.push_back({x, z, height(x, z)});
            }
        }
    }
}

} // namespace rally::sim
