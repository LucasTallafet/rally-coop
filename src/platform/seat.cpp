#include "platform/seat.h"

#include <algorithm>

namespace rally::platform {

namespace {

double cm_to_m(int cm) { return static_cast<double>(cm) / 100.0; }

} // namespace

bool seat_in_limits(int cm) { return cm >= -kSeatLimitCm && cm <= kSeatLimitCm; }

SeatOffsetCm nudge_seat(SeatOffsetCm seat, SeatAxis axis, int delta_cm) {
    int& value = axis == SeatAxis::Up        ? seat.up
                 : axis == SeatAxis::Forward ? seat.forward
                                             : seat.right;
    value = std::clamp(value + delta_cm, -kSeatLimitCm, kSeatLimitCm);
    return seat;
}

Vec3d car_eye(const sim::CarData& car) {
    return Vec3d{-static_cast<double>(car.eye_left), static_cast<double>(car.eye_height),
                 -0.5 * static_cast<double>(car.wheelbase) +
                     static_cast<double>(car.eye_behind_front_axle)};
}

Vec3d driver_eye(const sim::CarData& car, const SeatOffsetCm& seat) {
    const Vec3d eye = car_eye(car);
    return Vec3d{eye.x + cm_to_m(seat.right), eye.y + cm_to_m(seat.up),
                 eye.z - cm_to_m(seat.forward)};
}

} // namespace rally::platform
