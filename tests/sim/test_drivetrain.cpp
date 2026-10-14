// Tarea 2.6: motor, turbo, embrague, caja y diferenciales (sim/drivetrain.h), y su resolución
// acoplada con las ruedas en el controlador. Criterios 1–11 del prompt de la 2.6 y la tabla de
// regímenes del plan (mensajes/2.6-plan.md).

#include "car_worlds.h"
#include "io/recording.h"
#include "sim/car_controller.h"
#include "sim/car_data.h"
#include "sim/demo_world.h"
#include "sim/drivetrain.h"
#include "sim/jolt_runtime.h"
#include "sim/sim_input.h"
#include "sim/sim_world.h"
#include "sim/tire.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using rally::sim::CarData;
using rally::sim::DrivetrainOutput;
using rally::sim::GearboxMode;
using rally::sim::kAxisMax;
using rally::sim::kStepsPerSecond;
using rally::sim::kWheelCount;
using rally::sim::SimInput;
using rally::sim::SimWorld;
using rally::sim::Surface;
using rally::sim::WorldDesc;
using rally::tests::flat_world;
using rally::tests::reference_car;

constexpr double kPi = 3.14159265358979323846;
constexpr std::array kAllSurfaces = {Surface::Asphalt, Surface::Dirt, Surface::Gravel,
                                     Surface::Snow, Surface::Sand};
constexpr double kRpm = kPi / 30.0; // rad/s por rpm
constexpr double kKmh = 1.0 / 3.6;  // m/s por km/h

const CarData& data_car(const char* name) {
    static std::array<CarData, 4> cars{};
    static std::array<bool, 4> loaded{};
    static const std::array<const char*, 4> names = {"mid_awd_turbo", "front_awd_turbo_5cyl",
                                                     "front_rwd_na", "front_awd_turbo_groupa"};
    for (std::size_t i = 0; i < names.size(); ++i) {
        if (std::string(names[i]) == name) {
            if (!loaded[i]) {
                std::string bytes;
                std::string error;
                const std::string path =
                    std::string(RALLY_SOURCE_DIR "/data/cars/") + name + ".car";
                REQUIRE(rally::io::read_file(path, bytes, error));
                cars[i] = rally::sim::load_car_or_abort(bytes, name, rally::sim::CarKeys::Full);
                loaded[i] = true;
            }
            return cars[i];
        }
    }
    FAIL("coche desconocido");
    return cars[0];
}

SimInput input(double throttle, double brake = 0.0, double steer = 0.0, std::uint16_t buttons = 0) {
    SimInput in;
    in.throttle = static_cast<std::int16_t>(std::clamp(throttle, 0.0, 1.0) * kAxisMax);
    in.brake = static_cast<std::int16_t>(std::clamp(brake, 0.0, 1.0) * kAxisMax);
    in.steer = static_cast<std::int16_t>(std::clamp(steer, -1.0, 1.0) * kAxisMax);
    in.buttons = buttons;
    return in;
}

// Coche de `car` en una losa de `surface` de `length` m, a `speed` m/s, en la marcha `gear`
// (por defecto, la que da la velocidad) y con la caja `mode`.
WorldDesc drive_world(Surface surface, double speed = 0.0,
                      std::int8_t gear = rally::sim::kSpawnGearBySpeed,
                      GearboxMode mode = GearboxMode::Automatic, double length = 600.0,
                      const CarData& car = reference_car()) {
    WorldDesc desc = flat_world(surface, false, static_cast<float>(speed));
    desc.bodies[0] = rally::tests::slab(surface, false, length);
    desc.car_data = car;
    desc.car_spawn.gear = gear;
    desc.aids.gearbox = mode;
    return desc;
}

double rpm(const SimWorld& world) {
    return world.car_controller().drivetrain().engine_omega / kRpm;
}

double speed(const SimWorld& world) {
    const rally::sim::BodyVelocity v = world.car_velocity();
    return std::hypot(static_cast<double>(v.vx), static_cast<double>(v.vz));
}

// Velocidad hacia delante (−z del coche) del centro de masas.
double forward_speed(const SimWorld& world) {
    const rally::sim::BodyPose p = world.car_pose();
    const rally::sim::BodyVelocity v = world.car_velocity();
    // Adelante = rotación · (0, 0, −1).
    const double qx = p.qx;
    const double qy = p.qy;
    const double qz = p.qz;
    const double qw = p.qw;
    const double fx = -(2.0 * (qx * qz + qw * qy));
    const double fz = -(1.0 - 2.0 * (qx * qx + qy * qy));
    return v.vx * fx + v.vz * fz;
}

void run(SimWorld& world, const SimInput& in, int steps) {
    for (int i = 0; i < steps; ++i) {
        world.step(in);
    }
}

// Pulsa un botón un paso y espera a que termine el cambio (si lo hay).
void press(SimWorld& world, std::uint16_t button, const SimInput& hold = SimInput{}) {
    SimInput in = hold;
    in.buttons = static_cast<std::uint16_t>(in.buttons | button);
    world.step(in);
    for (int i = 0; i < world.car_controller().car().shift_steps; ++i) {
        world.step(hold);
    }
}

// Ningún subpaso sin converger del LCP del tren motriz (2.6-respuesta-plan §2).
void check_lcp(const SimWorld& world) { CHECK(world.car_controller().lcp_failures_total() == 0); }

// Interpolación lineal de la tabla, independiente de la del motor.
double table_torque(const CarData& car, double omega) {
    const int n = car.torque_points;
    if (omega <= car.torque_speed[0]) {
        return car.torque_atm[0];
    }
    for (int i = 1; i < n; ++i) {
        const auto k = static_cast<std::size_t>(i);
        if (omega <= car.torque_speed[k]) {
            const double t =
                (omega - car.torque_speed[k - 1]) / (car.torque_speed[k] - car.torque_speed[k - 1]);
            return car.torque_atm[k - 1] + t * (car.torque_atm[k] - car.torque_atm[k - 1]);
        }
    }
    return car.torque_atm[static_cast<std::size_t>(n - 1)];
}

} // namespace

// --- 1. Curva --------------------------------------------------------------------------------

TEST_CASE("drivetrain: at full boost and fixed rpm the torque is the table times 1 plus g p") {
    const CarData& car = reference_car();
    double worst = 0.0;
    for (double r = 800.0; r <= 8200.0; r += 50.0) {
        const double omega = r * kRpm;
        const double expected =
            car.torque_scale * table_torque(car, omega) * (1.0 + car.boost_gain * car.boost_max);
        const double got =
            rally::sim::engine_full_torque(car, static_cast<float>(omega), car.boost_max);
        worst = std::max(worst, std::abs(got - expected) / expected);
        // A fondo y sin limitador, el par neto es el de la curva (el rozamiento es el freno motor
        // con el acelerador cerrado).
        if (r <= 8100.0) {
            const double net =
                rally::sim::engine_torque(car, static_cast<float>(omega), 1.0f, car.boost_max);
            CHECK(net == doctest::Approx(got).epsilon(1e-6));
        }
    }
    MESSAGE("par a plena presión frente a tabla × (1 + g·p): error máximo ", worst * 100.0, " %");
    CHECK(worst <= 0.01);
}

TEST_CASE("drivetrain: mid_awd_turbo peaks at 257 kW within 3 percent between 7500 and 8000 rpm") {
    const CarData& car = data_car("mid_awd_turbo");
    double best = 0.0;
    double best_rpm = 0.0;
    for (double r = 1000.0; r <= 8200.0; r += 10.0) {
        const auto omega = static_cast<float>(r * kRpm);
        const double power = rally::sim::engine_torque(car, omega, 1.0f, car.boost_max) * omega;
        if (power > best) {
            best = power;
            best_rpm = r;
        }
    }
    MESSAGE("potencia máxima ", best / 1000.0, " kW a ", best_rpm, " rpm");
    CHECK(best == doctest::Approx(257000.0).epsilon(0.03));
    CHECK(best_rpm >= 7500.0);
    CHECK(best_rpm <= 8000.0);
}

// --- 2. Turbo --------------------------------------------------------------------------------

TEST_CASE("drivetrain: at 4500 rpm the boost reaches 90 percent in 0.6 to 1.2 s and drops in 0.4") {
    const CarData& car = reference_car();
    const auto omega = static_cast<float>(4500.0 * kRpm);
    const float dt = 1.0f / kStepsPerSecond;
    const float target = rally::sim::boost_target(car, omega, 1.0f);
    float p = 0.0f;
    int rise = 0;
    while (p < 0.9f * target && rise < 10 * kStepsPerSecond) {
        p = rally::sim::turbo_step(car, p, omega, 1.0f, dt);
        ++rise;
    }
    const float released = p;
    int fall = 0;
    while (p >= 0.1f * released && fall < 10 * kStepsPerSecond) {
        p = rally::sim::turbo_step(car, p, omega, 0.0f, dt);
        ++fall;
    }
    // Por debajo del umbral, a fondo y 10 s: sin presión.
    float below = 0.0f;
    for (int s = 0; s < 10 * kStepsPerSecond; ++s) {
        below = rally::sim::turbo_step(car, below, static_cast<float>(3400.0 * kRpm), 1.0f, dt);
    }
    MESSAGE("a 4500 rpm: 90 % de ", target * 1e-5f, " bar en ",
            static_cast<double>(rise) / kStepsPerSecond, " s; al soltar, < 10 % en ",
            static_cast<double>(fall) / kStepsPerSecond, " s; a 3400 rpm a fondo, ", below * 1e-5f,
            " bar");
    CHECK(rise >= static_cast<int>(0.6 * kStepsPerSecond));
    CHECK(rise <= static_cast<int>(1.2 * kStepsPerSecond));
    CHECK(fall <= static_cast<int>(0.4 * kStepsPerSecond));
    CHECK(below < 0.2e5f);
    // Sin presión, entre el 40 y el 50 % del par a plena presión, a las mismas rpm.
    for (double r = 1000.0; r <= 8200.0; r += 500.0) {
        const auto w = static_cast<float>(r * kRpm);
        const double ratio = rally::sim::engine_full_torque(car, w, 0.0f) /
                             rally::sim::engine_full_torque(car, w, car.boost_max);
        CHECK(ratio >= 0.40);
        CHECK(ratio <= 0.50);
    }
}

TEST_CASE("drivetrain: in third at 4500 rpm a throttle step builds the boost with the lag") {
    const rally::sim::JoltRuntime runtime;
    const CarData& car = reference_car();
    // En 3ª a 4500 rpm, en la caja secuencial (sin cambios).
    const double v0 = 4500.0 * kRpm / car.gear_ratio_3 * car.wheel_radius;
    SimWorld world;
    world.build(drive_world(Surface::Asphalt, v0, 3, GearboxMode::Sequential, 2000.0));
    world.seal();
    // Medio segundo sosteniendo la velocidad con poco acelerador: presión ~0.
    run(world, input(0.12), kStepsPerSecond / 2);
    const double start_rpm = rpm(world);
    int steps = 0;
    double target = 0.0;
    double boost = 0.0;
    while (steps < 3 * kStepsPerSecond) {
        world.step(input(1.0));
        ++steps;
        const DrivetrainOutput& d = world.car_controller().drivetrain();
        target = rally::sim::boost_target(car, d.engine_omega, 1.0f);
        boost = d.boost;
        if (boost >= 0.9 * target) {
            break;
        }
    }
    const double rise = static_cast<double>(steps) / kStepsPerSecond;
    // Al soltar, < 10 % de la presión del momento.
    const double released = world.car_controller().drivetrain().boost;
    int fall = 0;
    while (world.car_controller().drivetrain().boost >= 0.1 * released && fall < kStepsPerSecond) {
        world.step(SimInput{});
        ++fall;
    }
    MESSAGE("en 3ª desde ", start_rpm, " rpm: 90 % de la presión objetivo en ", rise, " s (a ",
            rpm(world), " rpm); al soltar, < 10 % en ", static_cast<double>(fall) / kStepsPerSecond,
            " s");
    CHECK(rise >= 0.6);
    CHECK(rise <= 1.2);
    CHECK(fall <= static_cast<int>(0.4 * kStepsPerSecond));
    check_lcp(world);
}

// --- 3. Salida desde parado ------------------------------------------------------------------

namespace {

struct Launch {
    double to_100 = -1.0; // s, −1 si no llega
    double min_rpm = 1e9;
    double max_rpm = 0.0;
};

Launch launch(Surface surface, const CarData& car = reference_car(), double kmh = 100.0) {
    SimWorld world;
    world.build(drive_world(surface, 0.0, rally::sim::kSpawnGearBySpeed, GearboxMode::Automatic,
                            2000.0, car));
    world.seal();
    run(world, SimInput{}, kStepsPerSecond); // se asienta
    Launch out;
    for (int s = 0; s < 20 * kStepsPerSecond; ++s) {
        world.step(input(1.0));
        out.min_rpm = std::min(out.min_rpm, rpm(world));
        out.max_rpm = std::max(out.max_rpm, rpm(world));
        if (speed(world) >= kmh * kKmh) {
            out.to_100 = static_cast<double>(s + 1) / kStepsPerSecond;
            break;
        }
    }
    check_lcp(world);
    return out;
}

} // namespace

TEST_CASE(
    "drivetrain: launching flat out the rpm never drop below idle and 0 to 100 is 2.8 to 4.5 s") {
    const rally::sim::JoltRuntime runtime;
    std::string table;
    for (const Surface surface : {Surface::Asphalt, Surface::Gravel}) {
        const Launch l = launch(surface);
        char row[160];
        std::snprintf(row, sizeof(row), "%s: 0-100 en %.2f s, rpm mínimas %.0f\n",
                      rally::sim::surface_name(surface), l.to_100, l.min_rpm);
        table += row;
        CHECK(l.min_rpm >= 950.0);
        if (surface == Surface::Asphalt) {
            CHECK(l.to_100 >= 2.8);
            CHECK(l.to_100 <= 4.5);
        }
    }
    MESSAGE("salida desde parado a fondo (automático):\n", table);
}

// --- 4. Velocidad punta ----------------------------------------------------------------------

TEST_CASE("drivetrain: top speed on a long flat road is 200 to 230 km/h") {
    const rally::sim::JoltRuntime runtime;
    const CarData& car = reference_car();
    SimWorld world;
    world.build(drive_world(Surface::Asphalt, 0.0, rally::sim::kSpawnGearBySpeed,
                            GearboxMode::Automatic, 6000.0));
    world.seal();
    double top = 0.0;
    double top_rpm = 0.0;
    int gear = 0;
    for (int s = 0; s < 90 * kStepsPerSecond; ++s) {
        world.step(input(1.0));
        if (speed(world) > top) {
            top = speed(world);
            top_rpm = rpm(world);
            gear = world.car_controller().gear();
        }
    }
    // Con el aire sin límite de rpm: potencia = resistencia.
    const double drag_power_at_top =
        0.5 * rally::sim::kAirDensity * car.cda * top * top * top +
        car.tire(Surface::Asphalt).rolling_resistance * car.mass * 9.81 * top;
    const std::string limit = top_rpm >= 8100.0 ? "limitador" : "aire";
    MESSAGE("velocidad punta ", top / kKmh, " km/h en ", gear, "ª a ", top_rpm,
            " rpm; aire y rodadura piden ", drag_power_at_top / 1000.0, " kW: la limita el ",
            limit);
    CHECK(top / kKmh >= 200.0);
    CHECK(top / kKmh <= 230.0);
    check_lcp(world);
}

// --- 5. Automático ---------------------------------------------------------------------------

TEST_CASE("drivetrain: the automatic goes 1 to 5 in order flat out and never shifts down") {
    const rally::sim::JoltRuntime runtime;
    SimWorld world;
    world.build(drive_world(Surface::Asphalt, 0.0, rally::sim::kSpawnGearBySpeed,
                            GearboxMode::Automatic, 4000.0));
    world.seal();
    std::vector<int> gears = {world.car_controller().gear()};
    std::string speeds;
    for (int s = 0; s < 60 * kStepsPerSecond && speed(world) < 180.0 * kKmh; ++s) {
        world.step(input(1.0));
        const int g = world.car_controller().gear();
        if (g != gears.back()) {
            gears.push_back(g);
            speeds += std::to_string(g) + "ª a " +
                      std::to_string(static_cast<int>(speed(world) / kKmh + 0.5)) + " km/h; ";
        }
    }
    MESSAGE("cambios a fondo hasta 180 km/h: ", speeds);
    CHECK(speed(world) >= 180.0 * kKmh);
    REQUIRE(gears.size() == 5);
    for (std::size_t i = 0; i < gears.size(); ++i) {
        CHECK(gears[i] == static_cast<int>(i) + 1);
    }
    check_lcp(world);
}

TEST_CASE("drivetrain: the automatic cruising at 90 km/h for 20 s shifts at most once") {
    const rally::sim::JoltRuntime runtime;
    SimWorld world;
    world.build(drive_world(Surface::Asphalt, 90.0 * kKmh, rally::sim::kSpawnGearBySpeed,
                            GearboxMode::Automatic, 2000.0));
    world.seal();
    int changes = 0;
    int last = world.car_controller().gear();
    double throttle_sum = 0.0;
    for (int s = 0; s < 20 * kStepsPerSecond; ++s) {
        // Acelerador que sostiene 90 km/h (proporcional, alrededor de lo que pide el crucero).
        const double throttle = std::clamp(0.15 + 0.5 * (90.0 * kKmh - speed(world)), 0.0, 1.0);
        throttle_sum += throttle;
        world.step(input(throttle));
        if (world.car_controller().gear() != last) {
            ++changes;
            last = world.car_controller().gear();
        }
    }
    MESSAGE("crucero a 90 km/h 20 s: ", changes, " cambios, en ", last, "ª a ", rpm(world),
            " rpm, acelerador medio ", throttle_sum / (20.0 * kStepsPerSecond));
    CHECK(changes <= 1);
    // Y con medio acelerador fijo desde 90 km/h, sin cazar marchas: solo sube.
    SimWorld half;
    half.build(drive_world(Surface::Asphalt, 90.0 * kKmh, rally::sim::kSpawnGearBySpeed,
                           GearboxMode::Automatic, 4000.0));
    half.seal();
    int down = 0;
    last = half.car_controller().gear();
    for (int s = 0; s < 20 * kStepsPerSecond; ++s) {
        half.step(input(0.5));
        const int g = half.car_controller().gear();
        down += g < last ? 1 : 0;
        last = g;
    }
    MESSAGE("medio acelerador fijo 20 s desde 90 km/h: ", half.car_controller().gear(), "ª a ",
            speed(half) / kKmh, " km/h, ", down, " reducciones");
    CHECK(down == 0);
    check_lcp(world);
    check_lcp(half);
}

TEST_CASE("drivetrain: in a drift the automatic holds the upshift at most 0.75 s and never sits "
          "on the limiter") {
    const rally::sim::JoltRuntime runtime;
    const CarData& car = reference_car();
    // En grava, en 2ª a 65 km/h, con el 60 % de acelerador y la dirección a fondo sin ayuda: la
    // trasera sale y el coche derrapa con las rpm por encima de las de subir (7280 con ese
    // acelerador) y por debajo de la espera máxima (8000).
    WorldDesc desc = drive_world(Surface::Gravel, 65.0 * kKmh, 2, GearboxMode::Automatic);
    desc.bodies[0].half_x = 400.0f;
    desc.bodies[0].half_z = 400.0f;
    desc.bodies[0].z = 0.0;
    SimWorld world;
    world.build(desc);
    world.seal();
    world.set_telemetry(true);
    int hold = 0;
    int max_hold = 0;
    int at_limit = 0;
    int max_at_limit = 0;
    int max_beta_steps = 0;
    bool held = false;
    bool shifted_in_drift = false;
    double max_beta = 0.0;
    for (int s = 0; s < 6 * kStepsPerSecond; ++s) {
        world.step(input(0.6, 0.0, 1.0));
        const rally::sim::CarTelemetry& t = world.telemetry();
        const double beta = std::abs(t.beta) * 180.0 / kPi;
        max_beta = std::max(max_beta, beta);
        if (beta > 10.0) {
            ++max_beta_steps;
        }
        if (t.drift_hold) {
            held = true;
            ++hold;
            max_hold = std::max(max_hold, hold);
        } else {
            if (hold > 0 && t.shifting && beta > 10.0) {
                shifted_in_drift = true;
            }
            hold = 0;
        }
        at_limit = rpm(world) >= 8100.0 ? at_limit + 1 : 0;
        max_at_limit = std::max(max_at_limit, at_limit);
    }
    MESSAGE("derrape en grava: |β| máx. ", max_beta, "°, ", max_beta_steps,
            " pasos con |β| > 10°; subida retrasada ",
            static_cast<double>(max_hold) / kStepsPerSecond, " s como mucho (",
            shifted_in_drift ? "cambia en el derrape al acabar la espera" : "",
            "); en el limitador ", static_cast<double>(max_at_limit) / kStepsPerSecond,
            " s seguidos como mucho");
    REQUIRE(held);                           // el guion tiene que provocar la espera
    CHECK(max_hold == car.drift_hold_steps); // llega a la espera completa y cambia
    CHECK(shifted_in_drift);
    CHECK(max_at_limit <= kStepsPerSecond / 10);
    check_lcp(world);
}

TEST_CASE("drivetrain: the automatic holds an upshift in a drift for the file time, not on the "
          "limiter") {
    const CarData& car = reference_car();
    rally::sim::DrivetrainState state;
    rally::sim::drivetrain_reset(car, state);
    state.gear = 2;
    state.target_gear = 2;
    rally::sim::GearboxInput in;
    in.throttle = 1.0f;
    in.drifting = true;
    // 7900 rpm en 2ª (por encima de subir, 7800, y por debajo de la espera máxima, 8000).
    in.drive_speed = static_cast<float>(7900.0 * kRpm) / car.gear_ratio_2;
    state.engine_omega = static_cast<float>(7900.0 * kRpm);
    int held = 0;
    while (state.target_gear == 2 && held < 10 * kStepsPerSecond) {
        rally::sim::update_gearbox(car, in, state);
        held += state.target_gear == 2 ? 1 : 0;
    }
    MESSAGE("espera en derrape a 7900 rpm: ", held, " pasos (", car.drift_hold_steps,
            " del archivo)");
    CHECK(held == car.drift_hold_steps);
    // Cerca del limitador (8050 rpm) no espera.
    rally::sim::drivetrain_reset(car, state);
    state.gear = 2;
    state.target_gear = 2;
    in.drive_speed = static_cast<float>(8050.0 * kRpm) / car.gear_ratio_2;
    state.engine_omega = static_cast<float>(8050.0 * kRpm);
    rally::sim::update_gearbox(car, in, state);
    CHECK(state.target_gear == 3);
}

// --- 6. Secuencial ---------------------------------------------------------------------------

TEST_CASE("drivetrain: a sequential shift lasts the file time and an over-revving downshift is "
          "rejected") {
    const rally::sim::JoltRuntime runtime;
    const CarData& car = reference_car();
    SimWorld world;
    world.build(drive_world(Surface::Asphalt, 60.0 * kKmh, 2, GearboxMode::Sequential, 2000.0));
    world.seal();
    run(world, input(0.3), kStepsPerSecond / 2);
    SimInput up = input(0.3, 0.0, 0.0, rally::sim::kButtonShiftUp);
    world.step(up);
    int steps = 1;
    while (world.car_controller().gear() == 2 && steps < kStepsPerSecond) {
        CHECK(world.car_controller().drivetrain().shifting);
        world.step(input(0.3));
        ++steps;
    }
    const int expected = static_cast<int>(car.shift_time * kStepsPerSecond + 0.5);
    MESSAGE("cambio de 2ª a 3ª: ", steps, " pasos (archivo: ", car.shift_time, " s = ", expected,
            " pasos)");
    CHECK(world.car_controller().gear() == 3);
    CHECK(std::abs(steps - expected) <= 1);

    // En 3ª a 7500 rpm, bajar a 2ª pasaría de 10 000 rpm: se rechaza.
    SimWorld fast;
    const double v = 7500.0 * kRpm / car.gear_ratio_3 * car.wheel_radius;
    fast.build(drive_world(Surface::Asphalt, v, 3, GearboxMode::Sequential, 2000.0));
    fast.seal();
    press(fast, rally::sim::kButtonShiftDown, input(0.3));
    MESSAGE("reducción a 2ª en 3ª a ", rpm(fast), " rpm: queda en ", fast.car_controller().gear(),
            "ª");
    CHECK(fast.car_controller().gear() == 3);
    check_lcp(world);
    check_lcp(fast);
}

// --- 7. Punto muerto y marcha atrás (D4) -----------------------------------------------------

TEST_CASE("drivetrain: from first at rest both modes go N, R and back, and reverse drives back") {
    const rally::sim::JoltRuntime runtime;
    for (const GearboxMode mode : {GearboxMode::Automatic, GearboxMode::Sequential}) {
        INFO("modo ", mode == GearboxMode::Automatic ? "automático" : "secuencial");
        SimWorld world;
        world.build(drive_world(Surface::Asphalt, 0.0, rally::sim::kSpawnGearBySpeed, mode));
        world.seal();
        run(world, SimInput{}, kStepsPerSecond / 2);
        REQUIRE(world.car_controller().gear() == 1);
        press(world, rally::sim::kButtonShiftDown);
        CHECK(world.car_controller().gear() == rally::sim::kNeutralGear);
        press(world, rally::sim::kButtonShiftDown);
        CHECK(world.car_controller().gear() == rally::sim::kReverseGear);
        // En R, el acelerador lleva el coche hacia atrás.
        run(world, input(0.6), 2 * kStepsPerSecond);
        CHECK(forward_speed(world) < -2.0);
        run(world, input(0.0, 1.0), 3 * kStepsPerSecond); // parado
        press(world, rally::sim::kButtonShiftUp);
        CHECK(world.car_controller().gear() == rally::sim::kNeutralGear);
        press(world, rally::sim::kButtonShiftUp);
        CHECK(world.car_controller().gear() == 1);
        check_lcp(world);
    }
}

TEST_CASE("drivetrain: in neutral flat out the engine reaches the limiter and the car gets no "
          "torque") {
    const rally::sim::JoltRuntime runtime;
    SimWorld world;
    world.build(drive_world(Surface::Asphalt, 0.0, rally::sim::kNeutralGear));
    world.seal();
    run(world, SimInput{}, kStepsPerSecond / 2);
    const rally::sim::BodyPose start = world.car_pose();
    double max_rpm = 0.0;
    double max_wheel_torque = 0.0;
    for (int s = 0; s < 3 * kStepsPerSecond; ++s) {
        world.step(input(1.0));
        max_rpm = std::max(max_rpm, rpm(world));
        const DrivetrainOutput& d = world.car_controller().drivetrain();
        max_wheel_torque =
            std::max({max_wheel_torque, std::abs(static_cast<double>(d.axle_torque[0])),
                      std::abs(static_cast<double>(d.axle_torque[1]))});
    }
    const rally::sim::BodyPose end = world.car_pose();
    const double moved = std::hypot(end.x - start.x, end.z - start.z);
    MESSAGE("en N a fondo: rpm máximas ", max_rpm, ", par a los ejes ", max_wheel_torque,
            " N·m, desplazamiento ", moved * 1000.0, " mm");
    CHECK(max_rpm >= 8100.0);
    CHECK(max_rpm <= 8200.0);
    CHECK(max_wheel_torque == 0.0);
    CHECK(moved < 0.001);
    check_lcp(world);
}

TEST_CASE("drivetrain: rolling in neutral at 2 km/h or more, shifting down does not give reverse") {
    const rally::sim::JoltRuntime runtime;
    SimWorld world;
    world.build(drive_world(Surface::Asphalt, 5.0 * kKmh, rally::sim::kNeutralGear));
    world.seal();
    run(world, SimInput{}, 2);
    REQUIRE(speed(world) >= 2.0 * kKmh);
    press(world, rally::sim::kButtonShiftDown);
    CHECK(world.car_controller().gear() == rally::sim::kNeutralGear);
    check_lcp(world);
}

TEST_CASE("drivetrain: moving, sequential first goes to N and back only below the limiter; "
          "automatic ignores down") {
    const rally::sim::JoltRuntime runtime;
    const CarData& car = reference_car();
    {
        // Secuencial, en 1ª a 40 km/h: bajar da N; subir vuelve a 1ª (5000 rpm).
        SimWorld world;
        world.build(drive_world(Surface::Asphalt, 40.0 * kKmh, 1, GearboxMode::Sequential));
        world.seal();
        press(world, rally::sim::kButtonShiftDown);
        CHECK(world.car_controller().gear() == rally::sim::kNeutralGear);
        press(world, rally::sim::kButtonShiftUp);
        CHECK(world.car_controller().gear() == 1);
        check_lcp(world);
    }
    {
        // Secuencial, en N a 80 km/h: 1ª daría ~10 000 rpm; se rechaza.
        SimWorld world;
        world.build(drive_world(Surface::Asphalt, 80.0 * kKmh, rally::sim::kNeutralGear,
                                GearboxMode::Sequential));
        world.seal();
        press(world, rally::sim::kButtonShiftUp);
        CHECK(world.car_controller().gear() == rally::sim::kNeutralGear);
        check_lcp(world);
    }
    {
        // Automático, en marcha (3ª a 60 km/h, ~4900 rpm: sin cambio propio): bajar no hace nada.
        SimWorld world;
        world.build(drive_world(Surface::Asphalt, 60.0 * kKmh, 3));
        world.seal();
        const int gear = world.car_controller().gear();
        press(world, rally::sim::kButtonShiftDown, input(0.2));
        CHECK(world.car_controller().gear() == gear);
        CHECK_FALSE(world.car_controller().drivetrain().shifting);
        check_lcp(world);
    }
    (void)car;
}

TEST_CASE("drivetrain: after building or resetting the car is in first at idle with no boost") {
    const rally::sim::JoltRuntime runtime;
    SimWorld world;
    world.build(rally::sim::zone_world(Surface::Gravel, reference_car()));
    world.seal();
    CHECK(world.car_controller().gear() == 1);
    run(world, input(1.0), 6 * kStepsPerSecond);
    REQUIRE(world.car_controller().gear() > 1);
    REQUIRE(world.car_controller().drivetrain().boost > 0.0f);
    world.step(input(0.0, 0.0, 0.0, rally::sim::kButtonReset));
    const DrivetrainOutput& d = world.car_controller().drivetrain();
    CHECK(world.car_controller().gear() == 1);
    CHECK(d.engine_omega == doctest::Approx(reference_car().idle_speed).epsilon(0.02));
    CHECK(d.boost == 0.0f);
    CHECK_FALSE(d.shifting);
    check_lcp(world);
}

// --- 8. Central y autoblocante ---------------------------------------------------------------

TEST_CASE("drivetrain: with the front on asphalt and the rear on snow the front gets more than "
          "33 percent") {
    const rally::sim::JoltRuntime runtime;
    const CarData& car = reference_car();
    // Asfalto delante (z < −0,4) y nieve detrás, en losas que se solapan 0,5 m (de −0,65 a
    // −0,15): las delanteras (z = −1,27) quedan a 0,6 m del solape; las traseras (z = +1,27)
    // llegan a él tras ~1,4 m.
    WorldDesc desc;
    desc.car = true;
    desc.car_data = car;
    rally::sim::BoxDesc asphalt = rally::tests::slab(Surface::Asphalt, false, 600.0);
    asphalt.z = -0.15 - 300.0;
    rally::sim::BoxDesc snow = rally::tests::slab(Surface::Snow, false, 40.0);
    snow.z = -0.65 + 20.0;
    desc.bodies = {asphalt, snow};
    SimWorld world;
    world.build(desc);
    world.seal();
    run(world, SimInput{}, kStepsPerSecond / 2);
    REQUIRE(world.car_controller().wheel(0).surface == Surface::Asphalt);
    REQUIRE(world.car_controller().wheel(2).surface == Surface::Snow);
    const double start_z = world.car_pose().z;
    double front_sum = 0.0;
    double total_sum = 0.0;
    double max_dw = 0.0;
    int measured = 0;
    while (world.car_pose().z > start_z - 1.3 && measured < 4 * kStepsPerSecond) {
        world.step(input(1.0));
        const DrivetrainOutput& d = world.car_controller().drivetrain();
        REQUIRE(world.car_controller().wheel(2).surface == Surface::Snow);
        max_dw = std::max(max_dw, std::abs(static_cast<double>(d.center_slip)));
        // Desde que el embrague pasa par de verdad (el primer cuarto de segundo, no).
        if (measured >= kStepsPerSecond / 4) {
            front_sum += d.axle_torque[0];
            total_sum += d.axle_torque[0] + d.axle_torque[1];
        }
        ++measured;
    }
    const double share = front_sum / total_sum;
    MESSAGE("delanteras en asfalto y traseras en nieve, a fondo desde parado (",
            static_cast<double>(measured) / kStepsPerSecond,
            " s hasta el solape): par al eje "
            "delantero ",
            share * 100.0, " % (reparto 33 %), |Δω| del central máx. ", max_dw, " rad/s");
    CHECK(share > 0.33 * 1.2); // ≥ 20 % más que el reparto (valor de partida del viscoacoplador)
    CHECK(max_dw < 50.0);
    check_lcp(world);
}

TEST_CASE("drivetrain: with a front wheel in the air the other one gets at least the preload") {
    const rally::sim::JoltRuntime runtime;
    const CarData& car = reference_car();
    // Suelo de asfalto con un hueco de 1 × 1 m bajo la delantera izquierda (x = −0,715,
    // z = −1,27): tres losas alrededor.
    WorldDesc desc;
    desc.car = true;
    desc.car_data = car;
    const auto box = [](double x0, double x1, double z0, double z1) {
        rally::sim::BoxDesc b;
        b.x = 0.5 * (x0 + x1);
        b.z = 0.5 * (z0 + z1);
        b.y = -0.5;
        b.half_x = static_cast<float>(0.5 * (x1 - x0));
        b.half_y = 0.5f;
        b.half_z = static_cast<float>(0.5 * (z1 - z0));
        b.surface = Surface::Asphalt;
        return b;
    };
    desc.bodies = {box(-20.0, 20.0, -0.77, 20.0), box(-20.0, -1.215, -40.0, -0.77),
                   box(-0.215, 20.0, -40.0, -0.77), box(-1.215, -0.215, -40.0, -1.77)};
    SimWorld world;
    world.build(desc);
    world.seal();
    run(world, SimInput{}, kStepsPerSecond);
    REQUIRE_FALSE(world.car_controller().wheel(rally::sim::kFrontLeft).contact);
    REQUIRE(world.car_controller().wheel(rally::sim::kFrontRight).contact);
    // Desde que el eje lleva más del doble de la precarga, la derecha recibe al menos la
    // precarga, pegada (las dos giran igual) o deslizando (la izquierda gira más deprisa).
    double min_fr_torque = 1e9;
    double min_lock_slipping = 1e9;
    int measured = 0;
    int slipping = 0;
    for (int s = 0; s < kStepsPerSecond; ++s) {
        world.step(input(1.0));
        const rally::sim::CarController& c = world.car_controller();
        if (c.wheel(rally::sim::kFrontLeft).contact) {
            break;
        }
        const DrivetrainOutput& d = c.drivetrain();
        if (d.axle_torque[0] < 2.0f * car.lsd_front_preload) {
            continue;
        }
        ++measured;
        min_fr_torque =
            std::min(min_fr_torque,
                     static_cast<double>(c.wheel(rally::sim::kFrontRight).fx) * car.wheel_radius);
        if (c.wheel(rally::sim::kFrontLeft).omega > c.wheel(rally::sim::kFrontRight).omega + 0.5f) {
            ++slipping;
            min_lock_slipping = std::min(min_lock_slipping, static_cast<double>(d.lsd_torque[0]));
        }
    }
    MESSAGE("delantera izquierda en el aire: ", measured,
            " pasos con el eje por encima del doble "
            "de la precarga (",
            slipping, " con el autoblocante deslizando, par de bloqueo mínimo ", min_lock_slipping,
            " N·m); par de la derecha al suelo mínimo ", min_fr_torque, " N·m (precarga ",
            car.lsd_front_preload, ")");
    REQUIRE(measured > 0);
    CHECK(min_fr_torque >= car.lsd_front_preload);
    if (slipping > 0) {
        CHECK(min_lock_slipping >= car.lsd_front_preload * 0.999);
    }
    check_lcp(world);
}

// --- 9. Freno de mano en 4×4 -----------------------------------------------------------------

TEST_CASE("drivetrain: the handbrake at 60 km/h on gravel locks the rear, the front rolls, and "
          "release does not ring") {
    const rally::sim::JoltRuntime runtime;
    const CarData& car = reference_car();
    SimWorld world;
    world.build(drive_world(Surface::Gravel, 60.0 * kKmh));
    world.seal();
    run(world, input(0.2), kStepsPerSecond / 4);
    int locked_at = -1;
    double min_front_ratio = 1e9;
    const SimInput pull = input(0.0, 0.0, 0.0, rally::sim::kButtonHandbrake);
    for (int s = 0; s < kStepsPerSecond / 2; ++s) {
        world.step(pull);
        const rally::sim::CarController& c = world.car_controller();
        const double v = speed(world);
        const double rolling = v / car.wheel_radius;
        const bool rear_locked = c.wheel(rally::sim::kRearLeft).omega < 0.05 * rolling &&
                                 c.wheel(rally::sim::kRearRight).omega < 0.05 * rolling;
        if (rear_locked && locked_at < 0) {
            locked_at = s + 1;
        }
        min_front_ratio =
            std::min({min_front_ratio, c.wheel(0).omega / rolling, c.wheel(1).omega / rolling});
    }
    // Al soltar: Δω del central sin oscilar (como mucho 2 cambios de signo en 0,5 s).
    int changes = 0;
    int last = 0;
    for (int s = 0; s < kStepsPerSecond / 2; ++s) {
        world.step(input(0.2));
        const float dw = world.car_controller().drivetrain().center_slip;
        const int sign = dw > 1e-3f ? 1 : (dw < -1e-3f ? -1 : 0);
        if (sign != 0) {
            if (last != 0 && sign != last) {
                ++changes;
            }
            last = sign;
        }
    }
    MESSAGE("freno de mano en grava a 60 km/h: traseras bloqueadas en ",
            static_cast<double>(locked_at) / kStepsPerSecond, " s; delanteras, ω/(v/r) mínimo ",
            min_front_ratio, "; al soltar, ", changes, " cambios de signo de Δω en 0,5 s");
    REQUIRE(locked_at > 0);
    CHECK(locked_at <= kStepsPerSecond / 5);
    CHECK(min_front_ratio > 0.8);
    CHECK(changes <= 2);
    check_lcp(world);
}

// --- 10. Estabilidad -------------------------------------------------------------------------

namespace {

struct Hold {
    int rpm_changes = 0;    // cambios de signo de ΔRPM entre pasos (|Δ| ≥ 0,5 rpm)
    double rpm_resid = 0.0; // desviación típica respecto a la media móvil de 5 pasos, rpm
    int slip_changes = 0;   // cambios de signo de ω·r − v, máximo de las ruedas
};

// Mide el último medio segundo de un tramo de `steps` pasos con la entrada `in`.
Hold hold_segment(SimWorld& world, const SimInput& in, int steps) {
    const int window = kStepsPerSecond / 2;
    std::vector<double> rpms;
    std::array<int, kWheelCount> last{};
    std::array<int, kWheelCount> changes{};
    for (int s = 0; s < steps; ++s) {
        world.step(in);
        if (s < steps - window) {
            continue;
        }
        rpms.push_back(rpm(world));
        for (int i = 0; i < kWheelCount; ++i) {
            const auto k = static_cast<std::size_t>(i);
            const rally::sim::WheelOutput& w = world.car_controller().wheel(i);
            if (w.locked) {
                last[k] = 0;
                continue;
            }
            const int sign = w.slip_x > 0.0f ? 1 : (w.slip_x < 0.0f ? -1 : 0);
            if (sign != 0) {
                if (last[k] != 0 && sign != last[k]) {
                    ++changes[k];
                }
                last[k] = sign;
            }
        }
    }
    Hold out;
    int last_sign = 0;
    for (std::size_t i = 1; i < rpms.size(); ++i) {
        const double d = rpms[i] - rpms[i - 1];
        const int sign = d >= 0.5 ? 1 : (d <= -0.5 ? -1 : 0);
        if (sign != 0) {
            if (last_sign != 0 && sign != last_sign) {
                ++out.rpm_changes;
            }
            last_sign = sign;
        }
    }
    // Residuo respecto a la media móvil centrada de 5 pasos (~21 ms): recoge el temblor de paso
    // a paso y no la tendencia (la subida al limitador o la de la presión, que dobla la curva
    // dentro de la ventana).
    double sq = 0.0;
    int count = 0;
    for (std::size_t i = 2; i + 2 < rpms.size(); ++i) {
        const double mean = (rpms[i - 2] + rpms[i - 1] + rpms[i] + rpms[i + 1] + rpms[i + 2]) / 5.0;
        const double r = rpms[i] - mean;
        sq += r * r;
        ++count;
    }
    const double n = static_cast<double>(count);
    out.rpm_resid = std::sqrt(sq / n);
    out.slip_changes = *std::max_element(changes.begin(), changes.end());
    return out;
}

} // namespace

TEST_CASE("drivetrain: in every gear at three speeds and in neutral the rpm do not oscillate") {
    const rally::sim::JoltRuntime runtime;
    const CarData& car = reference_car();
    std::string table = "marcha | rpm de salida | acelerador 0 → 1 → 0: cambios ΔRPM, σ (rpm), "
                        "cambios ω·r − v\n";
    int worst_changes = 0;
    double worst_resid = 0.0;
    int worst_slip = 0;
    for (int gear = 0; gear <= rally::sim::kGearCount; ++gear) {
        for (const double start : {2500.0, 5000.0, 7500.0}) {
            // En N, las mismas velocidades que en 3ª.
            const double ratio = rally::sim::gear_ratio(car, gear == 0 ? 3 : gear);
            const double v = start * kRpm / ratio * car.wheel_radius;
            SimWorld world;
            world.build(drive_world(Surface::Asphalt, v, static_cast<std::int8_t>(gear),
                                    GearboxMode::Sequential, 4000.0));
            world.seal();
            std::string row = std::to_string(gear) + " | " + std::to_string(int(start)) + " |";
            for (const double throttle : {0.0, 1.0, 0.0}) {
                const Hold h = hold_segment(world, input(throttle), kStepsPerSecond);
                char cell[80];
                std::snprintf(cell, sizeof(cell), " %d, %.2f, %d;", h.rpm_changes, h.rpm_resid,
                              h.slip_changes);
                row += cell;
                worst_changes = std::max(worst_changes, h.rpm_changes);
                worst_resid = std::max(worst_resid, h.rpm_resid);
                worst_slip = std::max(worst_slip, h.slip_changes);
            }
            table += row + "\n";
            check_lcp(world);
        }
    }
    MESSAGE("estabilidad del tren motriz (último 0,5 s de cada tramo de 1 s):\n", table,
            "peor: ", worst_changes, " cambios de ΔRPM, σ ", worst_resid, " rpm, ", worst_slip,
            " cambios de ω·r − v");
    CHECK(worst_changes <= 2);
    CHECK(worst_resid <= 10.0);
    CHECK(worst_slip <= 2);
}

// --- 11. Configuraciones ---------------------------------------------------------------------

TEST_CASE("drivetrain: the four configurations load and their 0 to 100 and drive layout") {
    const rally::sim::JoltRuntime runtime;
    std::string table = "configuración | masa | reparto de peso | potencia máx. | tracción | "
                        "0-100 asfalto\n";
    for (const char* name :
         {"mid_awd_turbo", "front_awd_turbo_5cyl", "front_rwd_na", "front_awd_turbo_groupa"}) {
        const CarData& car = data_car(name);
        double best = 0.0;
        for (double r = 1000.0; r <= car.limiter_speed / kRpm; r += 10.0) {
            const auto omega = static_cast<float>(r * kRpm);
            best = std::max(best, static_cast<double>(
                                      rally::sim::engine_torque(car, omega, 1.0f, car.boost_max)) *
                                      omega);
        }
        const Launch l = launch(Surface::Asphalt, car);
        char row[200];
        std::snprintf(row, sizeof(row),
                      "%s | %.0f kg | %.0f/%.0f | %.0f kW (%.0f CV) | %s | %.2f s\n", name,
                      car.mass, car.front_weight * 100.0, 100.0 - car.front_weight * 100.0,
                      best / 1000.0, best / 735.5,
                      car.drive_front_share == 0.0f
                          ? "propulsión"
                          : ("4x4 " + std::to_string(int(car.drive_front_share * 100.0 + 0.5)) +
                             "/" + std::to_string(int(100.5 - car.drive_front_share * 100.0)))
                                .c_str(),
                      l.to_100);
        table += row;
        CHECK(l.to_100 > 0.0);
        CHECK(l.min_rpm >= 950.0);
    }
    MESSAGE("las cuatro configuraciones:\n", table);
}

TEST_CASE("drivetrain: front_rwd_na sends nothing to the front axle") {
    const rally::sim::JoltRuntime runtime;
    SimWorld world;
    world.build(drive_world(Surface::Gravel, 0.0, rally::sim::kSpawnGearBySpeed,
                            GearboxMode::Automatic, 600.0, data_car("front_rwd_na")));
    world.seal();
    double front = 0.0;
    double rear = 0.0;
    for (int s = 0; s < 3 * kStepsPerSecond; ++s) {
        world.step(input(1.0, 0.0, s > kStepsPerSecond ? 0.5 : 0.0));
        const DrivetrainOutput& d = world.car_controller().drivetrain();
        front = std::max(front, std::abs(static_cast<double>(d.axle_torque[0])));
        rear = std::max(rear, std::abs(static_cast<double>(d.axle_torque[1])));
    }
    MESSAGE("front_rwd_na: par máximo delantero ", front, " N·m, trasero ", rear, " N·m");
    CHECK(front == 0.0);
    CHECK(rear > 500.0);
    check_lcp(world);
}

TEST_CASE("drivetrain: mid_awd_turbo without slip sends 67 percent to the rear") {
    const rally::sim::JoltRuntime runtime;
    SimWorld world;
    world.build(drive_world(Surface::Asphalt, 70.0 * kKmh, rally::sim::kSpawnGearBySpeed,
                            GearboxMode::Automatic, 2000.0, data_car("mid_awd_turbo")));
    world.seal();
    run(world, input(0.25), kStepsPerSecond);
    double front = 0.0;
    double total = 0.0;
    double max_dw = 0.0;
    for (int s = 0; s < kStepsPerSecond; ++s) {
        world.step(input(0.25));
        const DrivetrainOutput& d = world.car_controller().drivetrain();
        front += d.axle_torque[0];
        total += d.axle_torque[0] + d.axle_torque[1];
        max_dw = std::max(max_dw, std::abs(static_cast<double>(d.center_slip)));
    }
    const double rear_share = 1.0 - front / total;
    MESSAGE("mid_awd_turbo a 70 km/h con 25 % de acelerador: trasero / total ", rear_share,
            ", |Δω| máx. ", max_dw, " rad/s");
    CHECK(rear_share == doctest::Approx(0.67).epsilon(0.02 / 0.67));
    check_lcp(world);
}

TEST_CASE("drivetrain: a rear-drive car with a viscous coupling or a front diff fails to load") {
    std::string text = rally::tests::reference_car_text();
    const auto set = [&](const std::string& from, const std::string& to) {
        std::string t = text;
        const std::size_t at = t.find(from);
        REQUIRE(at != std::string::npos);
        t.replace(at, from.size(), to);
        return t;
    };
    const rally::sim::CarParseResult rwd_viscous = rally::sim::parse_car(
        set("front_share = 0.33        # reparto 33/67", "front_share = 0 # reparto"),
        rally::sim::CarKeys::Sim);
    CHECK_FALSE(rwd_viscous.ok);
    MESSAGE("propulsión con viscoacoplador: ", rwd_viscous.error);
    // Histéresis del automático imposible: bajar por encima de subir.
    const rally::sim::CarParseResult hunt = rally::sim::parse_car(
        set("downshift_rpm_full = 4200", "downshift_rpm_full = 7000"), rally::sim::CarKeys::Sim);
    CHECK_FALSE(hunt.ok);
    MESSAGE("histéresis: ", hunt.error);
    // Curva de par con rpm que no crecen.
    const rally::sim::CarParseResult curve =
        rally::sim::parse_car(set("3000 = 175.5", "1500 = 175.5"), rally::sim::CarKeys::Sim);
    CHECK_FALSE(curve.ok);
    MESSAGE("curva: ", curve.error);
    CHECK(curve.error.rfind("línea ", 0) == 0);
}

// --- Regímenes del plan ----------------------------------------------------------------------

TEST_CASE("drivetrain: at rest in first with no input the engine idles and the car stays put") {
    const rally::sim::JoltRuntime runtime;
    SimWorld world;
    world.build(drive_world(Surface::Asphalt));
    world.seal();
    run(world, SimInput{}, kStepsPerSecond);
    const rally::sim::BodyPose start = world.car_pose();
    double lo = 1e9;
    double hi = 0.0;
    for (int s = 0; s < 10 * kStepsPerSecond; ++s) {
        world.step(SimInput{});
        lo = std::min(lo, rpm(world));
        hi = std::max(hi, rpm(world));
    }
    const rally::sim::BodyPose end = world.car_pose();
    const double moved = std::hypot(end.x - start.x, end.z - start.z);
    MESSAGE("parado en 1ª 10 s: rpm entre ", lo, " y ", hi, "; desplazamiento ", moved * 1000.0,
            " mm");
    CHECK(lo >= 980.0);
    CHECK(hi <= 1020.0);
    CHECK(moved < 0.001);
    CHECK(world.car_controller().gear() == 1);
    check_lcp(world);
}

TEST_CASE("drivetrain: a 20 percent throttle start never stalls on flat ground or the 15 percent "
          "ramp") {
    const rally::sim::JoltRuntime runtime;
    std::string table;
    for (const Surface surface : kAllSurfaces) {
        for (const bool ramp : {false, true}) {
            WorldDesc desc = ramp ? rally::tests::ramp_world(rally::tests::kRampGrade, surface)
                                  : drive_world(surface);
            if (ramp) {
                desc.car_spawn.y = 0.25;
            }
            SimWorld world;
            world.build(desc);
            world.seal();
            run(world, input(0.0, 1.0), 3 * kStepsPerSecond);
            const rally::sim::BodyPose start = world.car_pose();
            double lo = 1e9;
            for (int s = 0; s < 4 * kStepsPerSecond; ++s) {
                world.step(input(0.2));
                lo = std::min(lo, rpm(world));
            }
            const rally::sim::BodyPose end = world.car_pose();
            const double moved = std::hypot(end.x - start.x, end.z - start.z);
            char row[120];
            std::snprintf(row, sizeof(row), "%s %s: rpm mínimas %.0f, %.1f m en 4 s\n",
                          rally::sim::surface_name(surface), ramp ? "rampa" : "llano", lo, moved);
            table += row;
            CHECK(lo >= 950.0);
            check_lcp(world);
        }
    }
    MESSAGE("arranque con el 20 % de acelerador:\n", table);
}

TEST_CASE("drivetrain: full braking from 100 km/h in third never takes the rpm below idle") {
    const rally::sim::JoltRuntime runtime;
    for (const Surface surface : {Surface::Asphalt, Surface::Gravel}) {
        SimWorld world;
        world.build(drive_world(surface, 100.0 * kKmh, 3));
        world.seal();
        double lo = 1e9;
        for (int s = 0; s < 8 * kStepsPerSecond; ++s) {
            world.step(input(0.0, 1.0));
            lo = std::min(lo, rpm(world));
        }
        MESSAGE(rally::sim::surface_name(surface),
                ": frenada a fondo desde 100 km/h en 3ª: rpm "
                "mínimas ",
                lo, "; al final ", speed(world), " m/s en ", world.car_controller().gear(), "ª");
        CHECK(lo >= 950.0);
        CHECK(speed(world) < 0.01);
        check_lcp(world);
    }
}

TEST_CASE("drivetrain: lifting off in third at 100 km/h brakes with the engine without locking "
          "the rear") {
    const rally::sim::JoltRuntime runtime;
    const CarData& car = reference_car();
    const auto decel = [&](bool neutral) {
        SimWorld world;
        world.build(drive_world(Surface::Asphalt, 100.0 * kKmh,
                                neutral ? rally::sim::kNeutralGear : std::int8_t{3},
                                GearboxMode::Sequential));
        world.seal();
        const double v0 = speed(world);
        double worst_kappa = 0.0;
        for (int s = 0; s < kStepsPerSecond; ++s) {
            world.step(SimInput{});
            for (const int i : {rally::sim::kRearLeft, rally::sim::kRearRight}) {
                worst_kappa = std::min(worst_kappa,
                                       static_cast<double>(world.car_controller().wheel(i).kappa));
            }
        }
        check_lcp(world);
        return std::pair{v0 - speed(world), worst_kappa};
    };
    const auto [engine, kappa] = decel(false);
    const auto [coast, coast_kappa] = decel(true);
    MESSAGE("1 s sin acelerador desde 100 km/h: en 3ª ", engine, " m/s menos (κ_r trasero mínimo ",
            kappa, "); en N ", coast, " m/s menos");
    CHECK(engine > coast * 1.2);
    CHECK(kappa > -car.tire(Surface::Asphalt).slip_ratio_peak);
    (void)coast_kappa;
}

TEST_CASE("drivetrain: a sequential downshift from fourth to third at 100 km/h blips and keeps "
          "the rear rolling") {
    const rally::sim::JoltRuntime runtime;
    const CarData& car = reference_car();
    SimWorld world;
    world.build(drive_world(Surface::Asphalt, 100.0 * kKmh, 4, GearboxMode::Sequential));
    world.seal();
    run(world, input(0.0), kStepsPerSecond / 4);
    const double before = rpm(world);
    world.step(input(0.0, 0.0, 0.0, rally::sim::kButtonShiftDown));
    double worst_kappa = 0.0;
    double peak_rpm = 0.0;
    for (int s = 0; s < kStepsPerSecond / 2; ++s) {
        world.step(SimInput{});
        peak_rpm = std::max(peak_rpm, rpm(world));
        for (const int i : {rally::sim::kRearLeft, rally::sim::kRearRight}) {
            worst_kappa =
                std::min(worst_kappa, static_cast<double>(world.car_controller().wheel(i).kappa));
        }
    }
    MESSAGE("reducción de 4ª a 3ª a 100 km/h: de ", before, " rpm, golpe de gas hasta ", peak_rpm,
            " rpm; κ_r trasero mínimo ", worst_kappa, " (pico ",
            -car.tire(Surface::Asphalt).slip_ratio_peak, ")");
    CHECK(world.car_controller().gear() == 3);
    CHECK(peak_rpm > before + 1000.0);
    CHECK(worst_kappa > -car.tire(Surface::Asphalt).slip_ratio_peak);
    check_lcp(world);
}

TEST_CASE("drivetrain: with all four wheels in the air flat out the engine stays at the limiter") {
    const rally::sim::JoltRuntime runtime;
    // Secuencial (el automático subiría a 7800 rpm) y ~1,3 s de caída: el motor llega al
    // limitador arrastrando las ruedas.
    WorldDesc desc = drive_world(Surface::Asphalt, 0.0, 1, GearboxMode::Sequential);
    desc.car_spawn.y = 8.0;
    SimWorld world;
    world.build(desc);
    world.seal();
    double hi = 0.0;
    int airborne = 0;
    for (int s = 0; s < 2 * kStepsPerSecond; ++s) {
        world.step(input(1.0));
        bool any = false;
        for (int i = 0; i < kWheelCount; ++i) {
            any = any || world.car_controller().wheel(i).contact;
        }
        if (any) {
            break;
        }
        ++airborne;
        hi = std::max(hi, rpm(world));
    }
    MESSAGE("cuatro ruedas en el aire ", static_cast<double>(airborne) / kStepsPerSecond,
            " s a fondo en 1ª: rpm máximas ", hi);
    REQUIRE(airborne > kStepsPerSecond);
    CHECK(hi >= 8100.0);
    CHECK(hi <= 8200.0);
    check_lcp(world);
}

TEST_CASE("drivetrain: landing flat out in second with the clutch closed limits the torque and "
          "does not ring") {
    const rally::sim::JoltRuntime runtime;
    const CarData& car = reference_car();
    WorldDesc desc = drive_world(Surface::Asphalt, 50.0 * kKmh, 2, GearboxMode::Sequential);
    desc.car_spawn.y = 0.5;
    SimWorld world;
    world.build(desc);
    world.seal();
    int contact_step = -1;
    double peak = 0.0;
    std::vector<double> rpms;
    for (int s = 0; s < 2 * kStepsPerSecond; ++s) {
        world.step(input(1.0));
        const rally::sim::CarController& c = world.car_controller();
        bool any = false;
        for (int i = 0; i < kWheelCount; ++i) {
            any = any || c.wheel(i).contact;
        }
        if (any && contact_step < 0 && s > 2) {
            contact_step = s;
        }
        if (contact_step >= 0 && s - contact_step < kStepsPerSecond / 2) {
            peak = std::max(peak, std::abs(static_cast<double>(c.drivetrain().clutch_torque)));
            rpms.push_back(rpm(world));
        }
    }
    int changes = 0;
    int last = 0;
    for (std::size_t i = 1; i < rpms.size(); ++i) {
        const double d = rpms[i] - rpms[i - 1];
        const int sign = d >= 0.5 ? 1 : (d <= -0.5 ? -1 : 0);
        if (sign != 0) {
            if (last != 0 && sign != last) {
                ++changes;
            }
            last = sign;
        }
    }
    // Oscilación del tren motriz: el temblor de paso a paso (σ de las rpm respecto a su media
    // móvil de 5 pasos) en la segunda mitad de la ventana, como el criterio 10 mide el final de
    // cada tramo: en la primera, la respuesta la fuerza la suspensión (la trasera llega al tope a
    // los 0,1 s, con 100 kN) y crece con la altura de la caída; si el tren motriz oscilara,
    // seguiría después. Los cambios de signo de dω_e y la σ de toda la ventana, como dato.
    const auto jitter = [&](std::size_t from) {
        double sq = 0.0;
        int count = 0;
        for (std::size_t i = std::max<std::size_t>(from, 2); i + 2 < rpms.size(); ++i) {
            const double mean =
                (rpms[i - 2] + rpms[i - 1] + rpms[i] + rpms[i + 1] + rpms[i + 2]) / 5.0;
            sq += (rpms[i] - mean) * (rpms[i] - mean);
            ++count;
        }
        return std::sqrt(sq / count);
    };
    const double settled = jitter(rpms.size() / 2);
    MESSAGE("aterrizaje a fondo en 2ª desde 0,5 m: par del embrague máximo ", peak,
            " N·m (capacidad ", car.clutch_capacity, "); σ de las rpm sobre la media móvil ",
            settled, " rpm de 0,25 a 0,5 s (", jitter(0), " en toda la ventana); ", changes,
            " cambios de signo de dω_e en 0,5 s (forzados por la suspensión)");
    REQUIRE(contact_step > 0);
    CHECK(peak <= car.clutch_capacity * 1.0001);
    CHECK(settled <= 10.0);
    check_lcp(world);
}

// --- SaveState del tren motriz ---------------------------------------------------------------

namespace {

// Guarda a mitad del guion y comprueba que la continuación restaurada da el mismo estado.
void check_save_restore(const WorldDesc& desc, int save_step, int total, SimInput (*script)(int),
                        const char* what) {
    SimWorld world;
    world.build(desc);
    world.seal();
    for (int s = 0; s < save_step; ++s) {
        world.step(script(s));
    }
    const std::string saved = world.save_state();
    const rally::sim::ControlState controls = world.controls();
    for (int s = save_step; s < total; ++s) {
        world.step(script(s));
    }
    SimWorld restored;
    restored.build(desc);
    restored.seal();
    restored.restore_state(saved, controls);
    for (int s = save_step; s < total; ++s) {
        restored.step(script(s));
    }
    INFO(what);
    CHECK(restored.state_hash() == world.state_hash());
    CHECK(restored.save_state() == world.save_state());
}

SimInput shift_script(int s) {
    // Secuencial: a fondo y sube a 3ª a los 2 s, con la dirección para cargar un autoblocante.
    SimInput in = input(1.0, 0.0, s > 240 ? 0.6 : 0.0);
    if (s == 480) {
        in.buttons = rally::sim::kButtonShiftUp;
    }
    return in;
}

SimInput drift_script(int /*s*/) { return input(0.6, 0.0, 1.0); }

// Medio acelerador desde parado (el embrague se cierra por debajo de las rpm de arranque) y a
// fondo desde el paso 720.
SimInput half_then_full_script(int s) { return input(s < 720 ? 0.5 : 1.0); }

} // namespace

TEST_CASE("drivetrain: SaveState in a shift, a drift hold and a closed clutch restores exactly") {
    const rally::sim::JoltRuntime runtime;
    {
        const WorldDesc desc =
            drive_world(Surface::Gravel, 40.0 * kKmh, 2, GearboxMode::Sequential, 600.0);
        // Paso 488: 8 pasos dentro del cambio de 2ª a 3ª, con presión y el autoblocante.
        SimWorld probe;
        probe.build(desc);
        probe.seal();
        for (int s = 0; s < 488; ++s) {
            probe.step(shift_script(s));
        }
        const DrivetrainOutput& d = probe.car_controller().drivetrain();
        REQUIRE(d.shifting);
        REQUIRE(d.boost > 0.0f);
        check_save_restore(desc, 488, 488 + 240, shift_script, "en mitad del cambio");
    }
    {
        WorldDesc desc = drive_world(Surface::Gravel, 65.0 * kKmh, 2, GearboxMode::Automatic);
        desc.bodies[0].half_x = 400.0f;
        desc.bodies[0].half_z = 400.0f;
        desc.bodies[0].z = 0.0;
        // Primer paso con la espera en derrape en curso; se guarda 60 pasos dentro (la espera
        // llega a sus 180 pasos).
        SimWorld probe;
        probe.build(desc);
        probe.seal();
        int at = -1;
        for (int s = 0; s < 6 * kStepsPerSecond && at < 0; ++s) {
            probe.step(drift_script(s));
            if (probe.car_controller().drivetrain().drift_hold) {
                at = s + 1;
            }
        }
        REQUIRE(at > 0);
        check_save_restore(desc, at + 60, at + 60 + 240, drift_script, "en mitad de la espera");
    }
    {
        // Desde parado (embrague en arranque al construir) con medio acelerador: a los 3 s está
        // cerrado por debajo de las rpm de arranque; después, a fondo, y se guarda 40 pasos
        // más tarde, con el embrague pasando ~140 N·m. Si el modo no se guardara, el restaurado
        // volvería a arranque, que a ~3100 rpm solo muerde ~75 N·m: patinaría.
        const WorldDesc desc =
            drive_world(Surface::Asphalt, 0.0, 1, GearboxMode::Sequential, 2000.0);
        SimWorld probe;
        probe.build(desc);
        probe.seal();
        REQUIRE(probe.car_controller().drivetrain().clutch == rally::sim::ClutchMode::Launch);
        for (int s = 0; s < 760; ++s) {
            probe.step(half_then_full_script(s));
        }
        const DrivetrainOutput& d = probe.car_controller().drivetrain();
        REQUIRE(d.clutch == rally::sim::ClutchMode::Closed);
        REQUIRE(rpm(probe) < 4000.0);
        REQUIRE(d.clutch_torque > 100.0f);
        check_save_restore(desc, 760, 760 + 240, half_then_full_script, "con el embrague cerrado");
    }
}

// --- LCP -------------------------------------------------------------------------------------

TEST_CASE("drivetrain: the LCP sticks within the box, slips at the box and never reverses") {
    using rally::sim::DriveSystem;
    using rally::sim::RowState;
    const float h = 1.0f / 960.0f;
    // Motor (0,15 kg·m²) y una rueda (1,2) unidos por el embrague con G = 1: pegado.
    DriveSystem s;
    s.inv_mass = {1.0f / 0.15f, 1.0f / 1.2f, 1.0f, 1.0f, 1.0f};
    s.velocity = {100.0f, 90.0f, 0.0f, 0.0f, 0.0f};
    s.jacobian[0] = {1.0f, -1.0f, 0.0f, 0.0f, 0.0f};
    s.active[0] = true;
    s.cap[0] = h * 1.0e6f;
    std::array<RowState, rally::sim::kDriveRows> rows{};
    rally::sim::DriveSolution sol = rally::sim::solve_drive(s, rows);
    CHECK(sol.converged);
    CHECK(rows[0] == RowState::Stick);
    // Velocidad común: la media ponderada por inercias.
    const double common = (0.15 * 100.0 + 1.2 * 90.0) / 1.35;
    CHECK(sol.velocity[0] == doctest::Approx(common).epsilon(1e-5));
    CHECK(sol.velocity[1] == doctest::Approx(common).epsilon(1e-5));
    // Con poca capacidad: desliza con el impulso de la caja y no se cruzan.
    s.cap[0] = h * 50.0f;
    rows = {};
    sol = rally::sim::solve_drive(s, rows);
    CHECK(rows[0] == RowState::SlipNegative);
    CHECK(sol.impulse[0] == doctest::Approx(-h * 50.0f));
    CHECK(sol.velocity[0] > sol.velocity[1]);
    // Viscoacoplador solo: Δω' = Δω*/(1 + h·c·(1/I_1 + 1/I_2)), mismo signo.
    DriveSystem v;
    v.inv_mass = {1.0f, 1.0f / 1.2f, 1.0f / 1.2f, 1.0f / 1.2f, 1.0f / 1.2f};
    v.velocity = {0.0f, 50.0f, 50.0f, 40.0f, 40.0f};
    v.jacobian[1] = {0.0f, 0.5f, 0.5f, -0.5f, -0.5f};
    v.active[1] = true;
    const float c = 1.0e5f; // muy rígido
    v.soft[1] = 1.0f / (h * c);
    rows = {};
    sol = rally::sim::solve_drive(v, rows);
    const double dw =
        0.5 * (sol.velocity[1] + sol.velocity[2]) - 0.5 * (sol.velocity[3] + sol.velocity[4]);
    // Inercias de cada eje: 2 × 1,2.
    const double expected = 10.0 / (1.0 + h * c * (1.0 / 2.4 + 1.0 / 2.4));
    CHECK(dw == doctest::Approx(expected).epsilon(1e-3));
    CHECK(dw > 0.0);
    // Filas dependientes: las dos ruedas traseras frenadas y su autoblocante, todo pegado.
    DriveSystem d;
    d.inv_mass = {1.0f, 1.0f / 1.2f, 1.0f / 1.2f, 1.0f / 1.2f, 1.0f / 1.2f};
    d.velocity = {0.0f, 0.0f, 0.0f, 0.3f, 0.2f};
    d.jacobian[3] = {0.0f, 0.0f, 0.0f, 1.0f, -1.0f};
    d.jacobian[6] = {0.0f, 0.0f, 0.0f, 1.0f, 0.0f};
    d.jacobian[7] = {0.0f, 0.0f, 0.0f, 0.0f, 1.0f};
    for (const int k : {3, 6, 7}) {
        d.active[static_cast<std::size_t>(k)] = true;
        d.cap[static_cast<std::size_t>(k)] = h * 1500.0f;
    }
    rows = {};
    sol = rally::sim::solve_drive(d, rows);
    CHECK(sol.converged);
    CHECK(std::abs(sol.velocity[3]) < 1e-4);
    CHECK(std::abs(sol.velocity[4]) < 1e-4);
}

TEST_CASE("drivetrain: a single braked wheel through the LCP spins like step_tire") {
    // La rueda sola, sin tren motriz: predict_tire + LCP con solo su fila de freno + finish_tire
    // frente a step_tire (Coulomb), en varias velocidades, cargas y frenos.
    const CarData& car = reference_car();
    const float h = 1.0f / 960.0f;
    double worst = 0.0;
    for (const float load : {0.0f, 3000.0f}) {
        for (const float vx : {0.0f, 0.3f, 10.0f, 30.0f}) {
            for (const float stop : {0.0f, 50.0f, 2000.0f}) {
                rally::sim::TireInput in;
                in.load = load;
                in.surface = Surface::Gravel;
                in.radius = car.wheel_radius;
                in.vx = vx;
                in.vy = 0.5f;
                in.stop_torque = stop;
                rally::sim::WheelDynamics a{vx / car.wheel_radius + 1.0f, 0.001f, -0.002f};
                rally::sim::WheelDynamics b = a;
                for (int i = 0; i < 40; ++i) {
                    rally::sim::step_tire(car, in, h, true, a);
                    const rally::sim::TirePrediction p =
                        rally::sim::predict_tire(car, in, h, true, b);
                    rally::sim::DriveSystem s;
                    s.inv_mass = {1.0f, 1.0f / rally::sim::wheel_effective_inertia(car, in, h, p),
                                  1.0f, 1.0f, 1.0f};
                    s.velocity = {0.0f, b.omega + h * s.inv_mass[1] * (-in.radius * p.fx0), 0.0f,
                                  0.0f, 0.0f};
                    s.jacobian[4] = {0.0f, 1.0f, 0.0f, 0.0f, 0.0f};
                    s.active[4] = stop > 0.0f;
                    s.cap[4] = h * stop;
                    std::array<rally::sim::RowState, rally::sim::kDriveRows> rows{};
                    const rally::sim::DriveSolution sol = rally::sim::solve_drive(s, rows);
                    float omega = sol.velocity[1];
                    if (rows[4] == rally::sim::RowState::Stick) {
                        omega = 0.0f;
                    }
                    rally::sim::finish_tire(car, in, h, p, omega, b);
                    worst = std::max(worst, static_cast<double>(std::abs(a.omega - b.omega)));
                }
            }
        }
    }
    MESSAGE("rueda sola: |Δω| máximo entre step_tire y el LCP ", worst, " rad/s");
    CHECK(worst < 1e-3);
}
