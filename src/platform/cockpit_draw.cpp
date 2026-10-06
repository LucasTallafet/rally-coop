#include "platform/cockpit_draw.h"

#include "platform/camera_space.h"

#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

#include <cmath>

namespace rally::platform {

void draw_cockpit(const std::vector<CockpitTri>& tris, const Pose& car_pose, const Vec3d& origin) {
    if (tris.empty()) {
        return;
    }
    const Quaternion rotation{
        static_cast<float>(car_pose.rotation.x), static_cast<float>(car_pose.rotation.y),
        static_cast<float>(car_pose.rotation.z), static_cast<float>(car_pose.rotation.w)};
    const Vec3f position = relative_to_camera(car_pose.position, origin);
    const Matrix transform = MatrixMultiply(QuaternionToMatrix(rotation),
                                            MatrixTranslate(position.x, position.y, position.z));
    static const Vector3 kLight = Vector3Normalize(Vector3{-0.3f, 1.0f, 0.6f});

    // El estado de las caras no vacía el lote de rlgl: lo de antes se dibuja con el descarte.
    rlDrawRenderBatchActive();
    rlDisableBackfaceCulling();
    rlPushMatrix();
    rlMultMatrixf(MatrixToFloatV(transform).v);
    rlBegin(RL_TRIANGLES);
    for (const CockpitTri& t : tris) {
        const Vector3 a{t.a.x, t.a.y, t.a.z};
        const Vector3 b{t.b.x, t.b.y, t.b.z};
        const Vector3 c{t.c.x, t.c.y, t.c.z};
        const Vector3 n =
            Vector3Normalize(Vector3CrossProduct(Vector3Subtract(b, a), Vector3Subtract(c, a)));
        const float shade = 0.75f + 0.25f * std::abs(Vector3DotProduct(n, kLight));
        rlColor4ub(static_cast<unsigned char>(t.color.r * shade),
                   static_cast<unsigned char>(t.color.g * shade),
                   static_cast<unsigned char>(t.color.b * shade), 255);
        rlVertex3f(a.x, a.y, a.z);
        rlVertex3f(b.x, b.y, b.z);
        rlVertex3f(c.x, c.y, c.z);
    }
    rlEnd();
    rlPopMatrix();
    rlDrawRenderBatchActive(); // antes de volver a descartar caras
    rlEnableBackfaceCulling();
}

} // namespace rally::platform
