#include "platform/camera_space.h"
#include "platform/seat.h"
#include "sim/car_data.h"

#include <doctest/doctest.h>

#include <cmath>

using rally::platform::car_eye;
using rally::platform::driver_eye;
using rally::platform::kSeatLimitCm;
using rally::platform::nudge_seat;
using rally::platform::SeatAxis;
using rally::platform::SeatOffsetCm;
using rally::platform::Vec3d;

namespace {

rally::sim::CarData eye_car() {
    rally::sim::CarData car;
    car.eye_left = 0.37f;
    car.eye_height = 1.05f;
    car.eye_behind_front_axle = 1.0f;
    car.wheelbase = 2.5f;
    return car;
}

} // namespace

TEST_CASE("seat: the car eye is the one the camera used before the seat") {
    const rally::sim::CarData car = eye_car();
    const Vec3d eye = car_eye(car);
    // Como drive_demo.cpp hasta la 2.4.
    CHECK(eye.x == -car.eye_left);
    CHECK(eye.y == car.eye_height);
    CHECK(eye.z == -0.5 * car.wheelbase + car.eye_behind_front_axle);
    CHECK(driver_eye(car, SeatOffsetCm{}).x == eye.x);
    CHECK(driver_eye(car, SeatOffsetCm{}).y == eye.y);
    CHECK(driver_eye(car, SeatOffsetCm{}).z == eye.z);
}

TEST_CASE("seat: the eye moves exactly what was asked, in car axes") {
    const rally::sim::CarData car = eye_car();
    const Vec3d eye = car_eye(car);
    const Vec3d up = driver_eye(car, SeatOffsetCm{5, 0, 0});
    CHECK(up.y == eye.y + 5 / 100.0); // bit a bit
    CHECK(up.x == eye.x);
    CHECK(up.z == eye.z);
    const Vec3d forward = driver_eye(car, SeatOffsetCm{0, 3, 0});
    CHECK(forward.z == eye.z - 3 / 100.0); // delante es −z
    CHECK(forward.x == eye.x);
    const Vec3d right = driver_eye(car, SeatOffsetCm{0, 0, 2});
    CHECK(right.x == eye.x + 2 / 100.0);
    CHECK(right.y == eye.y);
}

TEST_CASE("seat: 1.05 m plus 5 cm is the 1.10 m eye") {
    const rally::sim::CarData car = eye_car();
    CHECK(driver_eye(car, SeatOffsetCm{5, 0, 0}).y == doctest::Approx(1.10).epsilon(1e-7));
}

TEST_CASE("seat: the offset turns with the car") {
    // Coche girado 90° a la izquierda en guiñada: su delante (−z) es −x del mundo.
    rally::platform::Pose car;
    car.position = {100.0, 0.0, 50.0};
    const double half = 0.25 * 3.141592653589793;
    car.rotation = {0.0, std::sin(half), 0.0, std::cos(half)};
    const rally::sim::CarData data = eye_car();
    const Vec3d base = rally::platform::attach(car, driver_eye(data, SeatOffsetCm{})).position;
    const Vec3d moved =
        rally::platform::attach(car, driver_eye(data, SeatOffsetCm{0, 10, 0})).position;
    CHECK(moved.x - base.x == doctest::Approx(-0.10).epsilon(1e-9));
    CHECK(moved.z - base.z == doctest::Approx(0.0).epsilon(1e-9));
    CHECK(moved.y == doctest::Approx(base.y));
}

TEST_CASE("seat: limits are respected") {
    SeatOffsetCm seat;
    for (int i = 0; i < 16; ++i) {
        seat = nudge_seat(seat, SeatAxis::Up, 1);
    }
    CHECK(seat.up == kSeatLimitCm);
    seat = nudge_seat(SeatOffsetCm{0, -kSeatLimitCm, 0}, SeatAxis::Forward, -1);
    CHECK(seat.forward == -kSeatLimitCm);
    seat = nudge_seat(SeatOffsetCm{}, SeatAxis::Right, -3);
    CHECK(seat == SeatOffsetCm{0, 0, -3});
}
