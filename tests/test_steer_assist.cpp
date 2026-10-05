#include "car_worlds.h"
#include "sim/input_smoothing.h"
#include "sim/steer_assist.h"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstdint>

using rally::sim::CarData;
using rally::sim::clip_steer;
using rally::sim::kAxisMax;
using rally::sim::kSteerLeft;
using rally::sim::kSteerRight;
using rally::sim::SteerAssistInput;
using rally::sim::SteerContext;
using rally::sim::SteerSide;
using rally::sim::Surface;

namespace {

constexpr double kPi = 3.14159265358979323846;

float deg(double degrees) { return static_cast<float>(degrees * kPi / 180.0); }

float tan_peak(Surface surface) {
    return rally::tests::reference_car().tire(surface).tan_alpha_peak;
}

// Coche en recta, hacia delante a `kmh`, con las cuatro ruedas cargadas sobre `surface` y las
// velocidades en las ruedas a `front_angle` (delanteras) y `rear_angle` (eje trasero).
SteerAssistInput driving(double kmh, Surface surface, float front_angle = 0.0f,
                         float rear_angle = 0.0f) {
    SteerAssistInput in;
    in.speed = static_cast<float>(kmh / 3.6);
    in.forward_speed = in.speed;
    in.side[kSteerRight] = SteerSide{true, true, front_angle, tan_peak(surface)};
    in.side[kSteerLeft] = SteerSide{true, true, front_angle, tan_peak(surface)};
    in.rear_loaded = true;
    in.rear_vel_angle = rear_angle;
    in.rear_tan_alpha_peak = tan_peak(surface);
    return in;
}

// La cuenta de steer_assist.cpp: dirección en [0, 1] → unidades de eje, al más cercano.
std::int32_t axis_units(float axis) {
    return static_cast<std::int32_t>(std::floor(axis * static_cast<float>(kAxisMax) + 0.5f));
}

// Ángulo de la rueda exterior con el tope `limit` (unidades de eje).
double outer_angle(const CarData& car, std::int32_t limit) {
    return rally::sim::wheel_angle_for_axis(car, static_cast<float>(limit) / kAxisMax, true);
}

} // namespace

TEST_CASE("steer assist: the Ackermann inverse round-trips the outer wheel angle") {
    const CarData& car = rally::tests::reference_car();
    const float outer_max = rally::sim::wheel_angle_for_axis(car, 1.0f, true);
    CHECK(outer_max == doctest::Approx(deg(26.7719)).epsilon(1.0e-4));
    for (const double angle : {0.5, 1.0, 7.0, 14.0, 20.0, 26.0}) {
        INFO(angle, "°");
        const float axis = rally::sim::axis_for_wheel_angle(car, deg(angle), true);
        CHECK(std::abs(rally::sim::wheel_angle_for_axis(car, axis, true) - deg(angle)) < 1.0e-6f);
        // La interior es lineal.
        const float inner_axis = rally::sim::axis_for_wheel_angle(car, deg(angle), false);
        CHECK(inner_axis == doctest::Approx(deg(angle) / car.max_steer));
    }
    CHECK(rally::sim::axis_for_wheel_angle(car, 0.0f, true) == 0.0f);
    CHECK(rally::sim::axis_for_wheel_angle(car, -0.1f, true) == 0.0f);
    CHECK(rally::sim::axis_for_wheel_angle(car, outer_max + 0.01f, true) == 1.0f);
}

TEST_CASE("steer assist: switched off or below 15 km/h it never clips") {
    const CarData& car = rally::tests::reference_car();
    for (const double kmh : {10.0, 60.0, 100.0}) {
        INFO(kmh, " km/h");
        const SteerContext off =
            rally::sim::steer_context(driving(kmh, Surface::Asphalt), car, false);
        CHECK_FALSE(off.active);
        CHECK(off.right_max == kAxisMax);
        CHECK(off.left_max == kAxisMax);
        CHECK(clip_steer(kAxisMax, off) == kAxisMax);
        CHECK(clip_steer(-kAxisMax, off) == -kAxisMax);
    }
    const SteerContext slow = rally::sim::steer_context(driving(10.0, Surface::Asphalt), car, true);
    CHECK_FALSE(slow.active);
    CHECK(slow.right_max == kAxisMax);
    CHECK(slow.left_max == kAxisMax);
    CHECK(slow.speed == doctest::Approx(10.0 / 3.6));
}

TEST_CASE("steer assist: at speed in a straight line the outer wheel stops at the peak slip "
          "angle") {
    const CarData& car = rally::tests::reference_car();
    for (const Surface surface : {Surface::Asphalt, Surface::Gravel}) {
        const double peak = surface == Surface::Asphalt ? 7.0 : 14.0;
        for (const double kmh : {60.0, 100.0}) {
            INFO(rally::sim::surface_name(surface), " a ", kmh, " km/h");
            const SteerContext ctx = rally::sim::steer_context(driving(kmh, surface), car, true);
            CHECK(ctx.active);
            CHECK_FALSE(ctx.rear_sliding);
            // Medio paso del eje son 8e-6 rad en la exterior.
            CHECK(std::abs(outer_angle(car, ctx.right_max) - deg(peak)) < 1.0e-5);
            CHECK(ctx.left_max == ctx.right_max);
            CHECK(clip_steer(kAxisMax, ctx) == ctx.right_max);
            CHECK(clip_steer(-kAxisMax, ctx) == -ctx.left_max);
            // Lo que pide el jugador por dentro de la ventana pasa tal cual.
            CHECK(clip_steer(ctx.right_max / 2, ctx) == ctx.right_max / 2);
        }
    }
}

TEST_CASE("steer assist: from 15 to 30 km/h the limit blends linearly with full lock") {
    const CarData& car = rally::tests::reference_car();
    const SteerContext full = rally::sim::steer_context(driving(60.0, Surface::Asphalt), car, true);
    const SteerContext mid = rally::sim::steer_context(driving(22.5, Surface::Asphalt), car, true);
    CHECK(mid.active);
    const auto half_way = static_cast<std::int32_t>(std::floor(
        static_cast<float>(kAxisMax) +
        0.5f * (static_cast<float>(full.right_max) - static_cast<float>(kAxisMax)) + 0.5f));
    CHECK(mid.right_max == half_way);
    CHECK(mid.left_max == half_way);
    const SteerContext start =
        rally::sim::steer_context(driving(15.0, Surface::Asphalt), car, true);
    CHECK(start.right_max == kAxisMax);
    const SteerContext end = rally::sim::steer_context(driving(30.0, Surface::Asphalt), car, true);
    CHECK(end.right_max == full.right_max);
}

TEST_CASE("steer assist: the window follows the front velocity and never turns the wheels on its "
          "own") {
    const CarData& car = rally::tests::reference_car();
    // Giro a la derecha con agarre: la velocidad en las delanteras va 2° a la derecha. El borde
    // derecho pasa a 9° y el izquierdo a 5°.
    const SteerContext grip =
        rally::sim::steer_context(driving(60.0, Surface::Asphalt, deg(2.0), deg(-3.0)), car, true);
    CHECK_FALSE(grip.rear_sliding);
    CHECK(std::abs(outer_angle(car, grip.right_max) - deg(9.0)) < 1.0e-5);
    CHECK(std::abs(outer_angle(car, grip.left_max) - deg(5.0)) < 1.0e-5);

    // Volante centrado en pleno derrape: 0, la ayuda no contravolantea sola.
    const SteerContext drift = rally::sim::steer_context(
        driving(60.0, Surface::Gravel, deg(-20.0), deg(-25.0)), car, true);
    CHECK(drift.rear_sliding);
    CHECK(clip_steer(0, drift) == 0);

    // Sobreviraje con la entrada hacia dentro: la ventana entera queda al otro lado del 0 (borde
    // derecho −10° + 7° = −3°) y la regla da 0.
    const SteerContext inward = rally::sim::steer_context(
        driving(60.0, Surface::Asphalt, deg(-10.0), deg(-4.0)), car, true);
    CHECK_FALSE(inward.rear_sliding);
    CHECK(inward.right_max == 0);
    CHECK(clip_steer(kAxisMax, inward) == 0);
    CHECK(clip_steer(500, inward) == 0);
}

TEST_CASE("steer assist: full countersteer gives full lock only with the rear past its peak") {
    const CarData& car = rally::tests::reference_car();
    // Sobreviraje girando a la derecha: la trasera se va a la izquierda (δ_vel,tras < 0) y las
    // delanteras también llevan la velocidad a la izquierda (−12°).
    SteerAssistInput in = driving(60.0, Surface::Asphalt, deg(-12.0), deg(-20.0));
    const SteerContext sliding = rally::sim::steer_context(in, car, true);
    CHECK(sliding.active);
    CHECK(sliding.rear_sliding);
    CHECK(sliding.left_max == kAxisMax);
    CHECK(clip_steer(-kAxisMax, sliding) == -kAxisMax);
    // Hacia dentro sigue recortando (borde −12° + 7° < 0).
    CHECK(sliding.right_max == 0);

    // La misma entrada con la trasera por debajo de su pico (5° < 7°): es un giro a la izquierda
    // como otro cualquiera y recibe su ventana: borde −12° − 7° = 19° hacia la izquierda.
    in.rear_vel_angle = deg(-5.0);
    const SteerContext grip = rally::sim::steer_context(in, car, true);
    CHECK_FALSE(grip.rear_sliding);
    CHECK(grip.left_max == axis_units(rally::sim::axis_for_wheel_angle(car, deg(19.0), true)));
    CHECK(clip_steer(-kAxisMax, grip) == -grip.left_max);

    // Con la trasera hacia la derecha, el contravolante es hacia la derecha.
    in = driving(60.0, Surface::Asphalt, deg(12.0), deg(20.0));
    const SteerContext mirrored = rally::sim::steer_context(in, car, true);
    CHECK(mirrored.rear_sliding);
    CHECK(mirrored.right_max == kAxisMax);
    CHECK(mirrored.left_max == 0);

    // Traseras sin carga: no desliza, aunque el ángulo sea grande.
    in.rear_loaded = false;
    const SteerContext unloaded = rally::sim::steer_context(in, car, true);
    CHECK_FALSE(unloaded.rear_sliding);
    CHECK(unloaded.right_max < kAxisMax);
}

TEST_CASE("steer assist: reverse, backwards motion and both front wheels in the air give full "
          "lock") {
    const CarData& car = rally::tests::reference_car();
    SteerAssistInput reverse = driving(60.0, Surface::Asphalt);
    reverse.reverse = true;
    const SteerContext r = rally::sim::steer_context(reverse, car, true);
    CHECK_FALSE(r.active);
    CHECK(r.right_max == kAxisMax);
    CHECK(r.left_max == kAxisMax);

    SteerAssistInput backwards = driving(60.0, Surface::Asphalt);
    backwards.forward_speed = -backwards.speed;
    CHECK(rally::sim::steer_context(backwards, car, true).right_max == kAxisMax);

    SteerAssistInput air = driving(60.0, Surface::Asphalt);
    air.side[kSteerRight].loaded = false;
    air.side[kSteerLeft].loaded = false;
    const SteerContext flying = rally::sim::steer_context(air, car, true);
    CHECK_FALSE(flying.active);
    CHECK(flying.right_max == kAxisMax);
    CHECK(flying.left_max == kAxisMax);

    // Con la exterior en el aire, decide la interior con su propia relación (lineal).
    SteerAssistInput one = driving(60.0, Surface::Asphalt);
    one.side[kSteerRight].outer = false;
    const SteerContext inner = rally::sim::steer_context(one, car, true);
    CHECK(inner.active);
    CHECK(inner.right_max == axis_units(deg(7.0) / car.max_steer));
}
