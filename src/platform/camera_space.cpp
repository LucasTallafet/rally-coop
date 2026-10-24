#include "platform/camera_space.h"

namespace rally::platform {

Vec3f relative_to_camera(const Vec3d& world, const Vec3d& camera) {
    return Vec3f{static_cast<float>(world.x - camera.x), static_cast<float>(world.y - camera.y),
                 static_cast<float>(world.z - camera.z)};
}

Vec3d rotate(const Quatd& q, const Vec3d& v) {
    // v + w·t + u × t, con u = (x, y, z) y t = 2 (u × v).
    const Vec3d t{2.0 * (q.y * v.z - q.z * v.y), 2.0 * (q.z * v.x - q.x * v.z),
                  2.0 * (q.x * v.y - q.y * v.x)};
    return Vec3d{v.x + q.w * t.x + (q.y * t.z - q.z * t.y),
                 v.y + q.w * t.y + (q.z * t.x - q.x * t.z),
                 v.z + q.w * t.z + (q.x * t.y - q.y * t.x)};
}

Pose attach(const Pose& body, const Vec3d& local_offset) {
    const Vec3d offset = rotate(body.rotation, local_offset);
    Pose out;
    out.position = {body.position.x + offset.x, body.position.y + offset.y,
                    body.position.z + offset.z};
    out.rotation = body.rotation;
    return out;
}

int next_camera_view(int view) { return (view + 1) % 3; }

} // namespace rally::platform
