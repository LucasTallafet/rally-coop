// Coche base (tarea 2.4): lector del .car y criterios de aceptación de docs/prompts/fase-2/2.4.md
// con el coche congelado tests/data/cars/reference.car en mundos pequeños.

#include "car_worlds.h"
#include "sim/car_controller.h"
#include "sim/car_data.h"
#include "sim/demo_world.h"
#include "sim/jolt_runtime.h"
#include "sim/sim_input.h"
#include "sim/sim_world.h"
#include "sim/test_zone.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <clocale>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using rally::sim::BodyPose;
using rally::sim::CarController;
using rally::sim::CarData;
using rally::sim::kAxisMax;
using rally::sim::kStepsPerSecond;
using rally::sim::kWheelCount;
using rally::sim::SimInput;
using rally::sim::SimWorld;
using rally::sim::Surface;
using rally::sim::WheelOutput;
using rally::sim::WorldDesc;
using rally::tests::flat_world;
using rally::tests::reference_car;
using rally::tests::reference_car_text;

constexpr double kPi = 3.14159265358979323846;
constexpr double kGravity = 9.81;

void run(SimWorld& world, const SimInput& input, int steps) {
    for (int i = 0; i < steps; ++i) {
        world.step(input);
    }
}

// Ejes del coche en el mundo a partir de su orientación.
struct Axes {
    double fx, fy, fz; // adelante (-z local)
    double rx, ry, rz; // derecha (+x local)
    double ux, uy, uz; // arriba (+y local)
};

Axes axes(const BodyPose& p) {
    const double x = p.qx;
    const double y = p.qy;
    const double z = p.qz;
    const double w = p.qw;
    // Columnas de la matriz de rotación.
    const double c0[3] = {1 - 2 * (y * y + z * z), 2 * (x * y + w * z), 2 * (x * z - w * y)};
    const double c1[3] = {2 * (x * y - w * z), 1 - 2 * (x * x + z * z), 2 * (y * z + w * x)};
    const double c2[3] = {2 * (x * z + w * y), 2 * (y * z - w * x), 1 - 2 * (x * x + y * y)};
    return Axes{-c2[0], -c2[1], -c2[2], c0[0], c0[1], c0[2], c1[0], c1[1], c1[2]};
}

double pitch_deg(const BodyPose& p) { return std::asin(axes(p).fy) * 180.0 / kPi; }
double roll_deg(const BodyPose& p) { return std::asin(axes(p).ry) * 180.0 / kPi; }
double heading(const BodyPose& p) {
    const Axes a = axes(p);
    return std::atan2(a.fx, -a.fz);
}

// Velocidad hacia delante del coche.
double forward_speed(const SimWorld& world) {
    const Axes a = axes(world.car_pose());
    const rally::sim::BodyVelocity v = world.car_velocity();
    return v.vx * a.fx + v.vy * a.fy + v.vz * a.fz;
}

// Altura mínima de las esquinas de abajo de la caja de la carrocería sobre el suelo que da
// `ground` (x, z).
template <typename Ground>
double body_clearance(const SimWorld& world, Ground ground) {
    const CarData& car = reference_car();
    const BodyPose p = world.car_pose();
    const Axes a = axes(p);
    double lowest = 1e9;
    for (int sx = -1; sx <= 1; sx += 2) {
        for (int sz = -1; sz <= 1; sz += 2) {
            const double lx = 0.5 * sx * car.box_width;
            const double ly = car.ground_clearance;
            const double lz = 0.5 * sz * car.box_length; // +z local es hacia atrás
            const double wx = p.x + a.rx * lx + a.ux * ly - a.fx * lz;
            const double wy = p.y + a.ry * lx + a.uy * ly - a.fy * lz;
            const double wz = p.z + a.rz * lx + a.uz * ly - a.fz * lz;
            lowest = std::min(lowest, wy - ground(wx, wz));
        }
    }
    return lowest;
}

double mean_compression(const SimWorld& world) {
    double sum = 0.0;
    for (int i = 0; i < kWheelCount; ++i) {
        sum += world.car_controller().wheel(i).compression;
    }
    return sum / kWheelCount;
}

double static_load(int wheel) {
    const CarData& car = reference_car();
    const bool front = wheel < 2;
    return 0.5 * (front ? car.front_weight : 1.0 - car.front_weight) * car.mass * kGravity;
}

SimInput pedal(std::int16_t throttle, std::int16_t brake, std::uint16_t buttons = 0) {
    SimInput input;
    input.throttle = throttle;
    input.brake = brake;
    input.buttons = buttons;
    return input;
}

} // namespace

// --- 1. Lector -------------------------------------------------------------------------------

TEST_CASE("car_data: the reference car reads whole and degrees become radians") {
    const rally::sim::CarParseResult result = rally::sim::parse_car(reference_car_text());
    REQUIRE_MESSAGE(result.ok, result.error);
    const CarData& car = result.car;
    CHECK(car.mass == 1100.0f);
    CHECK(car.front_weight == 0.45f);
    CHECK(car.max_steer == doctest::Approx(0.5236).epsilon(1e-4));
    CHECK(car.drive_power == 250000.0f);
    CHECK(car.mu(Surface::Snow) == 0.45f);
    CHECK(car.eye_height == 1.05f);
}

namespace {

std::string replace_line(const std::string& text, const std::string& from, const std::string& to) {
    const std::size_t at = text.find(from);
    REQUIRE(at != std::string::npos);
    std::string out = text;
    out.replace(at, from.size(), to);
    return out;
}

int line_of(const std::string& text, const std::string& needle) {
    const std::size_t at = text.find(needle);
    REQUIRE(at != std::string::npos);
    return 1 +
           static_cast<int>(std::count(text.begin(), text.begin() + static_cast<long>(at), '\n'));
}

} // namespace

TEST_CASE("car_data: an unknown key, a missing key or a bad number fails with its line") {
    const std::string& text = reference_car_text();
    SUBCASE("unknown key") {
        const std::string bad = replace_line(text, "cg_height = 0.45", "cg_heigth = 0.45");
        const rally::sim::CarParseResult r = rally::sim::parse_car(bad);
        CHECK_FALSE(r.ok);
        const std::string expected = "línea " + std::to_string(line_of(bad, "cg_heigth")) + ":";
        MESSAGE(r.error);
        CHECK(r.error.rfind(expected, 0) == 0);
        CHECK(r.error.find("clave desconocida 'cg_heigth'") != std::string::npos);
    }
    SUBCASE("bad number") {
        const std::string bad = replace_line(text, "track = 1.43", "track = 1,43");
        const rally::sim::CarParseResult r = rally::sim::parse_car(bad);
        CHECK_FALSE(r.ok);
        MESSAGE(r.error);
        CHECK(r.error.rfind("línea " + std::to_string(line_of(bad, "track = 1,43")) + ":", 0) == 0);
        CHECK(r.error.find("número mal escrito") != std::string::npos);
    }
    SUBCASE("missing key") {
        const std::string bad = replace_line(text, "radius = 0.31", "");
        const rally::sim::CarParseResult r = rally::sim::parse_car(bad);
        CHECK_FALSE(r.ok);
        MESSAGE(r.error);
        CHECK(r.error.rfind("línea ", 0) == 0);
        CHECK(r.error.find("falta la clave 'radius' en [wheels]") != std::string::npos);
    }
    SUBCASE("repeated key") {
        const std::string bad = replace_line(text, "track = 1.43", "track = 1.43\ntrack = 1.5");
        const rally::sim::CarParseResult r = rally::sim::parse_car(bad);
        CHECK_FALSE(r.ok);
        CHECK(r.error.find("repetida") != std::string::npos);
    }
    SUBCASE("out of range") {
        const std::string bad = replace_line(text, "front_weight = 0.45", "front_weight = 4.5");
        CHECK_FALSE(rally::sim::parse_car(bad).ok);
    }
}

TEST_CASE("car_data: a Spanish process locale reads the same numbers") {
    const std::string previous = std::setlocale(LC_ALL, nullptr);
    const char* spanish = std::setlocale(LC_ALL, "Spanish_Spain.1252");
    if (spanish == nullptr) {
        spanish = std::setlocale(LC_ALL, "es-ES");
    }
    REQUIRE(spanish != nullptr);
    char formatted[16];
    std::snprintf(formatted, sizeof(formatted), "%.1f", 0.5);
    CHECK(std::string(formatted) == "0,5"); // el idioma está activo de verdad
    const rally::sim::CarParseResult r = rally::sim::parse_car(reference_car_text());
    std::setlocale(LC_ALL, previous.c_str());
    REQUIRE(r.ok);
    CHECK(r.car.track == reference_car().track);
    CHECK(r.car.max_steer == reference_car().max_steer);
    CHECK(r.car.mu_gravel == reference_car().mu_gravel);
}

// --- 2. Equilibrio ---------------------------------------------------------------------------

TEST_CASE("car: at rest on flat ground the loads add up to m g with the 45/55 split, level") {
    const rally::sim::JoltRuntime runtime;
    SimWorld world;
    world.build(flat_world(Surface::Asphalt));
    world.seal();
    run(world, SimInput{}, 3 * kStepsPerSecond);

    const CarData& car = reference_car();
    double total = 0.0;
    double front = 0.0;
    for (int i = 0; i < kWheelCount; ++i) {
        const WheelOutput& w = world.car_controller().wheel(i);
        REQUIRE(w.contact);
        total += w.load;
        front += i < 2 ? w.load : 0.0;
        MESSAGE("rueda ", i, ": carga ", w.load, " N, compresión ", w.compression, " m");
    }
    const BodyPose pose = world.car_pose();
    const double weight = car.mass * kGravity;
    MESSAGE("suma de cargas ", total, " N (m·g = ", weight, "); delante ", 100.0 * front / total,
            " %; cabeceo ", pitch_deg(pose), "°; balanceo ", roll_deg(pose), "°; caja a ",
            body_clearance(world, [](double, double) { return 0.0; }), " m del suelo; origen a ",
            pose.y, " m");
    CHECK(std::abs(total - weight) / weight < 0.005);
    CHECK(std::abs(front / total - car.front_weight) < 0.01);
    CHECK(std::abs(pitch_deg(pose)) < 0.1);
    CHECK(std::abs(roll_deg(pose)) < 0.1);
}

// --- 3. Caída --------------------------------------------------------------------------------

TEST_CASE("car: dropped from 0.5 m it settles with at most 2 visible bounces") {
    const rally::sim::JoltRuntime runtime;
    // Compresión de equilibrio (media de las cuatro ruedas) en llano.
    double equilibrium = 0.0;
    {
        SimWorld rest;
        rest.build(flat_world(Surface::Asphalt));
        rest.seal();
        run(rest, SimInput{}, 3 * kStepsPerSecond);
        equilibrium = mean_compression(rest);
    }
    WorldDesc desc = flat_world(Surface::Asphalt);
    desc.car_spawn.y = 0.5;
    SimWorld world;
    world.build(desc);
    world.seal();
    int bounces = 0;
    bool above = false;
    double max_compression = 0.0;
    double min_clearance = 1e9;
    for (int i = 0; i < 4 * kStepsPerSecond; ++i) {
        world.step(SimInput{});
        const double c = mean_compression(world);
        for (int w = 0; w < kWheelCount; ++w) {
            max_compression = std::max(
                max_compression, static_cast<double>(world.car_controller().wheel(w).compression));
        }
        min_clearance =
            std::min(min_clearance, body_clearance(world, [](double, double) { return 0.0; }));
        const bool now_above = c > equilibrium + 0.01;
        if (now_above && !above) {
            ++bounces;
        }
        above = now_above;
    }
    MESSAGE("rebotes (compresión > equilibrio + 1 cm): ", bounces, "; compresión máxima ",
            max_compression, " m de ", reference_car().travel, " (tope a ", reference_car().travel,
            "); equilibrio ", equilibrium, " m; caja a ", min_clearance,
            " m del suelo como mínimo");
    CHECK(bounces <= 2);
    CHECK(min_clearance > 0.0);
}

// --- 4. Parado -------------------------------------------------------------------------------

namespace {

// Desplazamiento del origen del coche entre dos poses.
double moved(const BodyPose& a, const BodyPose& b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) +
                     (a.z - b.z) * (a.z - b.z));
}

double hold_on_ramp(const SimInput& input) {
    WorldDesc desc = rally::tests::ramp_world();
    desc.car_spawn.y = 0.25; // sale por encima y cae sobre la rampa
    SimWorld world;
    world.build(desc);
    world.seal();
    run(world, input, 3 * kStepsPerSecond); // se asienta con el freno puesto
    const BodyPose start = world.car_pose();
    run(world, input, 10 * kStepsPerSecond);
    for (int i = 0; i < kWheelCount; ++i) {
        const WheelOutput& w = world.car_controller().wheel(i);
        MESSAGE("rueda ", i, ": ancla ", w.anchored, ", carga ", w.load, " N");
    }
    return moved(start, world.car_pose());
}

} // namespace

TEST_CASE("car: parked on the 15 percent ramp it moves less than 1 cm in 10 s") {
    const rally::sim::JoltRuntime runtime;
    const double with_brake = hold_on_ramp(pedal(0, kAxisMax));
    const double with_handbrake = hold_on_ramp(pedal(0, 0, rally::sim::kButtonHandbrake));
    MESSAGE("en la rampa del 15 % en 10 s: con el freno ", with_brake * 1000.0,
            " mm; con el freno de mano ", with_handbrake * 1000.0, " mm");
    CHECK(with_brake < 0.01);
    CHECK(with_handbrake < 0.01);
}

TEST_CASE("car: released on the ramp it rolls back") {
    const rally::sim::JoltRuntime runtime;
    WorldDesc desc = rally::tests::ramp_world();
    desc.car_spawn.y = 0.25;
    SimWorld world;
    world.build(desc);
    world.seal();
    run(world, pedal(0, kAxisMax), 3 * kStepsPerSecond);
    const BodyPose held = world.car_pose();
    run(world, SimInput{}, 2 * kStepsPerSecond);
    const BodyPose rolled = world.car_pose();
    MESSAGE("2 s después de soltar el freno: ", rolled.z - held.z, " m hacia atrás (+z)");
    CHECK(rolled.z - held.z > 0.5);
}

TEST_CASE("car: on flat ground with no input for 60 s it moves less than 1 mm and does not turn") {
    const rally::sim::JoltRuntime runtime;
    SimWorld world;
    world.build(flat_world(Surface::Asphalt, true));
    world.seal();
    run(world, SimInput{}, 3 * kStepsPerSecond);
    const BodyPose start = world.car_pose();
    run(world, SimInput{}, 60 * kStepsPerSecond);
    const BodyPose end = world.car_pose();
    const double turned = std::abs(heading(end) - heading(start)) * 180.0 / kPi;
    MESSAGE("60 s sin entradas: ", moved(start, end) * 1000.0, " mm, giro ", turned, "°");
    CHECK(moved(start, end) < 0.001);
    CHECK(turned < 0.01);
}

// --- 5. Barrido de estabilidad de las ruedas -------------------------------------------------

namespace {

enum class Case { Throttle, Brake, Neutral };

struct SweepResult {
    int sign_changes = 0;   // máximo de las cuatro ruedas en el último segundo
    double kappa_std = 0.0; // máximo de las cuatro ruedas en el último segundo
};

SweepResult sweep_case(Surface surface, float speed, Case c, bool explicit_wheels) {
    WorldDesc desc = flat_world(surface, false, speed);
    desc.explicit_wheels = explicit_wheels;
    SimWorld world;
    world.build(desc);
    world.seal();
    const SimInput input = c == Case::Throttle ? pedal(kAxisMax, 0)
                           : c == Case::Brake  ? pedal(0, kAxisMax)
                                               : SimInput{};
    run(world, input, kStepsPerSecond);
    std::array<int, kWheelCount> changes{};
    std::array<int, kWheelCount> last_sign{};
    std::array<double, kWheelCount> sum{};
    std::array<double, kWheelCount> sum_sq{};
    for (int s = 0; s < kStepsPerSecond; ++s) {
        world.step(input);
        for (int i = 0; i < kWheelCount; ++i) {
            const auto k = static_cast<std::size_t>(i);
            const double kappa = world.car_controller().wheel(i).kappa;
            // El signo de ω·r − v es el de κ (el denominador es positivo).
            const int sign = kappa > 0.0 ? 1 : (kappa < 0.0 ? -1 : 0);
            if (sign != 0) {
                if (last_sign[k] != 0 && sign != last_sign[k]) {
                    ++changes[k];
                }
                last_sign[k] = sign;
            }
            sum[k] += kappa;
            sum_sq[k] += kappa * kappa;
        }
    }
    SweepResult result;
    for (std::size_t k = 0; k < kWheelCount; ++k) {
        result.sign_changes = std::max(result.sign_changes, changes[k]);
        const double mean = sum[k] / kStepsPerSecond;
        const double variance = std::max(0.0, sum_sq[k] / kStepsPerSecond - mean * mean);
        result.kappa_std = std::max(result.kappa_std, std::sqrt(variance));
    }
    return result;
}

constexpr std::array<float, 9> kSweepSpeeds = {0.5f,  1.0f,  2.0f,  3.0f, 5.0f,
                                               10.0f, 20.0f, 40.0f, 60.0f};

std::string sweep_table(bool explicit_wheels, int& worst_changes, double& worst_std) {
    std::string table = "superficie | v (m/s) | acelerador | freno | punto muerto (σκ)\n";
    worst_changes = 0;
    worst_std = 0.0;
    for (const Surface surface : {Surface::Asphalt, Surface::Snow}) {
        for (const float speed : kSweepSpeeds) {
            const SweepResult t = sweep_case(surface, speed, Case::Throttle, explicit_wheels);
            const SweepResult b = sweep_case(surface, speed, Case::Brake, explicit_wheels);
            const SweepResult n = sweep_case(surface, speed, Case::Neutral, explicit_wheels);
            char row[160];
            std::snprintf(row, sizeof(row), "%s | %.1f | %d | %d | %d (%.5f)\n",
                          rally::sim::surface_name(surface), static_cast<double>(speed),
                          t.sign_changes, b.sign_changes, n.sign_changes, n.kappa_std);
            table += row;
            worst_changes =
                std::max({worst_changes, t.sign_changes, b.sign_changes, n.sign_changes});
            worst_std = std::max(worst_std, n.kappa_std);
        }
    }
    return table;
}

} // namespace

TEST_CASE("car: the wheels are stable from 0.5 to 60 m/s on asphalt and snow") {
    const rally::sim::JoltRuntime runtime;
    int changes = 0;
    double kappa_std = 0.0;
    const std::string table = sweep_table(false, changes, kappa_std);
    MESSAGE("barrido semi-implícito (cambios de signo de ω·r − v en el último segundo, máximo "
            "de las cuatro ruedas):\n",
            table);
    CHECK(changes <= 2);
    CHECK(kappa_std < 0.02);

    // Referencia, sin juzgar: la integración explícita.
    int explicit_changes = 0;
    double explicit_std = 0.0;
    const std::string reference = sweep_table(true, explicit_changes, explicit_std);
    MESSAGE("barrido explícito (referencia, no se juzga):\n", reference);
}

TEST_CASE("car: full brake at 1 m/s takes the wheels to 0 and never below") {
    const rally::sim::JoltRuntime runtime;
    SimWorld world;
    world.build(flat_world(Surface::Asphalt, false, 1.0f));
    world.seal();
    bool reached_zero = false;
    double lowest = 1e9;
    for (int s = 0; s < 2 * kStepsPerSecond; ++s) {
        world.step(pedal(0, kAxisMax));
        for (int i = 0; i < kWheelCount; ++i) {
            const double omega = world.car_controller().wheel(i).omega;
            lowest = std::min(lowest, omega);
        }
        bool all_zero = true;
        for (int i = 0; i < kWheelCount; ++i) {
            all_zero = all_zero && world.car_controller().wheel(i).omega == 0.0f;
        }
        reached_zero = reached_zero || all_zero;
    }
    MESSAGE("ω mínima con el freno a 1 m/s: ", lowest, " rad/s");
    CHECK(reached_zero);
    CHECK(lowest >= 0.0);
}

// --- 6. Sin golpes falsos --------------------------------------------------------------------

namespace {

struct RideResult {
    double max_impact = 0.0;
    double max_load_jump = 0.0; // máximo |ΔFz| entre dos pasos / carga estática
    double min_clearance = 1e9;
};

// Coche en punto muerto `seconds` s; mide desde `warmup` pasos (el primer paso no tiene carga
// del paso anterior y la rueda sube a h de golpe).
template <typename Ground>
RideResult ride(SimWorld& world, int seconds, int warmup, Ground ground) {
    RideResult r;
    std::array<double, kWheelCount> previous{};
    for (int s = 0; s < seconds * kStepsPerSecond; ++s) {
        world.step(SimInput{});
        for (int i = 0; i < kWheelCount; ++i) {
            const double load = world.car_controller().wheel(i).load;
            if (s > warmup) {
                r.max_load_jump = std::max(r.max_load_jump,
                                           std::abs(load - previous[static_cast<std::size_t>(i)]) /
                                               static_load(i));
            }
            previous[static_cast<std::size_t>(i)] = load;
        }
        if (s >= warmup) {
            r.max_impact = std::max(r.max_impact, static_cast<double>(world.last_impact()));
            r.min_clearance = std::min(r.min_clearance, body_clearance(world, ground));
        }
    }
    return r;
}

} // namespace

TEST_CASE("car: no false hits on flat asphalt with h at 30, 60 and 120 km/h") {
    const rally::sim::JoltRuntime runtime;
    const rally::sim::TestZone noise; // la misma banda de h que nota la rueda
    for (const double kmh : {30.0, 60.0, 120.0}) {
        SimWorld world;
        world.build(flat_world(Surface::Asphalt, true, static_cast<float>(kmh / 3.6)));
        world.seal();
        const RideResult r = ride(world, 5, kStepsPerSecond / 2, [&noise](double x, double z) {
            return noise.surface_noise(Surface::Asphalt, x, z);
        });
        MESSAGE(kmh, " km/h en asfalto con h: impacto máximo ", r.max_impact,
                " m/s; salto de carga máximo entre pasos ", 100.0 * r.max_load_jump,
                " % de la estática");
        CHECK(r.max_impact < 0.3);
        CHECK(r.max_load_jump < 0.05);
    }
}

namespace {

// Coche en la zona en (0, z) mirando al norte a `kmh`.
WorldDesc zone_ride(double z, double kmh) {
    WorldDesc desc = rally::sim::zone_world(Surface::Gravel, reference_car());
    const rally::sim::TestZone zone;
    desc.car_spawn.x = 0.0;
    desc.car_spawn.z = z;
    desc.car_spawn.y = zone.ground_height(0.0, z);
    desc.car_spawn.yaw = 0.0f;
    desc.car_spawn.speed = static_cast<float>(kmh / 3.6);
    return desc;
}

} // namespace

TEST_CASE("car: the zone bumps and gravel ripples give no false hits and the body stays clear") {
    const rally::sim::JoltRuntime runtime;
    const rally::sim::TestZone zone;
    const auto ground = [&zone](double x, double z) {
        return zone.ground_height(x, z) + zone.micro_height(x, z);
    };
    // Baches de asfalto (ondulaciones de 6 m y los dos baches sueltos): de z = -350 a -500.
    for (const double kmh : {60.0, 100.0}) {
        SimWorld world;
        world.build(zone_ride(-340.0, kmh));
        world.seal();
        // 200 m: los 150 m de baches y margen para lo que frena en punto muerto.
        const int seconds = static_cast<int>(std::ceil(200.0 / (kmh / 3.6)));
        const RideResult r = ride(world, seconds, kStepsPerSecond / 2, ground);
        MESSAGE("baches de asfalto a ", kmh, " km/h: impacto máximo ", r.max_impact,
                " m/s; caja a ", r.min_clearance,
                " m del suelo como mínimo; llega a z = ", world.car_pose().z);
        CHECK(world.car_pose().z < -500.0);
        CHECK(r.max_impact < 1.0);
        CHECK(r.min_clearance > 0.0);
    }
    // Ondulaciones de la grava: de z = -500 a -650.
    {
        SimWorld world;
        world.build(zone_ride(-490.0, 60.0));
        world.seal();
        const RideResult r = ride(world, 12, kStepsPerSecond / 2, ground);
        MESSAGE("ondulaciones de grava a 60 km/h: impacto máximo ", r.max_impact, " m/s; caja a ",
                r.min_clearance, " m del suelo como mínimo; llega a z = ", world.car_pose().z);
        CHECK(world.car_pose().z < -640.0);
        CHECK(r.max_impact < 1.0);
        CHECK(r.min_clearance > 0.0);
    }
}

// --- 7. Dirección ----------------------------------------------------------------------------

TEST_CASE("car: full steer at 5 m/s follows the Ackermann radius and right turns are positive") {
    const rally::sim::JoltRuntime runtime;
    SimWorld world;
    world.build(flat_world(Surface::Asphalt, false, 5.0f));
    world.seal();
    const CarData& car = reference_car();
    double radius_sum = 0.0;
    double angle_sum = 0.0;
    double yaw_rate_sum = 0.0;
    int samples = 0;
    for (int s = 0; s < 6 * kStepsPerSecond; ++s) {
        // Acelerador proporcional para sostener 5 m/s.
        const double error = 5.0 - forward_speed(world);
        SimInput input;
        input.steer = kAxisMax;
        input.throttle = static_cast<std::int16_t>(std::clamp(error, 0.0, 1.0) * kAxisMax);
        world.step(input);
        if (s < 3 * kStepsPerSecond) {
            continue;
        }
        // Radio de la trayectoria del punto medio del eje trasero: |v| / |ω|.
        const BodyPose p = world.car_pose();
        const Axes a = axes(p);
        const rally::sim::BodyVelocity v = world.car_velocity();
        const BodyPose com = world.car_center_of_mass();
        const double rear_x = p.x - a.fx * 0.5 * car.wheelbase;
        const double rear_y = p.y - a.fy * 0.5 * car.wheelbase;
        const double rear_z = p.z - a.fz * 0.5 * car.wheelbase;
        const double dx = rear_x - com.x;
        const double dy = rear_y - com.y;
        const double dz = rear_z - com.z;
        // v + ω × r
        const double vx = v.vx + (v.wy * dz - v.wz * dy);
        const double vz = v.vz + (v.wx * dy - v.wy * dx);
        const double speed = std::sqrt(vx * vx + vz * vz);
        const double yaw_rate = -(v.wx * a.ux + v.wy * a.uy + v.wz * a.uz);
        radius_sum += speed / std::abs(yaw_rate);
        yaw_rate_sum += yaw_rate;
        const double left = std::abs(world.car_wheel(rally::sim::kFrontLeft).steer);
        const double right = std::abs(world.car_wheel(rally::sim::kFrontRight).steer);
        angle_sum += 0.5 * (left + right);
        ++samples;
    }
    const double radius = radius_sum / samples;
    const double mean_angle = angle_sum / samples;
    const double expected = car.wheelbase / std::tan(mean_angle);
    MESSAGE("radio medido ", radius, " m; batalla / tan(ángulo medio ", mean_angle * 180.0 / kPi,
            "°) = ", expected, " m; guiñada media ", yaw_rate_sum / samples, " rad/s");
    CHECK(std::abs(radius - expected) / expected < 0.10);
    CHECK(yaw_rate_sum / samples > 0.0);
}

// --- 8. Resistencia --------------------------------------------------------------------------

TEST_CASE("car: coasting at 150 km/h decelerates by drag and rolling resistance") {
    const rally::sim::JoltRuntime runtime;
    const double v0 = 150.0 / 3.6;
    SimWorld world;
    world.build(flat_world(Surface::Asphalt, false, static_cast<float>(v0 + 0.45)));
    world.seal();
    run(world, SimInput{}, kStepsPerSecond / 2);
    const double before = forward_speed(world);
    double sum_v2 = 0.0;
    for (int s = 0; s < kStepsPerSecond; ++s) {
        world.step(SimInput{});
        const double v = forward_speed(world);
        sum_v2 += v * v;
    }
    const double after = forward_speed(world);
    const double measured = before - after; // en 1 s
    const CarData& car = reference_car();
    const double mean_v2 = sum_v2 / kStepsPerSecond;
    const double force = 0.5 * rally::sim::kAirDensity * car.cda * mean_v2 +
                         car.rolling_resistance * car.mass * kGravity;
    const double effective_mass =
        car.mass + kWheelCount * car.wheel_inertia / (car.wheel_radius * car.wheel_radius);
    const double expected = force / effective_mass;
    const double plain = force / car.mass;
    MESSAGE("deceleración a ", std::sqrt(mean_v2) * 3.6, " km/h: medida ", measured,
            " m/s²; con la masa efectiva m + ΣI/r² (", effective_mass, " kg) ", expected,
            " m/s²; con m sola ", plain, " m/s²");
    CHECK(std::abs(measured - expected) / expected < 0.05);
}

// --- 9. Recolocar ----------------------------------------------------------------------------

TEST_CASE("car: reset puts it at the nearest respawn point, still, with the controller at zero") {
    const rally::sim::JoltRuntime runtime;
    SimWorld world;
    world.build(rally::sim::zone_world(Surface::Gravel, reference_car()));
    world.seal();
    SimInput drive;
    drive.throttle = kAxisMax;
    drive.steer = kAxisMax / 3;
    run(world, drive, 3 * kStepsPerSecond);
    SimInput down;
    down.brake = kAxisMax;
    run(world, down, 4 * kStepsPerSecond); // parado y con el freno: ancla
    down.buttons = rally::sim::kButtonShiftDown;
    world.step(down); // a N
    REQUIRE(world.car_controller().gear() == rally::sim::Gear::Neutral);
    SimInput hold;
    hold.brake = kAxisMax;
    world.step(hold);
    bool any_anchor = false;
    for (int i = 0; i < kWheelCount; ++i) {
        any_anchor = any_anchor || world.car_controller().wheel(i).anchored;
    }
    REQUIRE(any_anchor);

    const BodyPose at = world.car_pose();
    const rally::sim::TestZone& zone = *world.zone();
    const rally::sim::RespawnPoint& target =
        zone.respawns()[static_cast<std::size_t>(zone.nearest_respawn(at.x, at.z))];
    SimInput reset = drive;
    reset.buttons = rally::sim::kButtonReset;
    world.step(reset);
    const BodyPose back = world.car_pose();
    CHECK(back.x == target.x);
    CHECK(back.z == target.z);
    CHECK(back.y == doctest::Approx(zone.ground_height(target.x, target.z)).epsilon(1e-9));
    CHECK(heading(back) == doctest::Approx(-target.yaw).epsilon(1e-6));
    const rally::sim::BodyVelocity v = world.car_velocity();
    CHECK(v.vx == 0.0f);
    CHECK(v.vy == 0.0f);
    CHECK(v.vz == 0.0f);
    CHECK(v.wx == 0.0f);
    CHECK(v.wy == 0.0f);
    CHECK(v.wz == 0.0f);
    const CarController& controller = world.car_controller();
    CHECK(controller.gear() == rally::sim::Gear::Forward);
    for (int i = 0; i < kWheelCount; ++i) {
        CHECK(controller.wheel(i).omega == 0.0f);
        CHECK_FALSE(controller.wheel(i).anchored);
        CHECK(world.car_wheel(i).rotation == 0.0f);
        CHECK(world.car_wheel(i).steer == 0.0f);
    }
    CHECK(world.last_impact() == 0.0f);
}

// --- 10. SaveState y RestoreState ------------------------------------------------------------

namespace {

// Entradas del escenario: en la rampa, freno 4 s (ancla), bajar a N, freno hasta 10 s; después
// suelta el freno y acelera con dirección (vuelve a adelante) durante 240 pasos.
SimInput ramp_script(int step) {
    SimInput input;
    if (step < 10 * kStepsPerSecond) {
        input.brake = kAxisMax;
        if (step == 4 * kStepsPerSecond) {
            input.buttons = rally::sim::kButtonShiftDown;
        }
        return input;
    }
    if (step == 10 * kStepsPerSecond + 20) {
        input.buttons = rally::sim::kButtonShiftUp;
    }
    input.throttle = kAxisMax / 2;
    input.steer = static_cast<std::int16_t>(kAxisMax / 3);
    input.brake = step < 10 * kStepsPerSecond + 60 ? kAxisMax / 2 : 0;
    return input;
}

} // namespace

TEST_CASE("car: SaveState at 10 s and RestoreState in a fresh world give the same 240 steps") {
    const rally::sim::JoltRuntime runtime;
    WorldDesc desc = rally::tests::ramp_world();
    desc.car_spawn.y = 0.25;
    constexpr int kSave = 10 * kStepsPerSecond;

    SimWorld world;
    world.build(desc);
    world.seal();
    for (int s = 0; s < kSave; ++s) {
        world.step(ramp_script(s));
    }
    // El estado guardado tiene que ser distinto del de un mundo nuevo: anclas puestas, en N.
    // Si el controlador no guardara algo de esto, la continuación restaurada se separaría.
    REQUIRE(world.car_controller().gear() == rally::sim::Gear::Neutral);
    for (int i = 0; i < kWheelCount; ++i) {
        REQUIRE(world.car_controller().wheel(i).anchored);
    }
    const std::string saved = world.save_state();
    const rally::sim::ControlState controls = world.controls();
    for (int s = kSave; s < kSave + 240; ++s) {
        world.step(ramp_script(s));
    }

    SimWorld restored;
    restored.build(desc);
    restored.seal();
    restored.restore_state(saved, controls);
    CHECK(restored.save_state() == saved);
    for (int s = kSave; s < kSave + 240; ++s) {
        restored.step(ramp_script(s));
    }
    CHECK(world.car_controller().gear() == rally::sim::Gear::Forward); // la continuación cambia
    CHECK(restored.save_state() == world.save_state());
    CHECK(restored.state_hash() == world.state_hash());
    for (int i = 0; i < kWheelCount; ++i) {
        CHECK(restored.car_wheel(i).rotation == world.car_wheel(i).rotation);
        CHECK(restored.car_controller().wheel(i).omega == world.car_controller().wheel(i).omega);
    }
}

// --- 12. Recarga -----------------------------------------------------------------------------

TEST_CASE("car: building with a heavier car file changes the car mass and nothing else") {
    const rally::sim::JoltRuntime runtime;
    const CarData heavy = rally::sim::load_car_or_abort(
        replace_line(reference_car_text(), "mass = 1100", "mass = 1300"), "heavy");
    SimWorld light_world;
    light_world.build(rally::sim::zone_world(Surface::Gravel, reference_car()));
    light_world.seal();
    SimWorld heavy_world;
    heavy_world.build(rally::sim::zone_world(Surface::Gravel, heavy));
    heavy_world.seal();
    CHECK(light_world.car_mass() == doctest::Approx(1100.0));
    CHECK(heavy_world.car_mass() == doctest::Approx(1300.0));
    CHECK(light_world.zone_hash() == heavy_world.zone_hash());
    CHECK(light_world.ground_hash() == heavy_world.ground_hash());
    const BodyPose a = light_world.car_pose();
    const BodyPose b = heavy_world.car_pose();
    CHECK(a.x == b.x);
    CHECK(a.y == b.y);
    CHECK(a.z == b.z);
}

// --- 14. Muro a 60 m/s -----------------------------------------------------------------------

TEST_CASE("car: hitting a wall at 60 m/s does not go through it") {
    const rally::sim::JoltRuntime runtime;
    WorldDesc desc = flat_world(Surface::Asphalt, false, 60.0f);
    // Muro de 1 m de grueso y 4 m de alto con su cara a 60 m por delante del coche.
    rally::sim::BoxDesc wall;
    wall.x = 0.0;
    wall.y = 2.0;
    wall.z = -60.5;
    wall.half_x = 20.0f;
    wall.half_y = 2.0f;
    wall.half_z = 0.5f;
    desc.bodies.push_back(wall);
    SimWorld world;
    world.build(desc);
    world.seal();
    double front_most = 0.0;
    float peak = 0.0f;
    for (int s = 0; s < 3 * kStepsPerSecond; ++s) {
        world.step(SimInput{});
        const BodyPose p = world.car_pose();
        front_most = std::min(front_most, p.z - 0.5 * reference_car().box_length);
        peak = std::max(peak, world.last_impact());
    }
    const BodyPose end = world.car_pose();
    MESSAGE("muro a 60 m/s: morro como mucho en z = ", front_most,
            " (cara del muro en z = -60); al final el coche está en z = ", end.z, "; impacto ",
            peak, " m/s");
    CHECK(end.z > -60.0);
    CHECK(front_most > -61.0); // no pasa al otro lado (la cara de atrás está en -61)
}

// --- 15. Impacto -----------------------------------------------------------------------------

TEST_CASE("car impact: falling from 3 m is above the rumble threshold, resting below") {
    const rally::sim::JoltRuntime runtime;
    WorldDesc desc = flat_world(Surface::Asphalt);
    desc.car_spawn.y = 3.0;
    SimWorld world;
    world.build(desc);
    world.seal();
    float in_air = 0.0f;
    for (int s = 0; s < 60; ++s) {
        world.step(SimInput{});
        in_air = std::max(in_air, world.last_impact());
    }
    float landing = 0.0f;
    for (int s = 0; s < 2 * kStepsPerSecond; ++s) {
        world.step(SimInput{});
        landing = std::max(landing, world.last_impact());
    }
    run(world, SimInput{}, 2 * kStepsPerSecond);
    float resting = 0.0f;
    for (int s = 0; s < kStepsPerSecond; ++s) {
        world.step(SimInput{});
        resting = std::max(resting, world.last_impact());
    }
    MESSAGE("impacto: en el aire ", in_air, " m/s; al caer desde 3 m ", landing, " m/s; parado ",
            resting, " m/s");
    CHECK(in_air < 0.01f);
    CHECK(landing > 1.0f);
    CHECK(resting < 0.1f);
}
