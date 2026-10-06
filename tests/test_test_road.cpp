#include "car_worlds.h"
#include "io/recording.h"
#include "io/replay.h"
#include "jolt_allocation_counter.h"
#include "new_counter.h"
#include "sim/demo_world.h"
#include "sim/jolt_runtime.h"
#include "sim/lap_timer.h"
#include "sim/sim_input.h"
#include "sim/sim_world.h"
#include "sim/test_road.h"
#include "sim/test_zone.h"

#include <Jolt/Jolt.h>
#include <Jolt/Physics/Body/Body.h>
#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

// Carretera de prueba de la zona (tarea 2.6b): geometría medida en el suelo de Jolt, lo que la
// rejilla de 0,75 m deja del arcén y de la cuneta, terreno, árboles, cronómetro de punto a
// punto y recolocación.

namespace {

using rally::sim::AreaId;
using rally::sim::kAxisMax;
using rally::sim::RoadPiece;
using rally::sim::RoadPoint;
using rally::sim::RoadTimer;
using rally::sim::SimInput;
using rally::sim::SimWorld;
using rally::sim::Surface;
using rally::sim::TestRoad;
using rally::sim::TestZone;
using rally::sim::WorldDesc;
namespace zone = rally::sim::zone;

constexpr double kPi = 3.14159265358979323846;

// Hash de la zona entera con la pista ciega y la carretera de grava (suelo, h, muros y
// árboles). Igual en Debug y en Release; si un cambio intencionado de la zona lo altera, se
// copia aquí el nuevo y se dice en el commit.
constexpr std::uint64_t kExpectedZoneHash = 0x0e45e3b127ff4a6bull;

double wrap(double a) {
    while (a > kPi) {
        a -= 2.0 * kPi;
    }
    while (a < -kPi) {
        a += 2.0 * kPi;
    }
    return a;
}

double car_heading(const rally::sim::BodyPose& p) {
    const double fx = -2.0 * (p.qx * p.qz + p.qw * p.qy);
    const double fz = -(1.0 - 2.0 * (p.qx * p.qx + p.qy * p.qy));
    return std::atan2(fx, -fz);
}

// Punto a un lateral t (positivo a la derecha) de un punto del eje.
void lateral_point(const RoadPoint& p, double t, double& x, double& z) {
    x = p.x + t * std::cos(p.psi);
    z = p.z + t * std::sin(p.psi);
}

// Mundo de la zona construido una vez para los tests que solo sondean el suelo.
struct ZoneGround {
    rally::sim::JoltRuntime runtime;
    SimWorld world;
    explicit ZoneGround(Surface road = Surface::Gravel) {
        world.build(rally::sim::zone_world(Surface::Gravel, rally::tests::reference_car(),
                                           rally::sim::ZoneStart::Straight, road));
        world.seal();
    }
    const TestZone& zone() const { return *world.zone(); }
    const TestRoad& road() const { return world.zone()->road(); }
};

// Parámetros u ∈ [0, 1] del segmento (x0, z0) → (x1, z1) en los que cruza una arista de los
// triángulos del suelo de Jolt (líneas de la rejilla en x y en z y diagonales de (ix, iz) a
// (ix + 1, iz + 1)), con los extremos. Entre dos seguidos, la superficie es la de una celda y
// la altura es lineal: con ellos, las medidas en el suelo de Jolt son exactas.
std::vector<double> grid_breaks(double x0, double z0, double x1, double z1) {
    const double g = zone::kGroundSpacing;
    const double o = zone::kGroundOrigin;
    std::vector<double> u = {0.0, 1.0};
    const auto add_family = [&u](double a0, double a1) {
        // Cruces de a(u) = a0 + u (a1 − a0) con los enteros.
        if (a0 == a1) {
            return;
        }
        const double lo = std::min(a0, a1);
        const double hi = std::max(a0, a1);
        for (double k = std::ceil(lo); k <= hi; k += 1.0) {
            const double t = (k - a0) / (a1 - a0);
            if (t > 0.0 && t < 1.0) {
                u.push_back(t);
            }
        }
    };
    add_family((x0 - o) / g, (x1 - o) / g);
    add_family((z0 - o) / g, (z1 - o) / g);
    add_family((x0 - o) / g - (z0 - o) / g, (x1 - o) / g - (z1 - o) / g);
    std::sort(u.begin(), u.end());
    return u;
}

// Largo de arena en la normal del eje en s, del lado `side`, medido en el suelo de Jolt.
double sand_width(const SimWorld& world, const TestRoad& road, double s, int side) {
    const RoadPoint p = road.point(s);
    double x0 = 0.0;
    double z0 = 0.0;
    double x1 = 0.0;
    double z1 = 0.0;
    lateral_point(p, side * (zone::kRoadHalfWidth - 1.0), x0, z0);
    lateral_point(p, side * (zone::kShoulderEdge + 1.0), x1, z1);
    const double length = std::hypot(x1 - x0, z1 - z0);
    const std::vector<double> u = grid_breaks(x0, z0, x1, z1);
    double sand = 0.0;
    for (std::size_t i = 0; i + 1 < u.size(); ++i) {
        const double m = 0.5 * (u[i] + u[i + 1]);
        const rally::sim::GroundProbe probe =
            world.probe_ground(x0 + m * (x1 - x0), z0 + m * (z1 - z0));
        if (probe.hit && probe.surface == Surface::Sand) {
            sand += (u[i + 1] - u[i]) * length;
        }
    }
    return sand;
}

// Fondo de la cuneta bajo el plano de la calzada en la normal del eje en s (suelo de Jolt).
double ditch_depth(const SimWorld& world, const TestRoad& road, double s, int side) {
    const RoadPoint p = road.point(s);
    const double t0 = side * zone::kShoulderEdge;
    const double t1 = side * zone::kDitchEdge;
    double x0 = 0.0;
    double z0 = 0.0;
    double x1 = 0.0;
    double z1 = 0.0;
    lateral_point(p, t0, x0, z0);
    lateral_point(p, t1, x1, z1);
    double depth = 0.0;
    for (const double u : grid_breaks(x0, z0, x1, z1)) {
        const rally::sim::GroundProbe probe =
            world.probe_ground(x0 + u * (x1 - x0), z0 + u * (z1 - z0));
        REQUIRE(probe.hit);
        const double t = t0 + u * (t1 - t0);
        depth = std::max(depth, p.height + p.bank * t - probe.y);
    }
    return depth;
}

// Ajuste de circunferencia por mínimos cuadrados (Kåsa): radio.
double fit_radius(const std::vector<std::array<double, 2>>& pts) {
    double mx = 0.0;
    double mz = 0.0;
    for (const auto& p : pts) {
        mx += p[0];
        mz += p[1];
    }
    mx /= static_cast<double>(pts.size());
    mz /= static_cast<double>(pts.size());
    double suu = 0.0, svv = 0.0, suv = 0.0, suuu = 0.0, svvv = 0.0, suvv = 0.0, svuu = 0.0;
    for (const auto& p : pts) {
        const double u = p[0] - mx;
        const double v = p[1] - mz;
        suu += u * u;
        svv += v * v;
        suv += u * v;
        suuu += u * u * u;
        svvv += v * v * v;
        suvv += u * v * v;
        svuu += v * u * u;
    }
    const double b1 = 0.5 * (suuu + suvv);
    const double b2 = 0.5 * (svvv + svuu);
    const double det = suu * svv - suv * suv;
    const double uc = (b1 * svv - b2 * suv) / det;
    const double vc = (suu * b2 - suv * b1) / det;
    const double n = static_cast<double>(pts.size());
    return std::sqrt(uc * uc + vc * vc + (suu + svv) / n);
}

// Lateral (en la normal del eje en p, del lado `side`) en el que la ladera interior de la
// cuneta baja la mitad de su fondo bajo el plano de la calzada, en el suelo de Jolt: la altura
// es lineal entre los cruces con las aristas, así que se interpola entre ellos. Nominal:
// kShoulderEdge + kDitchSide / 2.
double ditch_side(const SimWorld& world, const RoadPoint& p, int side) {
    double x0 = 0.0;
    double z0 = 0.0;
    double x1 = 0.0;
    double z1 = 0.0;
    const double t0 = side * (zone::kShoulderEdge - 0.4);
    const double t1 = side * (zone::kShoulderEdge + zone::kDitchSide + 0.2);
    lateral_point(p, t0, x0, z0);
    lateral_point(p, t1, x1, z1);
    double prev_t = t0;
    double prev_depth = 0.0;
    bool first = true;
    for (const double u : grid_breaks(x0, z0, x1, z1)) {
        const rally::sim::GroundProbe probe =
            world.probe_ground(x0 + u * (x1 - x0), z0 + u * (z1 - z0));
        const double t = t0 + u * (t1 - t0);
        const double depth = (p.height + p.bank * t - probe.y) - 0.5 * zone::kDitchDepth;
        if (!first && prev_depth < 0.0 && depth >= 0.0) {
            return prev_t + (t - prev_t) * (-prev_depth) / (depth - prev_depth);
        }
        prev_t = t;
        prev_depth = depth;
        first = false;
    }
    return t1;
}

} // namespace

TEST_CASE("test_road: the axis measures 992.5 m and each corner keeps its radius in Jolt") {
    const ZoneGround g;
    const TestRoad& road = g.road();
    // Eje medido en el suelo: el punto medio entre los bordes de la calzada, cada 5 m.
    const auto measured_axis = [&](double s) {
        const RoadPoint p = road.point(s);
        const double mid = 0.5 * (ditch_side(g.world, p, 1) + ditch_side(g.world, p, -1));
        std::array<double, 2> q{};
        lateral_point(p, mid, q[0], q[1]);
        return q;
    };
    double length = 0.0;
    std::array<double, 2> prev = measured_axis(0.0);
    for (double s = 5.0; s <= road.length() + 1e-9; s += 5.0) {
        const std::array<double, 2> q = measured_axis(std::min(s, road.length()));
        length += std::hypot(q[0] - prev[0], q[1] - prev[1]);
        prev = q;
    }
    MESSAGE("eje de salida a meta: nominal ", road.length(), " m, medido ", length, " m");
    // Cada pieza, medida igual (cada metro).
    for (const rally::sim::RoadSegment& seg : road.segments()) {
        double piece = 0.0;
        std::array<double, 2> a = measured_axis(seg.s0);
        const int steps = static_cast<int>(std::ceil(seg.axis.length));
        for (int k = 1; k <= steps; ++k) {
            const std::array<double, 2> b = measured_axis(seg.s0 + seg.axis.length * k / steps);
            piece += std::hypot(b[0] - a[0], b[1] - a[1]);
            a = b;
        }
        MESSAGE(seg.key, ": s = ", seg.s0, " m, nominal ", seg.axis.length, " m, medido ", piece,
                " m");
        CHECK(std::abs(piece - seg.axis.length) <= 0.02 * seg.axis.length + 0.2);
    }
    CHECK(road.length() == doctest::Approx(992.54).epsilon(1e-4));
    CHECK(length == doctest::Approx(990.0).epsilon(0.10));
    // Radios: ajuste a los puntos medios de los bordes cada 0,5 m, sin 2 m en cada extremo.
    for (const rally::sim::RoadSegment& seg : road.segments()) {
        if (seg.axis.radius == 0.0) {
            continue;
        }
        std::vector<std::array<double, 2>> pts;
        for (double u = 2.0; u <= seg.axis.length - 2.0; u += 0.5) {
            pts.push_back(measured_axis(seg.s0 + u));
        }
        const double r = fit_radius(pts);
        MESSAGE(seg.key, ": R nominal ", seg.axis.radius, " m, medido ", r, " m");
        INFO(seg.key);
        if (seg.piece == RoadPiece::Hairpin) {
            CHECK(std::abs(r - 12.0) <= 0.25);
        } else {
            CHECK(std::abs(r - seg.axis.radius) <= 0.02 * seg.axis.radius);
        }
    }
}

TEST_CASE("test_road: crest, grades and banks measured in the Jolt ground") {
    const ZoneGround g;
    const TestRoad& road = g.road();
    const auto y_at = [&](double s, double t) {
        double x = 0.0;
        double z = 0.0;
        lateral_point(road.point(s), t, x, z);
        const rally::sim::GroundProbe probe = g.world.probe_ground(x, z);
        REQUIRE(probe.hit);
        return probe.y;
    };
    // Cresta: lo más alto del eje sobre la altura antes de ella.
    const rally::sim::RoadSegment& crest = road.segment(RoadPiece::Crest);
    double top = -1e9;
    for (double u = 0.0; u <= crest.axis.length; u += 0.1) {
        top = std::max(top, y_at(crest.s0 + u, 0.0));
    }
    const double crest_height = top - y_at(crest.s0 - 1.0, 0.0);
    MESSAGE("cresta: ", crest_height, " m");
    CHECK(std::abs(crest_height - 1.5) <= 0.1);
    // Pendientes: recta de mínimos cuadrados en el tramo de pendiente plena.
    const auto grade = [&](double s0, double s1) {
        double n = 0.0, ss = 0.0, sy = 0.0, sss = 0.0, ssy = 0.0;
        for (double s = s0; s <= s1; s += 1.0) {
            const double y = y_at(s, 0.0);
            n += 1.0;
            ss += s;
            sy += y;
            sss += s * s;
            ssy += s * y;
        }
        return (n * ssy - ss * sy) / (n * sss - ss * ss);
    };
    const rally::sim::RoadSegment& descent = road.segment(RoadPiece::Descent);
    const rally::sim::RoadSegment& climb = road.segment(RoadPiece::ClimbLeft);
    const double down = grade(descent.s0 + 30.0, descent.s0 + descent.axis.length - 30.0);
    const double up = grade(climb.s0 + 27.0, climb.s0 + climb.axis.length - 27.0);
    MESSAGE("bajada ", 100.0 * down, " %; subida ", 100.0 * up, " %");
    CHECK(std::abs(down + 0.07) <= 0.003);
    CHECK(std::abs(up - 0.05) <= 0.003);
    // Peraltes en la mitad de cada curva: positivo si sube hacia la derecha.
    struct Bank {
        RoadPiece first;
        RoadPiece last;
        double expected; // pendiente lateral: + sube a la derecha
    };
    const Bank banks[] = {{RoadPiece::FastRight, RoadPiece::FastRight, 0.03},
                          {RoadPiece::Tightening50, RoadPiece::Tightening35, 0.04},
                          {RoadPiece::CrestLeft, RoadPiece::CrestLeft, 0.04}};
    for (const Bank& b : banks) {
        const double s0 = road.segment(b.first).s0;
        const double s1 = road.segment(b.last).s0 + road.segment(b.last).axis.length;
        const double s = 0.5 * (s0 + s1);
        const double slope = (y_at(s, 2.5) - y_at(s, -2.5)) / 5.0;
        MESSAGE(road.segment(b.first).key, ": peralte ", 100.0 * slope, " % (esperado ",
                100.0 * b.expected, " %)");
        CHECK(std::abs(slope - b.expected) <= 0.005);
    }
}

TEST_CASE("test_road: roadway, shoulder, ditch and outside give their surface") {
    for (const Surface surface : {Surface::Gravel, Surface::Asphalt}) {
        const ZoneGround g(surface);
        const TestRoad& road = g.road();
        INFO("calzada de ", std::string(rally::sim::surface_name(surface)));
        CHECK(g.zone().road_surface() == surface);
        for (const double s : {50.0, 200.0, 370.0, 520.0, 650.0, 780.0, 850.0, 950.0}) {
            const RoadPoint p = road.point(s);
            const double ditch_bottom =
                zone::kShoulderEdge + zone::kDitchSide + 0.5 * zone::kDitchFlat;
            const std::pair<double, Surface> want[] = {{0.0, surface},
                                                       {1.5, surface},
                                                       {zone::kRoadHalfWidth + 0.8, Surface::Sand},
                                                       {ditch_bottom, Surface::Dirt},
                                                       {15.0, Surface::Dirt}};
            for (const auto& [t, expected] : want) {
                for (const int side : {-1, 1}) {
                    double x = 0.0;
                    double z = 0.0;
                    lateral_point(p, side * t, x, z);
                    // La celda del punto tiene su centro dentro de la misma franja: se mide el
                    // centro de la celda.
                    const double cx = zone::kGroundOrigin +
                                      (std::floor((x - zone::kGroundOrigin) / 0.75) + 0.5) * 0.75;
                    const double cz = zone::kGroundOrigin +
                                      (std::floor((z - zone::kGroundOrigin) / 0.75) + 0.5) * 0.75;
                    INFO("s = ", s, ", t = ", side * t);
                    CHECK(g.zone().surface_at(cx, cz) == expected);
                    const rally::sim::GroundProbe probe = g.world.probe_ground(cx, cz);
                    REQUIRE(probe.hit);
                    CHECK(probe.surface == expected);
                }
            }
        }
    }
}

TEST_CASE("test_road: the grid keeps a sand cell on each side and the ditch depth everywhere") {
    const ZoneGround g;
    const TestRoad& road = g.road();
    double min_sand = 1e9;
    double min_depth = 1e9;
    double max_depth = 0.0;
    double min_depth_s = 0.0;
    int bad_sand = 0;
    int bad_depth = 0;
    for (double s = road.first_s() + 1.0; s <= road.last_s() - 1.0; s += 0.5) {
        for (const int side : {-1, 1}) {
            const double sand = sand_width(g.world, road, s, side);
            const double depth = ditch_depth(g.world, road, s, side);
            min_sand = std::min(min_sand, sand);
            if (depth < min_depth) {
                min_depth = depth;
                min_depth_s = s * side;
            }
            max_depth = std::max(max_depth, depth);
            bad_sand += sand < 0.75 ? 1 : 0;
            bad_depth += std::abs(depth - zone::kDitchDepth) > 0.15 * zone::kDitchDepth ? 1 : 0;
        }
    }
    MESSAGE("arena en la normal: mínimo ", min_sand, " m; fondo de la cuneta: de ", min_depth,
            " (en s = ", std::abs(min_depth_s), ", lado ",
            min_depth_s < 0.0 ? "izquierdo" : "derecho", ") a ", max_depth, " m");
    CHECK(bad_sand == 0);
    CHECK(bad_depth == 0);
}

TEST_CASE("test_road: the terrain stays under 30 percent and is exactly 0 past the footprint") {
    const ZoneGround g;
    const TestRoad& road = g.road();
    const TestZone& zone_ref = g.zone();
    // Alturas de los vértices del suelo de Jolt en la caja de la carretera.
    const double o = zone::kGroundOrigin;
    const double sp = zone::kGroundSpacing;
    const int ix0 = static_cast<int>(std::floor((road.min_x() - o) / sp));
    const int ix1 = static_cast<int>(std::ceil((road.max_x() - o) / sp));
    const int iz0 = static_cast<int>(std::floor((road.min_z() - o) / sp));
    const int iz1 = static_cast<int>(std::ceil((road.max_z() - o) / sp));
    const int nx = ix1 - ix0 + 1;
    const int nz = iz1 - iz0 + 1;
    std::vector<float> y(static_cast<std::size_t>(nx) * nz);
    std::vector<unsigned char> near_ditch(static_cast<std::size_t>(nx) * nz);
    for (int iz = 0; iz < nz; ++iz) {
        for (int ix = 0; ix < nx; ++ix) {
            const double x = o + (ix0 + ix) * sp;
            const double z = o + (iz0 + iz) * sp;
            const std::size_t i = static_cast<std::size_t>(iz) * nx + ix;
            y[i] = static_cast<float>(zone_ref.ground_height(x, z));
            double d = 0.0;
            road.nearest_s(x, z, d);
            // La muestra del eje más cercana está a menos de 0,5 m del punto del eje.
            near_ditch[i] = d < zone::kDitchEdge + 1.0 ? 1 : 0;
        }
    }
    // Pendiente de cada triángulo de Jolt ((ix, iz)–(ix+1, iz+1) es la diagonal) fuera de las
    // cunetas.
    double max_slope = 0.0;
    double at_x = 0.0;
    double at_z = 0.0;
    for (int iz = 0; iz + 1 < nz; ++iz) {
        for (int ix = 0; ix + 1 < nx; ++ix) {
            const std::size_t a = static_cast<std::size_t>(iz) * nx + ix;
            const std::size_t b = a + 1;
            const std::size_t c = a + nx;
            const std::size_t d = c + 1;
            if (near_ditch[a] || near_ditch[b] || near_ditch[c] || near_ditch[d]) {
                continue;
            }
            // Triángulo (a, c, d): ∂y/∂z por a–c, ∂y/∂x por c–d. Triángulo (a, d, b): ∂y/∂x por
            // a–b, ∂y/∂z por b–d.
            const double s1 = std::hypot((y[d] - y[c]) / sp, (y[c] - y[a]) / sp);
            const double s2 = std::hypot((y[b] - y[a]) / sp, (y[d] - y[b]) / sp);
            const double s = std::max(s1, s2);
            if (s > max_slope) {
                max_slope = s;
                at_x = o + (ix0 + ix) * sp;
                at_z = o + (iz0 + iz) * sp;
            }
        }
    }
    double at_d = 0.0;
    const double at_s = road.nearest_s(at_x, at_z, at_d);
    MESSAGE("pendiente máxima del terreno fuera de las cunetas: ", 100.0 * max_slope, " % en (",
            at_x, ", ", at_z, "), s = ", at_s, ", a ", at_d, " m del eje");
    CHECK(max_slope <= 0.30);
    // A kRoadFootprint o más del eje (30 m del borde de la cuneta), 0 exacto.
    int not_zero = 0;
    for (double s = road.first_s(); s <= road.last_s(); s += 2.0) {
        for (const int side : {-1, 1}) {
            for (const double t : {zone::kRoadFootprint + 0.6, zone::kRoadFootprint + 5.0}) {
                double x = 0.0;
                double z = 0.0;
                lateral_point(road.point(s), side * t, x, z);
                double d = 0.0;
                road.nearest_s(x, z, d);
                // La muestra del eje más cercana está a menos de 0,5 m del eje.
                if (d >= zone::kRoadFootprint + 0.5) {
                    not_zero += zone_ref.ground_height(x, z) != 0.0 ? 1 : 0;
                }
            }
        }
    }
    CHECK(not_zero == 0);
    CHECK(zone::kRoadFootprint - zone::kDitchEdge == doctest::Approx(30.0));
}

TEST_CASE("test_road: areas, walls and trees keep their distances from the road") {
    const TestZone zone_ref;
    const TestRoad& road = zone_ref.road();
    // Áreas a ≥ 30 m del borde exterior de la cuneta; muros a ≥ 60 m.
    double closest_area = 1e9;
    double closest_wall = 1e9;
    const double usable = zone::kUsableHalfSize;
    for (double s = road.first_s(); s <= road.last_s(); s += 0.5) {
        const RoadPoint p = road.point(s);
        for (const int side : {-1, 1}) {
            double x = 0.0;
            double z = 0.0;
            lateral_point(p, side * zone::kDitchEdge, x, z);
            for (const rally::sim::ZoneArea& a : zone_ref.areas()) {
                if (a.id == AreaId::Road || a.id == AreaId::RoadShoulder) {
                    continue;
                }
                closest_area = std::min(closest_area, TestZone::signed_distance(a, x, z));
            }
            closest_wall = std::min({closest_wall, x + usable, usable - x, z + usable, usable - z});
        }
    }
    MESSAGE("borde de la cuneta: área más cercana a ", closest_area, " m; muro a ", closest_wall,
            " m");
    CHECK(closest_area >= 30.0);
    CHECK(closest_wall >= 60.0);
    // Árboles: fuera de la calzada, el arcén y la cuneta (la corteza a ≥ 0,5 m del borde de la
    // cuneta de cualquier parte del eje), sobre tierra y ninguno en el interior de la horquilla.
    const std::vector<rally::sim::AxisSegment> axis = road.axis();
    const rally::sim::AxisSegment& hairpin = road.segment(RoadPiece::Hairpin).axis;
    double closest_tree = 1e9;
    for (const rally::sim::TreeSpot& tree : road.trees()) {
        double d = 1e18;
        for (const rally::sim::AxisSegment& seg : axis) {
            d = std::min(d, rally::sim::segment_distance(seg, tree.x, tree.z));
        }
        closest_tree = std::min(closest_tree, d);
        CHECK(d - zone::kTreeRadius >= zone::kDitchEdge + 0.5 - 1e-9);
        CHECK(zone_ref.surface_at(tree.x, tree.z) == Surface::Dirt);
        CHECK(std::hypot(tree.x - hairpin.cx, tree.z - hairpin.cz) >= hairpin.radius);
    }
    MESSAGE(road.trees().size(), " árboles; el más cercano al eje, a ", closest_tree, " m");
    CHECK(road.trees().size() > 150);
}

TEST_CASE("test_road: the corner after the crest stays below the line of sight") {
    const TestZone zone_ref;
    const TestRoad& road = zone_ref.road();
    const rally::sim::RoadSegment& crest = road.segment(RoadPiece::Crest);
    const double top_s = crest.s0 + 0.5 * crest.axis.length;
    const double curve_s = road.segment(RoadPiece::CrestLeft).s0;
    for (const double back : {30.0, 15.0}) {
        const RoadPoint eye_p = road.point(top_s - back);
        const double eye_y = zone_ref.ground_height(eye_p.x, eye_p.z) + 1.10;
        for (const double target_s : {curve_s, curve_s + 20.0}) {
            const RoadPoint t = road.point(target_s);
            const double ty = zone_ref.ground_height(t.x, t.z);
            const double dist = std::hypot(t.x - eye_p.x, t.z - eye_p.z);
            // La línea de visión rasante: la mayor pendiente desde el ojo al suelo en el camino.
            double sight = -1e9;
            for (double u = 0.01; u < 0.999; u += 0.0025) {
                const double x = eye_p.x + u * (t.x - eye_p.x);
                const double z = eye_p.z + u * (t.z - eye_p.z);
                sight = std::max(sight, (zone_ref.ground_height(x, z) - eye_y) / (u * dist));
            }
            const double below = eye_y + sight * dist - ty;
            MESSAGE("ojo a ", back, " m de coronar: el eje en s = ", target_s, " queda ", below,
                    " m bajo la visual");
            CHECK(below >= 1.0);
        }
    }
}

TEST_CASE("road_timer: start and finish time a run, resets mark it, backwards does not count") {
    rally::sim::FinishLine start;
    start.forward_z = -1.0;
    start.half_length = 4.0;
    rally::sim::FinishLine finish = start;
    finish.z = -100.0;
    // Cruces de la salida (z = 0) y de la meta (z = −100), hacia delante (hacia −z) o atrás.
    const auto cross = [](RoadTimer& t, double z, bool forward) {
        t.update(0.0, forward ? z + 0.1 : z - 0.1, 0.0, forward ? z - 0.1 : z + 0.1, false);
    };
    const auto idle = [](RoadTimer& t, int steps) {
        for (int i = 0; i < steps; ++i) {
            t.update(0.0, -50.0, 0.0, -50.0, false);
        }
    };
    SUBCASE("start to finish") {
        RoadTimer t(start, finish);
        cross(t, 0.0, true);
        CHECK(t.state().run_start == 1);
        idle(t, 99);
        cross(t, -100.0, true);
        CHECK(t.state().runs == 1);
        CHECK(t.state().last_run == 100);
        CHECK(t.state().last_resets == 0);
        CHECK(t.state().best_clean == 100);
        CHECK(t.state().run_start == -1);
    }
    SUBCASE("resets mark the run and count") {
        RoadTimer t(start, finish);
        cross(t, 0.0, true);
        t.update(0.0, -50.0, 0.0, -40.0, true);
        idle(t, 10);
        t.update(0.0, -50.0, 0.0, -40.0, true);
        CHECK(t.state().resets == 2);
        cross(t, -100.0, true);
        CHECK(t.state().runs == 1);
        CHECK(t.state().last_resets == 2);
        CHECK(t.state().best_clean == -1);
    }
    SUBCASE("backwards over the finish and forwards again is no run") {
        RoadTimer t(start, finish);
        cross(t, 0.0, true);
        cross(t, -100.0, false);
        CHECK(t.state().finish_debt == 1);
        cross(t, -100.0, true);
        CHECK(t.state().finish_debt == 0);
        CHECK(t.state().runs == 0);
        CHECK(t.state().run_start == 1);
    }
    SUBCASE("crossing the start again starts a new run") {
        RoadTimer t(start, finish);
        cross(t, 0.0, true);
        t.update(0.0, -50.0, 0.0, 5.0, true);
        idle(t, 5);
        cross(t, 0.0, true);
        CHECK(t.state().resets == 0);
        CHECK(t.state().run_start == 8);
    }
    SUBCASE("a finish without a start is nothing") {
        RoadTimer t(start, finish);
        cross(t, -100.0, true);
        CHECK(t.state().runs == 0);
        CHECK(t.state().last_run == -1);
    }
}

namespace {

// Conductor automático por el eje de la carretera: persigue un punto 8 m por delante y ajusta
// la velocidad a lo que viene en 60 m (aceleración lateral `lateral` y frenada de 3 m/s²). Con
// `overspeed`, entra en la horquilla a esa velocidad. La entrada cambia cada 4 pasos.
class RoadDriver {
public:
    explicit RoadDriver(const TestRoad& road, double lateral = 2.5, double overspeed = 0.0)
        : road_(road), lateral_(lateral), overspeed_(overspeed) {
        for (double s = road.first_s(); s <= road.last_s(); s += 0.5) {
            points_.push_back(road.point(s));
        }
    }

    double s() const { return points_[index_].s; }

    SimInput input(const rally::sim::BodyPose& pose, const rally::sim::BodyVelocity& v,
                   double stop_at) {
        const std::size_t n = points_.size();
        std::size_t best = index_;
        double best_d = 1e18;
        for (std::size_t k = 0; k < 80 && index_ + k < n; ++k) {
            const std::size_t i = index_ + k;
            const double d = std::hypot(points_[i].x - pose.x, points_[i].z - pose.z);
            if (d < best_d) {
                best_d = d;
                best = i;
            }
        }
        if (best_d > 10.0) {
            // Tras recolocar: el punto más cercano de todo el eje.
            for (std::size_t i = 0; i < n; ++i) {
                const double d = std::hypot(points_[i].x - pose.x, points_[i].z - pose.z);
                if (d < best_d) {
                    best_d = d;
                    best = i;
                }
            }
        }
        index_ = best;
        const RoadPoint& target = points_[std::min(best + 16, n - 1)];
        const double want = std::atan2(target.x - pose.x, -(target.z - pose.z));
        const double error = wrap(want - car_heading(pose));
        double target_speed = 20.0;
        for (std::size_t k = 0; k < 120 && best + k + 1 < n; ++k) {
            const RoadPoint& a = points_[best + k];
            const RoadPoint& b = points_[best + k + 1];
            const double curvature = std::abs(wrap(b.psi - a.psi)) / 0.5;
            const double radius = curvature > 1e-6 ? 1.0 / curvature : 1e6;
            const double d = 0.5 * static_cast<double>(k);
            target_speed = std::min(target_speed, std::sqrt(lateral_ * radius + 2.0 * 3.0 * d));
        }
        const rally::sim::RoadSegment& hairpin = road_.segment(RoadPiece::Hairpin);
        if (overspeed_ > 0.0 && s() > hairpin.s0 - 40.0 && s() < hairpin.s0 + 5.0) {
            target_speed = overspeed_;
        }
        if (s() >= stop_at) {
            target_speed = 0.0;
        }
        const double speed = std::hypot(v.vx, v.vz);
        SimInput in;
        in.steer = static_cast<std::int16_t>(std::clamp(3.0 * error, -1.0, 1.0) * kAxisMax);
        in.throttle = static_cast<std::int16_t>(std::clamp(0.6 * (target_speed - speed), 0.0, 1.0) *
                                                kAxisMax);
        in.brake = static_cast<std::int16_t>(std::clamp(0.6 * (speed - target_speed), 0.0, 1.0) *
                                             kAxisMax);
        return in;
    }

private:
    const TestRoad& road_;
    double lateral_;
    double overspeed_;
    std::vector<RoadPoint> points_;
    std::size_t index_ = 0;
};

struct RoadDrive {
    rally::io::Recording recording;
    rally::sim::RoadRunState run;
    double max_offset = 0.0; // mayor distancia del centro de masas al eje, m
};

// Graba al conductor automático desde la salida de la carretera hasta pararse 30 m pasada la
// meta (o `seconds`). `resets_at`: s de la carretera a la que pulsa recolocar.
RoadDrive drive_road(int seconds, double overspeed = 0.0,
                     const std::vector<double>& resets_at = {}) {
    const WorldDesc desc = rally::sim::zone_world(Surface::Gravel, rally::tests::reference_car(),
                                                  rally::sim::ZoneStart::Road);
    SimWorld world;
    world.build(desc);
    world.seal();
    const TestRoad& road = world.zone()->road();
    RoadDriver driver(road, 2.5, overspeed);
    rally::io::RecordingHeader header;
    header.commit = "test";
    header.world = rally::io::serialize_world(desc);
    header.car_text = rally::tests::reference_car_text();
    header.zone_hash = world.zone_hash();
    rally::io::Recorder recorder;
    const int steps = seconds * rally::sim::kStepsPerSecond;
    recorder.begin(std::move(header), static_cast<std::size_t>(steps));
    RoadDrive out;
    std::size_t next_reset = 0;
    SimInput in;
    int still = 0;
    for (int i = 0; i < steps; ++i) {
        if (i % 4 == 0) {
            in = driver.input(world.car_pose(), world.car_velocity(), road.length() + 30.0);
            if (next_reset < resets_at.size() && driver.s() >= resets_at[next_reset]) {
                in.buttons |= rally::sim::kButtonReset;
                ++next_reset;
            }
        } else {
            in.buttons = 0;
        }
        world.step(in);
        recorder.append(in);
        const rally::sim::BodyPose com = world.car_center_of_mass();
        double d = 0.0;
        road.nearest_s(com.x, com.z, d);
        out.max_offset = std::max(out.max_offset, d);
        const rally::sim::BodyVelocity v = world.car_velocity();
        still = driver.s() > road.length() + 10.0 && std::hypot(v.vx, v.vz) < 0.2 ? still + 1 : 0;
        if (still > 240) {
            break;
        }
    }
    out.run = *world.road_run();
    out.recording = recorder.finish(world.state_hash(), rally::io::timed_steps(world),
                                    rally::io::road_result(world));
    return out;
}

bool rewrite_recordings() {
    char* flag = nullptr;
    std::size_t size = 0;
    const bool rewrite =
        _dupenv_s(&flag, &size, "RALLY_WRITE_TEST_RECORDINGS") == 0 && flag != nullptr;
    std::free(flag);
    return rewrite;
}

// Compara (o, con RALLY_WRITE_TEST_RECORDINGS=1, escribe) la grabación guardada.
void check_stored(const char* path, const rally::io::Recording& recording) {
    std::string error;
    if (rewrite_recordings()) {
        REQUIRE(rally::io::write_file(path, rally::io::serialize(recording), error));
    }
    std::string bytes;
    REQUIRE(rally::io::read_file(path, bytes, error));
    const rally::io::ParseResult stored = rally::io::parse(bytes);
    REQUIRE(stored.ok);
    CHECK(stored.recording.header.zone_hash == recording.header.zone_hash);
    CHECK(stored.recording.footer.final_hash == recording.footer.final_hash);
    CHECK(stored.recording.footer.road == recording.footer.road);
}

} // namespace

TEST_CASE("test_road: a recorded run from start to finish is timed and replays the same") {
    const rally::sim::JoltRuntime runtime;
    const RoadDrive drive = drive_road(240);
    const rally::io::RoadResult& road = drive.recording.footer.road;
    MESSAGE("pasada: ", road.last_run, " pasos (",
            static_cast<double>(road.last_run) / rally::sim::kStepsPerSecond,
            " s); el centro de masas, como mucho a ", drive.max_offset, " m del eje");
    REQUIRE(road.runs == 1);
    CHECK(road.last_run > 0);
    CHECK(road.last_resets == 0);
    CHECK(drive.run.best_clean == road.last_run);
    CHECK(drive.max_offset < zone::kShoulderEdge);
    const rally::io::ParseResult parsed = rally::io::parse(rally::io::serialize(drive.recording));
    REQUIRE(parsed.ok);
    const rally::io::ReplayResult replayed = rally::io::replay(parsed.recording);
    CHECK(replayed.hash_ok);
    CHECK(replayed.road == road);
    check_stored(RALLY_TEST_DATA_DIR "/recordings/road_run.rrec", drive.recording);
}

TEST_CASE("test_road: a run with two resets is marked and counts them") {
    const rally::sim::JoltRuntime runtime;
    const RoadDrive drive = drive_road(240, 0.0, {150.0, 850.0});
    const rally::io::RoadResult& road = drive.recording.footer.road;
    MESSAGE("pasada con recolocaciones: ", road.last_run, " pasos, ", road.last_resets,
            " recolocaciones");
    REQUIRE(road.runs == 1);
    CHECK(road.last_resets == 2);
    CHECK(drive.run.best_clean == -1);
}

TEST_CASE("test_road: overshooting the hairpin leaves the road once") {
    const rally::sim::JoltRuntime runtime;
    const RoadDrive drive = drive_road(240, 13.0);
    MESSAGE("horquilla a 13 m/s: el centro de masas llega a ", drive.max_offset,
            " m del eje; pasadas ", drive.recording.footer.road.runs);
    CHECK(drive.max_offset > zone::kShoulderEdge);
    check_stored(RALLY_TEST_DATA_DIR "/recordings/road_off.rrec", drive.recording);
}

TEST_CASE("test_road: reset near the road never goes forward along it") {
    const rally::sim::JoltRuntime runtime;
    const TestZone zone_ref;
    const TestRoad& road = zone_ref.road();
    struct Case {
        double s;
        double t;
        const char* expected;
    };
    const rally::sim::RoadSegment& hairpin = road.segment(RoadPiece::Hairpin);
    const Case cases[] = {
        {hairpin.s0 - 5.0, 0.0, "road_700"}, // pata de bajada, 5 m antes de la horquilla
        {820.0, 0.0, "road_800"},            // pata de subida
        {50.0, 0.0, "road_start"},
        {-15.0, 0.0, "road_start"},
        {390.0, 20.0, "road_300"},
        {650.0, -35.0, "road_600"},
    };
    SimInput reset;
    reset.buttons = rally::sim::kButtonReset;
    for (const Case& c : cases) {
        double x = 0.0;
        double z = 0.0;
        lateral_point(road.point(c.s), c.t, x, z);
        const rally::sim::RespawnPoint& r =
            zone_ref.respawns()[static_cast<std::size_t>(zone_ref.respawn_for(x, z))];
        INFO("desde s = ", c.s, ", t = ", c.t);
        CHECK(std::string(r.key) == c.expected);
        // Y en el mundo, al pulsar recolocar.
        WorldDesc desc = rally::sim::zone_world(Surface::Gravel, rally::tests::reference_car());
        desc.car_spawn.x = x;
        desc.car_spawn.y = zone_ref.ground_height(x, z);
        desc.car_spawn.z = z;
        SimWorld world;
        world.build(desc);
        world.seal();
        world.step(SimInput{});
        world.step(reset);
        const rally::sim::BodyPose pose = world.car_pose();
        CHECK(std::hypot(pose.x - r.x, pose.z - r.z) < 1e-9);
        CHECK(std::abs(wrap(car_heading(pose) + r.yaw)) < 1e-6);
    }
    // Lejos de la carretera, el más cercano de la zona.
    const RoadPoint p = road.point(400.0);
    double x = 0.0;
    double z = 0.0;
    lateral_point(p, 60.0, x, z);
    CHECK(zone_ref.respawn_for(x, z) == zone_ref.nearest_respawn(x, z));
    // Los puntos de la carretera: uno antes de la salida y cada 100 m, en el eje, ninguno pasada
    // la meta, mirando en el sentido de la marcha.
    const std::size_t first = zone_ref.road_respawn_first();
    REQUIRE(zone_ref.respawns().size() == first + 10);
    for (std::size_t i = first; i < zone_ref.respawns().size(); ++i) {
        const rally::sim::RespawnPoint& r = zone_ref.respawns()[i];
        double d = 0.0;
        const double s = road.nearest_s(r.x, r.z, d);
        CHECK(d < 1e-6);
        CHECK(s < road.length());
        CHECK(std::abs(wrap(-r.yaw - road.point(s).psi)) < 1e-9);
    }
}

TEST_CASE("test_road: the car launched at 30 m/s against a tree stops and does not go through") {
    const rally::sim::JoltRuntime runtime;
    const TestZone zone_ref;
    const TestRoad& road = zone_ref.road();
    // Un árbol de la recta de salida, del lado izquierdo, sin otro en los 25 m de camino que
    // lleva el coche hasta él (a menos de 1,5 m de su línea).
    const rally::sim::TreeSpot* target = nullptr;
    double target_t = 0.0;
    for (const rally::sim::TreeSpot& tree : road.trees()) {
        double d = 0.0;
        const double s = road.nearest_s(tree.x, tree.z, d);
        if (s < 60.0 || s > 140.0 || tree.x > zone::kRoadStartX) {
            continue;
        }
        bool clear = true;
        for (const rally::sim::TreeSpot& other : road.trees()) {
            clear = clear && (&other == &tree || other.z > tree.z + 26.0 || other.z < tree.z ||
                              std::abs(other.x - tree.x) > 1.5);
        }
        if (clear) {
            target = &tree;
            target_t = -d;
            break;
        }
    }
    REQUIRE(target != nullptr);
    // El coche, 25 m antes en la misma fila y mirando al norte (la recta va al norte), a 30 m/s
    // en N, sin aire ni rodadura: llega al árbol a la velocidad de salida.
    const double start_z = target->z + 25.0;
    WorldDesc desc = rally::tests::in_neutral(
        rally::sim::zone_world(Surface::Gravel, rally::tests::reference_car()));
    desc.car_spawn.x = target->x;
    desc.car_spawn.z = start_z;
    desc.car_spawn.y = zone_ref.ground_height(target->x, start_z);
    desc.car_spawn.speed = 30.0f;
    desc.car_data.cda = 0.0f;
    for (rally::sim::TireSurface& tire : desc.car_data.tires) {
        tire.rolling_resistance = 0.0f;
    }
    SimWorld world;
    world.build(desc);
    world.seal();
    double hit_speed = 0.0;
    double previous = 30.0;
    int after = -1;
    // Pasos con el centro del árbol dentro de la planta del coche (con la penetración de un paso
    // de margen).
    int inside = 0;
    double closest = 1e9;
    const double margin = 30.0 * rally::sim::kStepSeconds;
    const double half_length = 0.5 * rally::tests::reference_car().box_length - margin;
    const double half_width = 0.5 * rally::tests::reference_car().box_width - margin;
    for (int i = 0; i < 5 * rally::sim::kStepsPerSecond && after < 240; ++i) {
        // Tras el choque, freno y freno de mano: en el talud, sin frenar, seguiría rodando.
        SimInput in;
        if (after >= 0) {
            in.brake = kAxisMax;
            in.buttons = rally::sim::kButtonHandbrake;
        }
        world.step(in);
        const rally::sim::BodyPose pose = world.car_pose();
        const double h = car_heading(pose);
        const double dx = target->x - pose.x;
        const double dz = target->z - pose.z;
        const double along = dx * std::sin(h) - dz * std::cos(h);
        const double across = dx * std::cos(h) + dz * std::sin(h);
        inside += std::abs(along) < half_length && std::abs(across) < half_width ? 1 : 0;
        closest = std::min(closest, std::hypot(dx, dz));
        const rally::sim::BodyVelocity v = world.car_velocity();
        const double speed = std::hypot(v.vx, v.vz);
        if (after < 0 && world.last_impact() > 5.0f) {
            after = 0;
            hit_speed = previous;
        } else if (after >= 0) {
            ++after;
        }
        previous = speed;
    }
    const rally::sim::BodyVelocity v = world.car_velocity();
    const rally::sim::BodyPose end = world.car_pose();
    MESSAGE("árbol a t = ", target_t, " m: choque a ", hit_speed, " m/s; 1 s después, ",
            std::hypot(v.vx, v.vz), " m/s; centro del coche a ", closest,
            " m del árbol como mucho; al final, ", end.z - target->z, " m al sur de él");
    REQUIRE(after >= 0);
    CHECK(hit_speed >= 29.0);
    CHECK(std::hypot(v.vx, v.vz) < 1.0);
    // No lo atraviesa: el árbol nunca dentro del coche y el coche se queda al sur de él.
    CHECK(inside == 0);
    CHECK(end.z > target->z);
}

TEST_CASE("test_road: the trees are in Jolt and the zone hash is the same twice") {
    const rally::sim::JoltRuntime runtime;
    SimWorld a;
    a.build(rally::sim::zone_world(Surface::Gravel, rally::tests::reference_car()));
    a.seal();
    SimWorld b;
    b.build(rally::sim::zone_world(Surface::Gravel, rally::tests::reference_car()));
    b.seal();
    const TestRoad& road = a.zone()->road();
    REQUIRE(a.tree_count() == static_cast<int>(road.trees().size()));
    for (int i = 0; i < a.tree_count(); ++i) {
        const rally::sim::BodyPose p = a.tree_pose(i);
        const rally::sim::TreeSpot& t = road.trees()[static_cast<std::size_t>(i)];
        CHECK(p.x == doctest::Approx(t.x).epsilon(1e-12));
        CHECK(p.z == doctest::Approx(t.z).epsilon(1e-12));
        CHECK(p.y == doctest::Approx(t.y + zone::kTreeHalfHeight - zone::kTreeSink));
    }
    char text[160];
    std::snprintf(text, sizeof(text),
                  "hash de la zona 0x%016llx, esperado 0x%016llx; %d árboles (Jolt: %d cuerpos "
                  "como mucho)",
                  static_cast<unsigned long long>(a.zone_hash()),
                  static_cast<unsigned long long>(kExpectedZoneHash), a.tree_count(), 1024);
    MESSAGE(text);
    MESSAGE("memoria de los árboles en Jolt: ", a.tree_count(), " cuerpos de ", sizeof(JPH::Body),
            " B (", a.tree_count() * sizeof(JPH::Body) / 1024, " KiB) y una forma compartida");
    CHECK(a.zone_hash() == b.zone_hash());
    CHECK(a.zone_hash() == kExpectedZoneHash);
    // La carretera de asfalto solo cambia la superficie de la calzada.
    const TestZone gravel(Surface::Gravel, Surface::Gravel);
    const TestZone asphalt(Surface::Gravel, Surface::Asphalt);
    std::vector<float> hg;
    std::vector<std::uint8_t> sg;
    std::vector<float> ha;
    std::vector<std::uint8_t> sa;
    gravel.fill_ground(hg, sg);
    asphalt.fill_ground(ha, sa);
    CHECK(hg == ha);
    int changed = 0;
    int wrong = 0;
    for (std::size_t i = 0; i < sg.size(); ++i) {
        if (sg[i] != sa[i]) {
            ++changed;
            wrong += sg[i] != static_cast<std::uint8_t>(Surface::Gravel) ||
                             sa[i] != static_cast<std::uint8_t>(Surface::Asphalt)
                         ? 1
                         : 0;
        }
    }
    // 1112 m × 6 m de calzada en celdas de 0,5625 m²: unas 11 900.
    MESSAGE("celdas de la calzada: ", changed);
    CHECK(wrong == 0);
    CHECK(changed > 11000);
    CHECK(changed < 12800);
}

TEST_CASE("test_road: step allocates nothing of ours and Jolt the same next to the hairpin trees") {
    const rally::sim::JoltRuntime runtime;
    const TestZone zone_ref;
    const TestRoad& road = zone_ref.road();
    // El coche en la bajada, 30 m antes de la horquilla, frenando y girando junto a la zona
    // densa de árboles del exterior.
    const RoadPoint p = road.point(road.segment(RoadPiece::Hairpin).s0 - 30.0);
    WorldDesc desc = rally::sim::zone_world(Surface::Gravel, rally::tests::reference_car());
    desc.car_spawn.x = p.x;
    desc.car_spawn.z = p.z;
    desc.car_spawn.y = zone_ref.ground_height(p.x, p.z);
    desc.car_spawn.yaw = static_cast<float>(-p.psi);
    desc.car_spawn.speed = 12.0f;
    SimWorld world;
    world.build(desc);
    world.seal();
    SimInput in;
    in.throttle = kAxisMax / 3;
    in.steer = kAxisMax / 2;
    for (int i = 0; i < 10; ++i) {
        world.step(in);
    }
    const rally::tests::JoltAllocationCounter jolt;
    constexpr int kSteps = 2000;
    int steps_other = 0;
    const std::uint64_t ours_before = rally::tests::global_new_count();
    for (int i = 0; i < kSteps; ++i) {
        const int before = jolt.count();
        world.step(in);
        const int n = jolt.count() - before;
        steps_other += n != 0 && n != 1 ? 1 : 0;
    }
    const std::uint64_t ours = rally::tests::global_new_count() - ours_before;
    MESSAGE("junto a la horquilla: reservas de Jolt en ", kSteps, " pasos: ", jolt.count());
    CHECK(ours == 0);
    CHECK(steps_other == 0);
    CHECK(jolt.count() <= kSteps);
}

TEST_CASE("test_road: over the crest up to 70 km/h the wheels stay on the ground") {
    const rally::sim::JoltRuntime runtime;
    const TestZone zone_ref;
    const TestRoad& road = zone_ref.road();
    const rally::sim::RoadSegment& crest = road.segment(RoadPiece::Crest);
    for (const double kmh : {50.0, 60.0, 70.0, 80.0, 90.0, 100.0, 110.0}) {
        const double speed = kmh / 3.6;
        // Sale 20 m antes de la cresta (en la S), en el eje, a la velocidad de la prueba.
        const RoadPoint p = road.point(crest.s0 - 20.0);
        WorldDesc desc = rally::sim::zone_world(Surface::Gravel, rally::tests::reference_car());
        desc.car_spawn.x = p.x;
        desc.car_spawn.z = p.z;
        desc.car_spawn.y = zone_ref.ground_height(p.x, p.z);
        desc.car_spawn.yaw = static_cast<float>(-p.psi);
        desc.car_spawn.speed = static_cast<float>(speed);
        SimWorld world;
        world.build(desc);
        world.seal();
        world.set_telemetry(true);
        const double travel = rally::tests::reference_car().travel;
        int airborne_steps = 0;
        double max_compression = 0.0;
        double min_compression = 1e9;
        double max_impact = 0.0;
        for (int i = 0; i < 4 * rally::sim::kStepsPerSecond; ++i) {
            const rally::sim::BodyPose pose = world.car_pose();
            double d = 0.0;
            const double s = road.nearest_s(pose.x, pose.z, d);
            if (s > crest.s0 + crest.axis.length + 5.0) {
                break;
            }
            // Mantiene la velocidad y sigue el eje 10 m por delante.
            const RoadPoint target = road.point(s + 10.0);
            const double want = std::atan2(target.x - pose.x, -(target.z - pose.z));
            const rally::sim::BodyVelocity v = world.car_velocity();
            SimInput in;
            in.steer = static_cast<std::int16_t>(
                std::clamp(3.0 * wrap(want - car_heading(pose)), -1.0, 1.0) * kAxisMax);
            in.throttle = static_cast<std::int16_t>(
                std::clamp(0.5 * (speed - std::hypot(v.vx, v.vz)), 0.0, 1.0) * kAxisMax);
            world.step(in);
            const rally::sim::CarTelemetry& t = world.telemetry();
            int contacts = 0;
            for (const rally::sim::WheelTelemetry& w : t.wheels) {
                contacts += w.contact;
                if (w.contact) {
                    max_compression = std::max(max_compression, static_cast<double>(w.compression));
                    min_compression = std::min(min_compression, static_cast<double>(w.compression));
                }
            }
            airborne_steps += contacts == 0 ? 1 : 0;
            if (s > crest.s0 - 5.0) {
                max_impact = std::max(max_impact, static_cast<double>(t.impact));
            }
        }
        MESSAGE("cresta a ", kmh, " km/h: ", airborne_steps,
                " pasos con las cuatro ruedas en el aire; compresión de ", min_compression, " a ",
                max_compression, " m de ", travel, " m de recorrido; impacto máximo ", max_impact,
                " m/s");
        // Hasta 70 km/h (la salida de la S de R 40 m en grava ronda los 60), en el suelo. Por
        // encima se informa: la suspensión rebota tras comprimirse en la vaguada de entrada y el
        // coche despega antes que la estimación cuasiestática √(g·R) de la cumbre (109 km/h).
        if (kmh <= 70.0) {
            CHECK(airborne_steps == 0);
        }
    }
}
