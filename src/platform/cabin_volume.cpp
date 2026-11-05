#include "platform/cabin_volume.h"

#include "platform/cockpit.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace rally::platform {

namespace {

constexpr double kInf = std::numeric_limits<double>::infinity();
constexpr double kFeasibleTolerance = 1e-9; // m: un candidato dentro del volumen salvo redondeo

// Semiplano a·y + b·z ≤ c con (a, b) unitario.
struct HalfPlane {
    double a;
    double b;
    double c;
    double excess(double y, double z) const { return a * y + b * z - c; }
};

} // namespace

CabinVolume cabin_volume(const sim::CarData& car, bool copilot) {
    const sim::CockpitData& k = car.cockpit;
    CabinVolume v;
    // Un coche sin [cockpit] (las medidas a cero, como tests/data/cars/reference.car) no tiene
    // cabina: volumen abierto, el ojo no se toca.
    if (k.door_left == 0.0f && k.door_right == 0.0f && k.roof_above == 0.0f &&
        k.roof_behind == 0.0f && k.windshield_base_ahead == 0.0f &&
        k.windshield_top_ahead == 0.0f) {
        v.x_min = -kInf;
        v.x_max = kInf;
        v.ry = 0.0;
        v.rz = 1.0;
        v.rc = kInf;
        v.z_max = kInf;
        v.wy = 0.0;
        v.wz = 1.0;
        v.wc = kInf;
        return v;
    }
    // Puerta del conductor: a door_left a la izquierda del ojo nominal. El copiloto, reflejada.
    const double door = cockpit_point(car, 0.0, -static_cast<double>(k.door_left), 0.0).x;
    if (copilot) {
        v.x_min = -kInf;
        v.x_max = -(door + kCabinMargin);
    } else {
        v.x_min = door + kCabinMargin;
        v.x_max = kInf;
    }
    // Techo: del borde superior del parabrisas al final del techo, como lo dibuja cockpit.cpp.
    const Vec3d roof_front = cockpit_point(car, static_cast<double>(k.windshield_top_ahead), 0.0,
                                           static_cast<double>(k.windshield_top_above));
    const Vec3d roof_rear = cockpit_point(car, -static_cast<double>(k.roof_behind), 0.0,
                                          static_cast<double>(k.roof_above));
    {
        const double ry = roof_rear.z - roof_front.z; // normal (dz, -dy), hacia arriba
        const double rz = -(roof_rear.y - roof_front.y);
        const double sign = ry < 0.0 ? -1.0 : 1.0;
        const double len = std::sqrt(ry * ry + rz * rz);
        v.ry = sign * ry / len;
        v.rz = sign * rz / len;
        v.rc = v.ry * roof_front.y + v.rz * roof_front.z - kCabinMargin;
    }
    v.z_max = cockpit_point(car, -static_cast<double>(k.roof_behind), 0.0, 0.0).z - kCabinMargin;

    // Parabrisas: de la base al borde superior; normal perpendicular, hacia delante y arriba.
    const Vec3d base = cockpit_point(car, static_cast<double>(k.windshield_base_ahead), 0.0,
                                     -static_cast<double>(k.windshield_base_below));
    const Vec3d top = cockpit_point(car, static_cast<double>(k.windshield_top_ahead), 0.0,
                                    static_cast<double>(k.windshield_top_above));
    const double dy = top.y - base.y;
    const double dz = top.z - base.z;
    double ny = dz;
    double nz = -dy;
    if (nz > 0.0) { // hacia delante es −z
        ny = -ny;
        nz = -nz;
    }
    const double length = std::sqrt(ny * ny + nz * nz);
    v.wy = ny / length;
    v.wz = nz / length;
    v.wc = v.wy * base.y + v.wz * base.z - kCabinMargin;
    return v;
}

Vec3d clamp_to_cabin(const CabinVolume& volume, const Vec3d& eye) {
    Vec3d out = eye;
    out.x = std::clamp(eye.x, volume.x_min, volume.x_max);

    // (y, z): proyección exacta sobre la intersección de tres semiplanos.
    const std::array<HalfPlane, 3> planes = {{
        {volume.ry, volume.rz, volume.rc},
        {0.0, 1.0, volume.z_max},
        {volume.wy, volume.wz, volume.wc},
    }};
    const auto inside = [&planes](double y, double z, double tolerance) {
        for (const HalfPlane& p : planes) {
            if (p.excess(y, z) > tolerance) {
                return false;
            }
        }
        return true;
    };
    if (inside(eye.y, eye.z, 0.0)) {
        return out;
    }
    // Candidatos: la proyección a cada plano y cada vértice (cruce de dos planos). El más cercano
    // que cumpla todos es el punto del convexo más cercano.
    double best_d2 = kInf;
    double best_y = eye.y;
    double best_z = eye.z;
    const auto consider = [&](double y, double z) {
        if (!inside(y, z, kFeasibleTolerance)) {
            return;
        }
        const double d2 = (y - eye.y) * (y - eye.y) + (z - eye.z) * (z - eye.z);
        if (d2 < best_d2) {
            best_d2 = d2;
            best_y = y;
            best_z = z;
        }
    };
    for (const HalfPlane& p : planes) {
        const double e = p.excess(eye.y, eye.z);
        consider(eye.y - e * p.a, eye.z - e * p.b);
    }
    for (std::size_t i = 0; i < planes.size(); ++i) {
        for (std::size_t j = i + 1; j < planes.size(); ++j) {
            const HalfPlane& p = planes[i];
            const HalfPlane& q = planes[j];
            const double det = p.a * q.b - p.b * q.a;
            if (std::abs(det) < 1e-12) {
                continue;
            }
            consider((p.c * q.b - p.b * q.c) / det, (p.a * q.c - p.c * q.a) / det);
        }
    }
    out.y = best_y;
    out.z = best_z;
    return out;
}

} // namespace rally::platform
