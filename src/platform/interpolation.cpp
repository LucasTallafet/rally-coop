#include "platform/interpolation.h"

#include <cmath>

namespace rally::platform {

Pose interpolate(const Pose& from, const Pose& to, double alpha) {
    Pose out;
    out.position.x = from.position.x + (to.position.x - from.position.x) * alpha;
    out.position.y = from.position.y + (to.position.y - from.position.y) * alpha;
    out.position.z = from.position.z + (to.position.z - from.position.z) * alpha;

    // q y -q son la misma rotación: se toma el signo que da el arco corto.
    const Quatd& a = from.rotation;
    Quatd b = to.rotation;
    if (a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w < 0.0) {
        b = {-b.x, -b.y, -b.z, -b.w};
    }
    const double keep = 1.0 - alpha;
    Quatd q{a.x * keep + b.x * alpha, a.y * keep + b.y * alpha, a.z * keep + b.z * alpha,
            a.w * keep + b.w * alpha};
    const double length = std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w);
    out.rotation = {q.x / length, q.y / length, q.z / length, q.w / length};
    return out;
}

} // namespace rally::platform
