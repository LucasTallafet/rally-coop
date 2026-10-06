#include "sim/zone_axis.h"

#include "sim/rmath.h"

#include <utility>

namespace rally::sim {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

} // namespace

void AxisBuilder::straight(double length) {
    AxisSegment seg;
    seg.x0 = x_;
    seg.z0 = z_;
    seg.psi0 = psi_;
    seg.length = length;
    segments_.push_back(seg);
    x_ += length * rmath::sin(psi_);
    z_ -= length * rmath::cos(psi_);
}

void AxisBuilder::arc(double radius, double turn) {
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

std::vector<AxisSegment> AxisBuilder::take() { return std::move(segments_); }

double wrap_angle(double a, double low) { return a - kTwoPi * rmath::floor((a - low) / kTwoPi); }

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

void fit_segment_boxes(std::vector<AxisSegment>& axis) {
    for (AxisSegment& seg : axis) {
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
    }
}

} // namespace rally::sim
