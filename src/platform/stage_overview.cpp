#include "platform/stage_overview.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace rally::platform {

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kPitch = -75.0 * kPi / 180.0;
constexpr double kSampleSpacing = 10.0;
constexpr double kMargin = 1.05;

struct Box {
    double min_x, max_x, min_y, max_y, min_z, max_z;
};

Vec3d cross(const Vec3d& a, const Vec3d& b) {
    return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
double dot(const Vec3d& a, const Vec3d& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
Vec3d normalize(const Vec3d& v) {
    const double n = std::sqrt(dot(v, v));
    return {v.x / n, v.y / n, v.z / n};
}

// El encuadre con el rumbo horizontal `heading` (vector unitario en planta).
StageOverview frame(const Box& box, const Vec3d& heading, double fov_y_deg, double aspect) {
    const double tan_v = std::tan(fov_y_deg * 0.5 * kPi / 180.0);
    const double tan_h = tan_v * aspect;
    const Vec3d forward =
        normalize({heading.x * std::cos(kPitch), std::sin(kPitch), heading.z * std::cos(kPitch)});
    const Vec3d right = normalize(cross(forward, {0.0, 1.0, 0.0}));
    const Vec3d up = cross(right, forward);
    const Vec3d center{(box.min_x + box.max_x) * 0.5, (box.min_y + box.max_y) * 0.5,
                       (box.min_z + box.max_z) * 0.5};
    // Distancia d del ojo al centro, sobre -forward, para que las 8 esquinas queden dentro.
    double distance = 0.0;
    for (const double x : {box.min_x, box.max_x}) {
        for (const double y : {box.min_y, box.max_y}) {
            for (const double z : {box.min_z, box.max_z}) {
                const Vec3d p{x - center.x, y - center.y, z - center.z};
                const double depth = dot(p, forward);
                distance = std::max({distance, std::fabs(dot(p, right)) / tan_h - depth,
                                     std::fabs(dot(p, up)) / tan_v - depth});
            }
        }
    }
    distance *= kMargin;
    StageOverview out;
    out.fov_y_deg = fov_y_deg;
    out.forward = forward;
    out.eye = {center.x - forward.x * distance, center.y - forward.y * distance,
               center.z - forward.z * distance};
    double far_distance = 0.0;
    double draw_distance = 0.0;
    for (const double x : {box.min_x, box.max_x}) {
        for (const double y : {box.min_y, box.max_y}) {
            for (const double z : {box.min_z, box.max_z}) {
                const Vec3d p{x - out.eye.x, y - out.eye.y, z - out.eye.z};
                far_distance = std::max(far_distance, std::sqrt(dot(p, p)));
                draw_distance = std::max(draw_distance, std::sqrt(p.x * p.x + p.z * p.z));
            }
        }
    }
    out.far_distance = far_distance;
    out.draw_distance = draw_distance;
    return out;
}

} // namespace

StageOverview stage_overview(const sim::StageTrack& track, double fov_y_deg, double aspect) {
    Box box{1e300, -1e300, 1e300, -1e300, 1e300, -1e300};
    const auto add = [&box](const sim::StagePose& p) {
        box.min_x = std::min(box.min_x, p.x);
        box.max_x = std::max(box.max_x, p.x);
        box.min_y = std::min(box.min_y, p.y);
        box.max_y = std::max(box.max_y, p.y);
        box.min_z = std::min(box.min_z, p.z);
        box.max_z = std::max(box.max_z, p.z);
    };
    const double end = track.s_end();
    for (double s = track.s_begin(); s < end; s += kSampleSpacing) {
        add(track.pose(s));
    }
    add(track.pose(end));
    // La franja del terreno, a ±60 m del eje en planta, y las alturas del eje.
    box.min_x -= sim::kStageStripHalfWidth;
    box.max_x += sim::kStageStripHalfWidth;
    box.min_z -= sim::kStageStripHalfWidth;
    box.max_z += sim::kStageStripHalfWidth;
    const StageOverview along_z = frame(box, {0.0, 0.0, -1.0}, fov_y_deg, aspect);
    const StageOverview along_x = frame(box, {1.0, 0.0, 0.0}, fov_y_deg, aspect);
    return along_x.far_distance < along_z.far_distance ? along_x : along_z;
}

} // namespace rally::platform
