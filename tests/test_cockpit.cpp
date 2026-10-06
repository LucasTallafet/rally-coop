// Interior provisional (tarea 2.7): lo que se ve desde el asiento con las medidas del 205 y el
// ángulo del volante.

#include "io/recording.h"
#include "platform/cockpit.h"
#include "sim/car_data.h"

#include <doctest/doctest.h>

#include <cmath>
#include <string>
#include <vector>

using rally::platform::project_from_eye;
using rally::platform::ScreenFraction;

namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kFovY = 55.0 * kPi / 180.0; // decisión cerrada: 55° vertical fijos
constexpr double kAspect = 16.0 / 9.0;

const rally::sim::CarData& car_205() {
    static const rally::sim::CarData car = [] {
        std::string text;
        std::string error;
        REQUIRE(rally::io::read_file(RALLY_SOURCE_DIR "/data/cars/mid_awd_turbo.car", text, error));
        return rally::sim::load_car_or_abort(text, "mid_awd_turbo", rally::sim::CarKeys::Full);
    }();
    return car;
}

ScreenFraction see(double ahead, double right, double up) {
    return project_from_eye(ahead, right, up, kFovY, kAspect);
}

} // namespace

TEST_CASE("cockpit: from the seat the roof, dash and pillars cover what the plan says") {
    const rally::sim::CockpitData& k = car_205().cockpit;
    // Techo: el borde superior del parabrisas a +15,3°: el 23,8 % de arriba de la imagen.
    const ScreenFraction roof = see(k.windshield_top_ahead, 0.0, k.windshield_top_above);
    MESSAGE("techo: ", roof.y * 100.0, " % de arriba");
    CHECK(roof.y == doctest::Approx(0.238).epsilon(0.005 / 0.238));
    // Salpicadero: la base a −23,2°: el 8,8 % de abajo.
    const ScreenFraction dash = see(k.windshield_base_ahead, 0.0, -k.windshield_base_below);
    MESSAGE("salpicadero: ", (1.0 - dash.y) * 100.0, " % de abajo");
    CHECK(1.0 - dash.y == doctest::Approx(0.088).epsilon(0.005 / 0.088));
    // Montante izquierdo en la base: a 19,7°, a ~31 % del ancho desde el borde izquierdo.
    const ScreenFraction left_base =
        see(k.windshield_base_ahead, -k.pillar_left, -k.windshield_base_below);
    MESSAGE("montante izquierdo (base): ", left_base.x * 100.0, " % del ancho");
    CHECK(left_base.x == doctest::Approx(0.307).epsilon(0.005 / 0.307));
    // Montante derecho: fuera de la imagen (≥ 56°), en la base y arriba.
    const double inner_right = k.pillar_right - 0.5 * k.pillar_width;
    CHECK(see(k.windshield_base_ahead, inner_right, -k.windshield_base_below).x > 1.0);
    CHECK(see(k.windshield_top_ahead, inner_right, k.windshield_top_above).x > 1.0);
    // Volante: el borde superior del aro (inclinado, más cerca del ojo) se ve.
    const double r = 0.5 * k.wheel_diameter;
    const double top_ahead = k.wheel_ahead - r * std::sin(k.wheel_tilt);
    const double top_up = -k.wheel_center_below + r * std::cos(k.wheel_tilt);
    const ScreenFraction wheel_top = see(top_ahead, 0.0, top_up);
    MESSAGE("borde superior del volante: ", (1.0 - wheel_top.y) * 100.0, " % de abajo");
    CHECK(wheel_top.y < 1.0);
    CHECK(wheel_top.y > 0.9);
    // Pantalla: entera dentro de la imagen y por encima del volante.
    const ScreenFraction display_bottom =
        see(k.display_ahead, k.display_right, -k.display_below - 0.5 * k.display_height);
    CHECK(display_bottom.y < wheel_top.y);
}

TEST_CASE("cockpit: the steering wheel turns 13 times the inner wheel angle") {
    const rally::sim::CarData& car = car_205();
    for (const float steer : {-1.0f, -0.5f, 0.0f, 0.37f, 1.0f}) {
        const double expected_deg = steer * 30.0 * 13.0;
        const double got_deg = rally::platform::steering_wheel_angle(steer, car) * 180.0 / kPi;
        CAPTURE(steer);
        CHECK(std::abs(got_deg - expected_deg) < 0.1);
    }
    MESSAGE("a fondo: ", rally::platform::steering_wheel_angle(1.0f, car) * 180.0 / kPi, "°");
}

TEST_CASE("cockpit: the interior is a few hundred triangles") {
    const rally::sim::CarData& car = car_205();
    std::vector<rally::platform::CockpitTri> tris = rally::platform::build_cockpit(car);
    const std::size_t fixed = tris.size();
    rally::platform::append_steering_wheel(tris, car, 0.3);
    rally::platform::CockpitDisplay display;
    display.gear = 3;
    display.rpm_fraction = 0.9f;
    display.red_fraction = 0.85f;
    display.boost_fraction = 0.5f;
    rally::platform::append_display(tris, car, display);
    MESSAGE("triángulos: ", fixed, " fijos y ", tris.size() - fixed, " del volante y la pantalla");
    CHECK(tris.size() < 400);
    // Todos los vértices, a menos de 2 m del ojo (la esquina de la puerta con el suelo, a ~1,5).
    const rally::platform::Vec3d eye = rally::platform::cockpit_point(car, 0.0, 0.0, 0.0);
    for (const rally::platform::CockpitTri& t : tris) {
        for (const rally::platform::Vec3f& v : {t.a, t.b, t.c}) {
            const double d = std::hypot(v.x - eye.x, v.y - eye.y, v.z - eye.z);
            REQUIRE(d < 2.0);
        }
    }
}
