#include "platform/interpolation.h"

#include <doctest/doctest.h>

#include <cmath>
#include <initializer_list>
#include <numbers>

using rally::platform::interpolate;
using rally::platform::Pose;
using rally::platform::Quatd;

namespace {

// Giro de `angle` radianes alrededor del eje vertical.
Quatd yaw(double angle) { return {0.0, std::sin(angle * 0.5), 0.0, std::cos(angle * 0.5)}; }

double length(const Quatd& q) { return std::sqrt(q.x * q.x + q.y * q.y + q.z * q.z + q.w * q.w); }

// Ángulo de giro de un cuaternión alrededor del eje vertical.
double yaw_angle(const Quatd& q) { return 2.0 * std::atan2(q.y, q.w); }

Pose make_pose(double x, double y, double z, double angle) {
    Pose pose;
    pose.position = {x, y, z};
    pose.rotation = yaw(angle);
    return pose;
}

} // namespace

TEST_CASE("interpolation: alpha 0 gives the previous pose") {
    const Pose from = make_pose(1000.25, -3.5, 12.0, 0.3);
    const Pose to = make_pose(1000.75, -3.25, 12.5, 0.35);
    const Pose out = interpolate(from, to, 0.0);
    CHECK(out.position.x == from.position.x);
    CHECK(out.position.y == from.position.y);
    CHECK(out.position.z == from.position.z);
    CHECK(out.rotation.y == doctest::Approx(from.rotation.y).epsilon(1e-15));
    CHECK(out.rotation.w == doctest::Approx(from.rotation.w).epsilon(1e-15));
}

TEST_CASE("interpolation: alpha 1 gives the last pose") {
    const Pose from = make_pose(0.0, 0.0, 0.0, 0.0);
    const Pose to = make_pose(2.0, 4.0, -8.0, 0.1);
    const Pose out = interpolate(from, to, 1.0);
    CHECK(out.position.x == doctest::Approx(2.0));
    CHECK(out.position.y == doctest::Approx(4.0));
    CHECK(out.position.z == doctest::Approx(-8.0));
    CHECK(yaw_angle(out.rotation) == doctest::Approx(0.1).epsilon(1e-12));
}

TEST_CASE("interpolation: position is linear") {
    const Pose from = make_pose(-10.0, 2.0, 100.0, 0.0);
    const Pose to = make_pose(10.0, 6.0, 0.0, 0.0);
    for (const double alpha : {0.125, 0.25, 0.5, 0.75, 0.99}) {
        const Pose out = interpolate(from, to, alpha);
        CHECK(out.position.x == doctest::Approx(-10.0 + 20.0 * alpha));
        CHECK(out.position.y == doctest::Approx(2.0 + 4.0 * alpha));
        CHECK(out.position.z == doctest::Approx(100.0 - 100.0 * alpha));
    }
}

TEST_CASE("interpolation: rotation stays normalized") {
    const Pose from = make_pose(0.0, 0.0, 0.0, 0.0);
    const Pose to = make_pose(0.0, 0.0, 0.0, 2.0); // giro grande: nlerp acorta el cuaternión
    for (const double alpha : {0.1, 0.3, 0.5, 0.7, 0.9}) {
        CHECK(length(interpolate(from, to, alpha).rotation) == doctest::Approx(1.0).epsilon(1e-14));
    }
}

TEST_CASE("interpolation: rotation follows the short arc") {
    Pose from = make_pose(0.0, 0.0, 0.0, 0.2);
    Pose to = make_pose(0.0, 0.0, 0.0, 0.3);
    // -q es la misma rotación que q: el resultado no debe cambiar ni dar la vuelta larga.
    to.rotation = {-to.rotation.x, -to.rotation.y, -to.rotation.z, -to.rotation.w};
    const Pose out = interpolate(from, to, 0.5);
    CHECK(std::remainder(yaw_angle(out.rotation), 2.0 * std::numbers::pi) ==
          doctest::Approx(0.25).epsilon(1e-6));
}

TEST_CASE("interpolation: small steps match slerp") {
    // Un paso de 1/240 s girando a 2 vueltas por segundo: unos 0,05 rad.
    const double step = 2.0 * 2.0 * std::numbers::pi / 240.0;
    const Pose from = make_pose(0.0, 0.0, 0.0, 1.0);
    const Pose to = make_pose(0.0, 0.0, 0.0, 1.0 + step);
    for (const double alpha : {0.25, 0.5, 0.75}) {
        const double angle = yaw_angle(interpolate(from, to, alpha).rotation);
        CHECK(std::abs(angle - (1.0 + step * alpha)) < 1e-5);
    }
}
