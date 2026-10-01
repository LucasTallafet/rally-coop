#include "platform/camera_space.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <numbers>

using rally::platform::attach;
using rally::platform::Pose;
using rally::platform::Quatd;
using rally::platform::relative_to_camera;
using rally::platform::rotate;
using rally::platform::Vec3d;
using rally::platform::Vec3f;

namespace {

// Giro de `angle` radianes alrededor del eje (x, y, z), unitario.
Quatd axis_angle(double x, double y, double z, double angle) {
    const double s = std::sin(angle * 0.5);
    return {x * s, y * s, z * s, std::cos(angle * 0.5)};
}

// Error máximo por componente entre un vector en float y el esperado en doble.
double max_error(const Vec3f& got, const Vec3d& expected) {
    return std::max(
        {std::abs(got.x - expected.x), std::abs(got.y - expected.y), std::abs(got.z - expected.z)});
}

void check_near(const Vec3d& got, const Vec3d& expected) {
    CHECK(got.x == doctest::Approx(expected.x).epsilon(1e-12));
    CHECK(got.y == doctest::Approx(expected.y).epsilon(1e-12));
    CHECK(got.z == doctest::Approx(expected.z).epsilon(1e-12));
}

} // namespace

TEST_CASE("camera_space: 20 km from the origin a vertex 1 m from the camera keeps sub-mm "
          "precision") {
    // Cámara a 20 km en x y z, con decimales que el float no representa. El vértice está a
    // exactamente 1 m (0,6² + 0,48² + 0,64² = 1).
    const Vec3d camera{20000.3141592653, 1.2, 20000.2718281828};
    const Vec3d offset{0.6, 0.48, -0.64};
    const Vec3d vertex{camera.x + offset.x, camera.y + offset.y, camera.z + offset.z};

    const double relative_error = max_error(relative_to_camera(vertex, camera), offset);

    // Lo que el render relativo evita: pasar cada posición a float del mundo y restar allí.
    const Vec3f world_float{static_cast<float>(vertex.x) - static_cast<float>(camera.x),
                            static_cast<float>(vertex.y) - static_cast<float>(camera.y),
                            static_cast<float>(vertex.z) - static_cast<float>(camera.z)};
    const double world_float_error = max_error(world_float, offset);

    MESSAGE("error a 20 km: relativo ", relative_error * 1000.0, " mm, float del mundo ",
            world_float_error * 1000.0, " mm");
    CHECK(relative_error < 1e-5);    // < 0,01 mm
    CHECK(world_float_error > 1e-4); // > 0,1 mm: el contraste existe con estos valores
}

TEST_CASE("camera_space: rotate turns vectors like the quaternion") {
    // 90° a la izquierda alrededor de +y: adelante (-z) pasa a -x y arriba sigue arriba.
    const Quatd left = axis_angle(0.0, 1.0, 0.0, std::numbers::pi / 2.0);
    check_near(rotate(left, {0.0, 0.0, -1.0}), {-1.0, 0.0, 0.0});
    check_near(rotate(left, {0.0, 1.0, 0.0}), {0.0, 1.0, 0.0});

    // 90° alrededor de +x: +y pasa a +z.
    const Quatd pitch = axis_angle(1.0, 0.0, 0.0, std::numbers::pi / 2.0);
    check_near(rotate(pitch, {0.0, 1.0, 0.0}), {0.0, 0.0, 1.0});

    // La identidad no cambia nada.
    check_near(rotate(Quatd{}, {0.25, -3.0, 7.5}), {0.25, -3.0, 7.5});
}

TEST_CASE("camera_space: attach moves the offset with the body and keeps its rotation") {
    Pose body;
    body.position = {20010.0, 2.0, 19997.0};
    body.rotation = axis_angle(0.0, 1.0, 0.0, std::numbers::pi / 2.0);

    // Ojo 0,4 m por encima y 1 m adelante: con el giro, adelante es -x.
    const Pose eye = attach(body, {0.0, 0.4, -1.0});
    check_near(eye.position, {20009.0, 2.4, 19997.0});
    CHECK(eye.rotation.x == body.rotation.x);
    CHECK(eye.rotation.y == body.rotation.y);
    CHECK(eye.rotation.z == body.rotation.z);
    CHECK(eye.rotation.w == body.rotation.w);

    // Sin giro, el desplazamiento se suma tal cual.
    Pose still;
    still.position = {1.0, 2.0, 3.0};
    check_near(attach(still, {0.0, 0.4, 0.0}).position, {1.0, 2.4, 3.0});
}
