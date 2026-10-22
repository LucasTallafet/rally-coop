#include "platform/scene_draw.h"

#include "platform/camera_space.h"

#include <raylib.h>
#include <raymath.h>
#include <rlgl.h>

#include <algorithm>
#include <array>

namespace rally::platform {

constexpr Color kCarColor{230, 160, 60, 255};
constexpr Color kTireColor{38, 40, 44, 255};
constexpr Color kRimColor{200, 204, 210, 255};

Color body_color(const sim::WorldDesc& desc, std::size_t index) {
    if (desc.bodies[index].motion == sim::BodyMotion::Dynamic) {
        return Color{90, 140, 200, 255};
    }
    return index == 0 ? Color{104, 118, 92, 255} : Color{140, 136, 128, 255};
}

static Vector3 to_vector(const Vec3f& v) { return Vector3{v.x, v.y, v.z}; }

// Caja con sombreado plano por cara: cada cara se aclara según su normal en el mundo
// frente a una luz fija. Sin texturas ni iluminación de la GPU. Se dibuja en
// (posición − origin) pasado a float: con origin en la cámara, render relativo.
void draw_box(const Pose& pose, const sim::BoxDesc& box, Color color, const Vec3d& origin) {
    struct Face {
        Vector3 normal;
        Vector3 u; // u × v = normal: las esquinas en orden (-,-) (+,-) (+,+) (-,+) quedan
        Vector3 v; // en sentido antihorario vistas desde fuera
    };
    static constexpr Face kFaces[] = {
        {{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}, {{-1, 0, 0}, {0, 0, 1}, {0, 1, 0}},
        {{0, 1, 0}, {0, 0, 1}, {1, 0, 0}}, {{0, -1, 0}, {1, 0, 0}, {0, 0, 1}},
        {{0, 0, 1}, {1, 0, 0}, {0, 1, 0}}, {{0, 0, -1}, {0, 1, 0}, {1, 0, 0}},
    };
    static const Vector3 kLight = Vector3Normalize(Vector3{0.35f, 1.0f, 0.55f});
    static constexpr float kCorners[4][2] = {{-1, -1}, {1, -1}, {1, 1}, {-1, 1}};

    const Quaternion rotation{
        static_cast<float>(pose.rotation.x), static_cast<float>(pose.rotation.y),
        static_cast<float>(pose.rotation.z), static_cast<float>(pose.rotation.w)};
    const Vec3f position = relative_to_camera(pose.position, origin);
    const Matrix transform = MatrixMultiply(QuaternionToMatrix(rotation),
                                            MatrixTranslate(position.x, position.y, position.z));
    const Vector3 half{box.half_x, box.half_y, box.half_z};

    rlPushMatrix();
    rlMultMatrixf(MatrixToFloatV(transform).v);
    rlBegin(RL_QUADS);
    for (const Face& face : kFaces) {
        const Vector3 world_normal = Vector3RotateByQuaternion(face.normal, rotation);
        const float shade = 0.5f + 0.5f * std::max(0.0f, Vector3DotProduct(world_normal, kLight));
        rlColor4ub(static_cast<unsigned char>(color.r * shade),
                   static_cast<unsigned char>(color.g * shade),
                   static_cast<unsigned char>(color.b * shade), color.a);
        for (const auto& corner : kCorners) {
            const Vector3 local =
                Vector3Add(face.normal, Vector3Add(Vector3Scale(face.u, corner[0]),
                                                   Vector3Scale(face.v, corner[1])));
            const Vector3 vertex = Vector3Multiply(local, half);
            rlVertex3f(vertex.x, vertex.y, vertex.z);
        }
    }
    rlEnd();
    rlPopMatrix();
}

// Rejilla de 1 m sobre el suelo, un poco más oscura que él, para ver el movimiento. Los
// extremos de cada línea se calculan en doble y se dibujan relativos a origin.
void draw_floor_grid(const sim::BoxDesc& floor, const Vec3d& origin) {
    constexpr Color kLine{88, 101, 78, 255};
    const double height = floor.y + floor.half_y + 0.01;
    const int half_x = static_cast<int>(floor.half_x);
    const int half_z = static_cast<int>(floor.half_z);
    for (int i = -half_x; i <= half_x; ++i) {
        const double x = floor.x + i;
        DrawLine3D(to_vector(relative_to_camera({x, height, floor.z - floor.half_z}, origin)),
                   to_vector(relative_to_camera({x, height, floor.z + floor.half_z}, origin)),
                   kLine);
    }
    for (int i = -half_z; i <= half_z; ++i) {
        const double z = floor.z + i;
        DrawLine3D(to_vector(relative_to_camera({floor.x - floor.half_x, height, z}, origin)),
                   to_vector(relative_to_camera({floor.x + floor.half_x, height, z}, origin)),
                   kLine);
    }
}

// Postes de 2,5 m (sobresalen de los muros) cada 10 m por dentro del recinto, junto a los
// muros. Solo se dibujan: no son cuerpos de la simulación.
void draw_posts(const sim::BoxDesc& floor, const Vec3d& origin) {
    constexpr Color kPost{236, 232, 220, 255};
    constexpr double kInside = 14.8;
    constexpr double kAlong[] = {-kInside, -5.0, 5.0, kInside};
    sim::BoxDesc post;
    post.half_x = 0.15f;
    post.half_y = 1.25f;
    post.half_z = 0.15f;
    const double y = floor.y + floor.half_y + post.half_y;
    auto draw_at = [&](double x, double z) {
        Pose pose;
        pose.position = {floor.x + x, y, floor.z + z};
        draw_box(pose, post, kPost, origin);
    };
    for (const double along : kAlong) {
        draw_at(along, -kInside);
        draw_at(along, kInside);
    }
    for (const double along : {-5.0, 5.0}) {
        draw_at(-kInside, along);
        draw_at(kInside, along);
    }
}

static Vec3d add(const Vec3d& a, const Vec3d& b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
static Vec3d scale(const Vec3d& a, double s) { return {a.x * s, a.y * s, a.z * s}; }

// Coche en low-poly: la caja de la carrocería y cada rueda como un cilindro de 12 lados con
// una marca en la llanta que gira con ella (para ver el giro y el bloqueo).
void draw_car(const Pose& car_pose, const sim::CarData& car,
              const std::array<sim::SimWorld::WheelPose, sim::kWheelCount>& wheels,
              const Vec3d& origin) {
    sim::BoxDesc body;
    body.half_x = 0.5f * car.box_width;
    body.half_y = 0.5f * car.box_height;
    body.half_z = 0.5f * car.box_length;
    const Pose body_pose =
        attach(car_pose, Vec3d{0.0, car.ground_clearance + 0.5 * car.box_height, 0.0});
    draw_box(body_pose, body, kCarColor, origin);
    for (const sim::SimWorld::WheelPose& w : wheels) {
        // Centro de la rueda en ejes del coche y sus ejes tras el giro de la dirección (Jolt:
        // positivo a la izquierda, alrededor de +y).
        const Vec3d hub_local{w.attach_x, w.attach_y - w.suspension_length, w.attach_z};
        const double c = std::cos(w.steer);
        const double s = std::sin(w.steer);
        const Vec3d axle = rotate(car_pose.rotation, Vec3d{c, 0.0, -s});
        const Vec3d forward = rotate(car_pose.rotation, Vec3d{-s, 0.0, -c});
        const Vec3d up = rotate(car_pose.rotation, Vec3d{0.0, 1.0, 0.0});
        const Vec3d hub = attach(car_pose, hub_local).position;
        const Vec3d half_axle = scale(axle, 0.5 * w.width);
        const Vec3d inner = add(hub, scale(half_axle, -1.0));
        const Vec3d outer = add(hub, half_axle);
        DrawCylinderEx(to_vector(relative_to_camera(inner, origin)),
                       to_vector(relative_to_camera(outer, origin)), w.radius, w.radius, 12,
                       kTireColor);
        // Marca en la llanta, por fuera de la cara exterior: arriba al girar 0 y hacia
        // delante al rodar hacia delante.
        const Vec3d rim = add(add(outer, scale(half_axle, 0.05)),
                              add(scale(up, 0.7 * w.radius * std::cos(w.rotation)),
                                  scale(forward, 0.7 * w.radius * std::sin(w.rotation))));
        DrawSphere(to_vector(relative_to_camera(rim, origin)), 0.045f, kRimColor);
    }
}

} // namespace rally::platform
