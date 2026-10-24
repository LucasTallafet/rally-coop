// Vista del copiloto desde el asiento derecho (lote 3, arreglo copiloto-asiento): el ojo, Tab, la
// mirada al salpicadero con B, el volante y la pantalla con lo que llega por la red.

#include "io/recording.h"
#include "net/car_state.h"
#include "net/state_buffer.h"
#include "platform/camera_space.h"
#include "platform/cockpit.h"
#include "platform/copilot_keys.h"
#include "platform/copilot_view.h"
#include "platform/keys.h"
#include "platform/seat.h"
#include "sim/car_data.h"

#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <cstdint>
#include <string>

using namespace rally::platform;

namespace {

constexpr double kPi = 3.14159265358979323846;

rally::sim::CarData load_car(const char* name) {
    std::string text;
    std::string error;
    REQUIRE(rally::io::read_file(std::string(RALLY_SOURCE_DIR) + "/data/cars/" + name + ".car",
                                 text, error));
    return rally::sim::load_car_or_abort(text, name, rally::sim::CarKeys::Full);
}

constexpr const char* kCars[] = {"mid_awd_turbo", "front_awd_turbo_5cyl", "front_awd_turbo_groupa",
                                 "front_rwd_na"};

// Un estado de red como lo recibe el copiloto: codificado, decodificado y muestreado del búfer.
rally::net::SampledState received(float steer, float boost_bar, int gear, float rpm) {
    rally::net::CarState sent;
    sent.steer = steer;
    sent.boost_bar = boost_bar;
    sent.gear = static_cast<std::int8_t>(gear);
    sent.rpm = rpm;
    std::array<std::uint8_t, rally::net::kCarStateBytes> bytes{};
    rally::net::encode(sent, bytes);
    rally::net::CarState got;
    REQUIRE(rally::net::decode(bytes, got));
    rally::net::StateBuffer buffer;
    buffer.reset(0);
    buffer.push(got, 0.0);
    rally::net::Pose pose;
    rally::net::SampledState extra;
    REQUIRE(buffer.sample(0.0, pose, extra));
    return extra;
}

} // namespace

TEST_CASE("copilot seat: the eye is the driver's with x mirrored, in the four cars") {
    for (const char* name : kCars) {
        const rally::sim::CarData car = load_car(name);
        const Vec3d driver = car_eye(car);
        const Vec3d copilot = copilot_eye(car);
        INFO(name, ": eye_left ", car.eye_left);
        CHECK(copilot.x == -driver.x);
        CHECK(copilot.y == driver.y);
        CHECK(copilot.z == driver.z);
        CHECK(copilot.x > 0.0); // a la derecha del eje del coche
        CHECK(copilot.x == doctest::Approx(0.37));
    }
}

TEST_CASE("copilot seat: Tab goes seat, chase, whole zone and back") {
    int view = 0;
    view = next_camera_view(view);
    CHECK(view == 1);
    view = next_camera_view(view);
    CHECK(view == 2);
    view = next_camera_view(view);
    CHECK(view == 0);
}

TEST_CASE("copilot seat: the head turns to the dash while B is held and comes back") {
    CHECK(look_yaw(0.0) == 0.0);
    CHECK(look_yaw(1.0) == doctest::Approx(kLookDashYaw));
    CHECK(kLookDashYaw == doctest::Approx(25.0 * kPi / 180.0));
    double t = look_step(0.0, true, kLookSeconds * 0.5);
    CHECK(t == doctest::Approx(0.5));
    CHECK(look_yaw(t) == doctest::Approx(0.5 * kLookDashYaw)); // punto medio de la curva
    t = look_step(t, true, 1.0);
    CHECK(t == 1.0);
    t = look_step(t, false, kLookSeconds * 0.25);
    CHECK(t == doctest::Approx(0.75));
    t = look_step(t, false, 1.0);
    CHECK(t == 0.0);
}

TEST_CASE("copilot seat: with the head turned the dash and the wheel are in the picture") {
    constexpr double kFovY = 55.0 * kPi / 180.0;
    constexpr double kAspect = 16.0 / 9.0;
    for (const char* name : kCars) {
        const rally::sim::CarData car = load_car(name);
        const rally::sim::CockpitData& k = car.cockpit;
        const double dx = car_eye(car).x - copilot_eye(car).x; // del ojo del copiloto al nominal
        const double hw = 0.5 * k.display_width;
        const double hh = 0.5 * k.display_height;
        struct P {
            double ahead, right, up;
        };
        // Esquinas de la pantalla y centro del aro, relativos al ojo nominal del conductor.
        const P points[] = {
            {k.display_ahead, k.display_right - hw, -k.display_below + hh},
            {k.display_ahead, k.display_right + hw, -k.display_below - hh},
            {k.wheel_ahead, 0.0, -k.wheel_center_below},
        };
        const double yaw = kLookDashYaw;
        for (const P& p : points) {
            const double right = p.right + dx; // relativo al ojo del copiloto
            // Cabeza girada `yaw` a la izquierda: su adelante y su derecha.
            const double head_ahead = p.ahead * std::cos(yaw) - right * std::sin(yaw);
            const double head_right = right * std::cos(yaw) + p.ahead * std::sin(yaw);
            REQUIRE(head_ahead > 0.0);
            const ScreenFraction f = project_from_eye(head_ahead, head_right, p.up, kFovY, kAspect);
            INFO(name, ": x ", f.x, " y ", f.y);
            CHECK(f.x > 0.0);
            CHECK(f.x < 1.0);
            CHECK(f.y > 0.0);
            CHECK(f.y < 1.0);
        }
        // Y al frente (sin giro) no se ve nada de eso: la pantalla queda fuera por la izquierda.
        const double right = points[0].right + dx;
        CHECK(project_from_eye(points[0].ahead, right, points[0].up, kFovY, kAspect).x < 0.0);
    }
}

TEST_CASE("copilot seat: the received steering reaches the wheel angle, x13 like the driver's") {
    const rally::sim::CarData car = load_car("mid_awd_turbo");
    CHECK(car.cockpit.steering_ratio == doctest::Approx(13.0));
    const double deg = 180.0 / kPi;
    // El estado pasa por encode, decode y el búfer antes de llegar al volante (16 bits: 0,5 viaja
    // como 16384 / 32767).
    CHECK(copilot_cockpit(received(1.0f, 0, 0, 0), car).wheel_angle * deg ==
          doctest::Approx(390.0).epsilon(1e-6));
    CHECK(copilot_cockpit(received(-1.0f, 0, 0, 0), car).wheel_angle * deg ==
          doctest::Approx(-390.0).epsilon(1e-6));
    CHECK(copilot_cockpit(received(0.5f, 0, 0, 0), car).wheel_angle * deg ==
          doctest::Approx(195.006).epsilon(0.01 / 195.0));
    // Y es la misma cuenta que la del conductor.
    const rally::net::SampledState half = received(0.5f, 0, 0, 0);
    CHECK(copilot_cockpit(half, car).wheel_angle == steering_wheel_angle(half.steer, car));
}

TEST_CASE("copilot seat: the dash shows the received gear, rpm and boost") {
    const rally::sim::CarData car = load_car("mid_awd_turbo");
    const double rpm_limit = car.limiter_speed * 60.0 / (2.0 * kPi);
    const CopilotCockpit c =
        copilot_cockpit(received(0.0f, 0.8f, 3, static_cast<float>(0.5 * rpm_limit)), car);
    CHECK(c.display.gear == 3);
    CHECK(c.display.turbo);
    CHECK(c.display.rpm_fraction == doctest::Approx(0.5).epsilon(0.002));
    CHECK(c.display.boost_fraction == doctest::Approx(0.8e5 / car.boost_max).epsilon(0.01));
    CHECK(c.display.red_fraction == doctest::Approx(car.upshift_full / car.limiter_speed));
    // La marcha de un paquete malo se acota a la caja (R, N, 1 a 5): no indexa fuera.
    CHECK(copilot_cockpit(received(0.0f, 0, 7, 0), car).display.gear == 5);
    CHECK(copilot_cockpit(received(0.0f, 0, -3, 0), car).display.gear == -1);
    CHECK(copilot_cockpit(received(0.0f, 0, -1, 0), car).display.gear == -1);
    // Sin turbo no hay barra de presión.
    const rally::sim::CarData na = load_car("front_rwd_na");
    CHECK(na.boost_max == 0.0f);
    const CopilotCockpit n = copilot_cockpit(received(0.0f, 0.5f, 2, 3000.0f), na);
    CHECK_FALSE(n.display.turbo);
    CHECK(n.display.boost_fraction == 0.0f);
}

TEST_CASE("copilot seat: B is the held look, only in the copilot window; Tab names the seat") {
    const ActionRef b = action(CopilotAction::LookAtDash);
    CHECK(chords_for(b)[0].key == Key::B);
    CHECK(contexts_for(b) == kContextCopilot);
    CHECK((contexts_for(b) & kCopilotKeyContext) != 0);
    CHECK(count_key_conflicts(key_table()) == 0);
    bool tab = false;
    for (const KeyBinding& row : copilot_help_table()) {
        if (row.keys[0].key == Key::Tab) {
            tab = true;
            CHECK(std::string(row.text).find("asiento") != std::string::npos);
        }
    }
    CHECK(tab);
}
