// Coche base (tarea 2.4) y neumático en el coche (tarea 2.5): lector del .car y criterios de
// aceptación de docs/prompts/fase-2/2.4.md y 2.5.md con el coche congelado
// tests/data/cars/reference.car en mundos pequeños. Las pruebas del neumático solo están en
// test_tire.cpp.

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
#include <utility>
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

constexpr std::array kAllSurfaces = {Surface::Asphalt, Surface::Dirt, Surface::Gravel,
                                     Surface::Snow, Surface::Sand};

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
    CHECK(car.gear_ratio_1 == 14.75f);
    CHECK(car.tire(Surface::Snow).mu_peak == 0.45f);
    CHECK(car.tire(Surface::Sand).slip_angle_peak == doctest::Approx(0.27925).epsilon(1e-4));
    CHECK(car.nominal_load == doctest::Approx(1100.0 * 9.81 / 4.0));
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
    CHECK(r.car.tire(Surface::Gravel).mu_peak == reference_car().tire(Surface::Gravel).mu_peak);
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

TEST_CASE("car: the equilibrium compression of each axle is g over (2 pi f) squared") {
    const rally::sim::JoltRuntime runtime;
    SimWorld world;
    world.build(flat_world(Surface::Asphalt));
    world.seal();
    run(world, SimInput{}, 3 * kStepsPerSecond);
    const CarData& car = reference_car();
    for (int i = 0; i < kWheelCount; ++i) {
        const bool front = i < 2;
        const double f = front ? car.frequency_front : car.frequency_rear;
        const double expected = kGravity / ((2.0 * kPi * f) * (2.0 * kPi * f));
        const double measured = world.car_controller().wheel(i).compression;
        MESSAGE("rueda ", i, ": compresión ", measured, " m; g/(2πf)² = ", expected, " m");
        CHECK(std::abs(measured - expected) / expected < 0.02);
    }
    const BodyPose pose = world.car_pose();
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

struct Hold {
    double moved = 0.0;
    // Pasos de los 10 s con ω exactamente 0, por rueda.
    std::array<int, kWheelCount> locked_steps{};
};

Hold hold_on_ramp(const SimInput& input, double grade = rally::tests::kRampGrade,
                  Surface surface = Surface::Asphalt) {
    // En N (2.6): mide el neumático y el freno, sin el motor.
    WorldDesc desc = rally::tests::in_neutral(rally::tests::ramp_world(grade, surface));
    desc.car_spawn.y = 0.25; // sale por encima y cae sobre la rampa
    SimWorld world;
    world.build(desc);
    world.seal();
    run(world, input, 3 * kStepsPerSecond); // se asienta
    const BodyPose start = world.car_pose();
    Hold hold;
    for (int s = 0; s < 10 * kStepsPerSecond; ++s) {
        world.step(input);
        for (int i = 0; i < kWheelCount; ++i) {
            hold.locked_steps[static_cast<std::size_t>(i)] +=
                world.car_controller().wheel(i).omega == 0.0f ? 1 : 0;
        }
    }
    for (int i = 0; i < kWheelCount; ++i) {
        const WheelOutput& w = world.car_controller().wheel(i);
        MESSAGE("rueda ", i, ": ω = 0 en ", hold.locked_steps[static_cast<std::size_t>(i)],
                " de 2400 pasos; carga ", w.load, " N; u ", w.u * 1000.0, " mm");
    }
    hold.moved = moved(start, world.car_pose());
    return hold;
}

} // namespace

TEST_CASE("car: parked on the 15 percent ramp it moves less than 1 cm in 10 s on five surfaces") {
    const rally::sim::JoltRuntime runtime;
    // Parado, la rigidez de la huella es B·C·D/σ de cada superficie: en grava y arena, de 3 a 6
    // veces más blanda que en asfalto.
    for (const Surface surface : kAllSurfaces) {
        const Hold with_brake = hold_on_ramp(pedal(0, kAxisMax), rally::tests::kRampGrade, surface);
        const Hold with_handbrake = hold_on_ramp(pedal(0, 0, rally::sim::kButtonHandbrake),
                                                 rally::tests::kRampGrade, surface);
        MESSAGE(std::string(rally::sim::surface_name(surface)),
                ": en la rampa del 15 % en 10 s, con el freno ", with_brake.moved * 1000.0,
                " mm; con el freno de mano ", with_handbrake.moved * 1000.0, " mm");
        CHECK(with_brake.moved < 0.01);
        CHECK(with_handbrake.moved < 0.01);
        // Con el freno, las cuatro ruedas quedan bloqueadas por fricción estática: ω = 0 exacto.
        for (const int locked : with_brake.locked_steps) {
            CHECK(locked == 10 * kStepsPerSecond);
        }
    }
}

TEST_CASE("car: without brakes it stays on a 1 percent slope and rolls down a 3 percent one") {
    const rally::sim::JoltRuntime runtime;
    const Hold one = hold_on_ramp(SimInput{}, 0.01);
    const Hold three = hold_on_ramp(SimInput{}, 0.03);
    MESSAGE("sin freno en 10 s: al 1 % ", one.moved * 1000.0, " mm; al 3 % ", three.moved,
            " m (la rodadura sujeta hasta el 1,5 %)");
    CHECK(one.moved < 0.001);
    CHECK(three.moved > 0.1);
    // La rodadura es un par de Coulomb: en arena (c_rr = 0,15) sujeta el coche sin freno en
    // pendientes de hasta el 14 % (en el 15 % rueda). Se revisa en la 2.10
    // (2.5-respuesta-plan §4.7).
    std::string sand;
    for (const double grade : {0.10, 0.12, 0.13, 0.14, 0.15}) {
        const Hold hold = hold_on_ramp(SimInput{}, grade, Surface::Sand);
        sand += std::to_string(static_cast<int>(grade * 100.0 + 0.5)) +
                " %: " + std::to_string(hold.moved * 1000.0) + " mm; ";
        if (grade <= 0.14) {
            CHECK(hold.moved < 0.01);
        }
    }
    MESSAGE("sin freno en arena en 10 s: ", sand);
}

TEST_CASE("car: released on the ramp it rolls back") {
    const rally::sim::JoltRuntime runtime;
    WorldDesc desc = rally::tests::in_neutral(rally::tests::ramp_world());
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

TEST_CASE(
    "car: on flat ground with no input for 180 s it moves less than 0.01 mm and does not turn") {
    const rally::sim::JoltRuntime runtime;
    SimWorld world;
    world.build(rally::tests::in_neutral(flat_world(Surface::Asphalt, true)));
    world.seal();
    run(world, SimInput{}, 3 * kStepsPerSecond);
    const BodyPose start = world.car_pose();
    // Asentamiento desde el equilibrio a 1, 10, 30, 60 y 180 s. Con la zona muerta del giro de
    // Jolt sin tratar, el coche se trasladaba sobre la huella: 0,365 mm a 60 s en la 2.4 y
    // 1,45 mm a 180 s con la Pacejka (car_controller.cpp; 2.5-respuesta-1 §3). Regresión.
    std::string settling;
    int elapsed = 0;
    for (const int mark : {1, 10, 30, 60, 180}) {
        run(world, SimInput{}, (mark - elapsed) * kStepsPerSecond);
        elapsed = mark;
        const BodyPose now = world.car_pose();
        settling += std::to_string(mark) + " s: " + std::to_string(moved(start, now) * 1000.0) +
                    " mm (x " + std::to_string((now.x - start.x) * 1000.0) + ", z " +
                    std::to_string((now.z - start.z) * 1000.0) + "); ";
    }
    const BodyPose end = world.car_pose();
    const rally::sim::BodyVelocity v = world.car_velocity();
    const double turned = std::abs(heading(end) - heading(start)) * 180.0 / kPi;
    MESSAGE("sin entradas: ", settling, "|v| final ",
            std::sqrt(static_cast<double>(v.vx * v.vx + v.vy * v.vy + v.vz * v.vz)) * 1e6,
            " µm/s; giro ", turned, "°");
    CHECK(moved(start, end) < 0.00001);
    CHECK(turned < 0.01);
}

// --- 5. Barrido de estabilidad de las ruedas -------------------------------------------------

namespace {

enum class Case { Throttle, Brake, Neutral };

struct SweepResult {
    int sign_changes = 0;        // máximo de las cuatro ruedas en el último segundo, girando
    double kappa_std = 0.0;      // máximo de las cuatro ruedas en el último segundo
    int anchored_steps = 0;      // pasos × ruedas bloqueadas (ω = 0) en el último segundo
    double anchored_speed = 0.0; // |v| máxima en el contacto con la rueda bloqueada
    bool stopped = false;        // el coche está parado (< 1 cm/s) al empezar el último segundo
};

SweepResult sweep_case(Surface surface, float speed, Case c, bool explicit_wheels) {
    WorldDesc desc = flat_world(surface, false, speed);
    desc.explicit_wheels = explicit_wheels;
    // 2.6: con la marcha que da la velocidad y la caja secuencial, que sin botones no cambia: el
    // barrido mide la estabilidad del giro de las ruedas, no los cambios (criterio 10 de la
    // 2.6, aparte).
    desc.aids.gearbox = rally::sim::GearboxMode::Sequential;
    SimWorld world;
    world.build(desc);
    world.seal();
    const SimInput input = c == Case::Throttle ? pedal(kAxisMax, 0)
                           : c == Case::Brake  ? pedal(0, kAxisMax)
                                               : SimInput{};
    run(world, input, kStepsPerSecond);
    const rally::sim::BodyVelocity start = world.car_velocity();
    const bool stopped =
        std::sqrt(static_cast<double>(start.vx * start.vx + start.vz * start.vz)) < 0.01;
    std::array<int, kWheelCount> changes{};
    std::array<int, kWheelCount> last_sign{};
    std::array<double, kWheelCount> sum{};
    std::array<double, kWheelCount> sum_sq{};
    SweepResult result;
    result.stopped = stopped;
    for (int s = 0; s < kStepsPerSecond; ++s) {
        world.step(input);
        for (int i = 0; i < kWheelCount; ++i) {
            const auto k = static_cast<std::size_t>(i);
            const WheelOutput& w = world.car_controller().wheel(i);
            const double kappa = w.kappa;
            if (w.locked) {
                // Rueda bloqueada por fricción estática: ω = 0, sin grado de libertad que pueda
                // oscilar. Sus pasos no cuentan para el signo; se mide que el contacto esté
                // quieto.
                ++result.anchored_steps;
                result.anchored_speed =
                    std::max(result.anchored_speed, static_cast<double>(w.contact_speed));
                last_sign[k] = 0;
                continue;
            }
            const double slip = w.slip_x; // ω·r − v
            const int sign = slip > 0.0 ? 1 : (slip < 0.0 ? -1 : 0);
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

std::string sweep_table(bool explicit_wheels, int& worst_changes, double& worst_std,
                        double& worst_anchored_speed) {
    std::string table = "superficie | v (m/s) | acelerador | freno (pasos bloqueados, |v| máx "
                        "bloqueada) | punto muerto (σκ)\n";
    worst_changes = 0;
    worst_std = 0.0;
    worst_anchored_speed = 0.0;
    for (const Surface surface : kAllSurfaces) {
        for (const float speed : kSweepSpeeds) {
            const SweepResult t = sweep_case(surface, speed, Case::Throttle, explicit_wheels);
            const SweepResult b = sweep_case(surface, speed, Case::Brake, explicit_wheels);
            const SweepResult n = sweep_case(surface, speed, Case::Neutral, explicit_wheels);
            char row[200];
            std::snprintf(row, sizeof(row), "%s | %.1f | %d | %d (%d, %.4f m/s) | %d (%.5f)\n",
                          rally::sim::surface_name(surface), static_cast<double>(speed),
                          t.sign_changes, b.sign_changes, b.anchored_steps, b.anchored_speed,
                          n.sign_changes, n.kappa_std);
            // Parado al empezar el último segundo y con las cuatro ruedas bloqueadas los 240
            // pasos: el cabeceo tras la frenada se tiene que haber apagado. A más velocidad las
            // ruedas también se bloquean, pero patinando (frenada con bloqueo): no es esto.
            if (b.stopped && b.anchored_steps == kWheelCount * kStepsPerSecond) {
                worst_anchored_speed = std::max(worst_anchored_speed, b.anchored_speed);
            }
            table += row;
            worst_changes =
                std::max({worst_changes, t.sign_changes, b.sign_changes, n.sign_changes});
            worst_std = std::max(worst_std, n.kappa_std);
        }
    }
    return table;
}

} // namespace

TEST_CASE("car: the wheels are stable from 0.5 to 60 m/s on the five surfaces") {
    const rally::sim::JoltRuntime runtime;
    int changes = 0;
    double kappa_std = 0.0;
    double anchored_speed = 0.0;
    const std::string table = sweep_table(false, changes, kappa_std, anchored_speed);
    MESSAGE("barrido semi-implícito (cambios de signo de ω·r − v en el último segundo con la "
            "rueda girando, máximo de las cuatro ruedas):\n",
            table);
    CHECK(changes <= 2);
    CHECK(kappa_std < 0.02);
    // Parado y bloqueado, el cabeceo tras la frenada se apaga: el contacto queda quieto.
    CHECK(anchored_speed < 0.01);

    // Referencia, sin juzgar: la integración explícita.
    int explicit_changes = 0;
    double explicit_std = 0.0;
    double explicit_speed = 0.0;
    const std::string reference = sweep_table(true, explicit_changes, explicit_std, explicit_speed);
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
    // 2.6: en punto muerto (rueda libre, como antes del tren motriz): mide la suspensión.
    return rally::tests::in_neutral(desc);
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
    world.build(rally::tests::in_neutral(
        flat_world(Surface::Asphalt, false, static_cast<float>(v0 + 0.45))));
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
                         car.tire(Surface::Asphalt).rolling_resistance * car.mass * kGravity;
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
    // Parado y con el freno: ruedas bloqueadas (2.6: con el motor real llega a ~60 km/h; 6 s).
    run(world, down, 6 * kStepsPerSecond);
    down.buttons = rally::sim::kButtonShiftDown;
    world.step(down); // a N, tras el tiempo del cambio
    down.buttons = 0;
    run(world, down, reference_car().shift_steps);
    REQUIRE(world.car_controller().gear() == rally::sim::kNeutralGear);
    SimInput hold;
    hold.brake = kAxisMax;
    hold.steer = kAxisMax; // con la dirección girada, la huella se deforma de lado
    run(world, hold, kStepsPerSecond / 2);
    bool any_deformed = false;
    for (int i = 0; i < kWheelCount; ++i) {
        const WheelOutput& w = world.car_controller().wheel(i);
        any_deformed = any_deformed || w.u != 0.0f || w.w != 0.0f;
    }
    REQUIRE(any_deformed);

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
    CHECK(controller.gear() == 1);
    for (int i = 0; i < kWheelCount; ++i) {
        CHECK(controller.wheel(i).omega == 0.0f);
        CHECK(controller.wheel(i).u == 0.0f);
        CHECK(controller.wheel(i).w == 0.0f);
        CHECK(world.car_wheel(i).rotation == 0.0f);
        CHECK(world.car_wheel(i).steer == 0.0f);
    }
    CHECK(world.last_impact() == 0.0f);
}

// --- 10. SaveState y RestoreState ------------------------------------------------------------

namespace {

// Entradas del escenario: en la rampa, freno 4 s (bloqueo), bajar a N, freno hasta 10 s; después
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
    // El estado guardado tiene que ser distinto del de un mundo nuevo: en N y con la huella
    // deformada (sujeta el coche en la rampa). Si el controlador no guardara algo de esto, la
    // continuación restaurada se separaría.
    REQUIRE(world.car_controller().gear() == rally::sim::kNeutralGear);
    for (int i = 0; i < kWheelCount; ++i) {
        REQUIRE(world.car_controller().wheel(i).u != 0.0f);
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
    CHECK(world.car_controller().gear() == 1); // la continuación cambia
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

TEST_CASE("car: in the air the footprint is undeformed and landing gives no force jump") {
    const rally::sim::JoltRuntime runtime;
    for (const Surface surface : {Surface::Asphalt, Surface::Gravel}) {
        // 2.6: en N; el aterrizaje con el embrague cerrado tiene su test.
        WorldDesc desc = rally::tests::in_neutral(flat_world(surface, false, 10.0f));
        desc.car_spawn.y = 0.6; // cae desde 0,6 m rodando a 10 m/s
        SimWorld world;
        world.build(desc);
        world.seal();
        SimInput steer;
        steer.steer = kAxisMax / 4;
        int airborne = 0;
        // En el paso del aterrizaje de cada rueda, su fuerza sale de la deformación que acumula ese
        // paso desde u = w = 0: una fracción del límite, no un salto. Se mide como fracción de
        // μ·F_z.
        double worst_landing = 0.0;
        std::array<bool, kWheelCount> was_airborne{};
        for (int s = 0; s < 2 * kStepsPerSecond; ++s) {
            world.step(steer);
            for (int i = 0; i < kWheelCount; ++i) {
                const auto k = static_cast<std::size_t>(i);
                const WheelOutput& w = world.car_controller().wheel(i);
                if (!w.contact) {
                    ++airborne;
                    was_airborne[k] = true;
                    CHECK(w.u == 0.0f);
                    CHECK(w.w == 0.0f);
                    CHECK(w.fx == 0.0f);
                    CHECK(w.fy == 0.0f);
                } else if (was_airborne[k] && w.load > 0.0f) {
                    // Primer paso con carga (en el primero con contacto aún es la del paso
                    // anterior, 0).
                    was_airborne[k] = false;
                    const double force = std::sqrt(static_cast<double>(w.fx * w.fx + w.fy * w.fy));
                    const double fraction = force / (w.mu * w.load);
                    MESSAGE("rueda ", i, " aterriza: F_z ", w.load, " N, fuerza ", force, " N (",
                            100.0 * fraction, " % de μ·F_z)");
                    worst_landing = std::max(worst_landing, fraction);
                }
            }
        }
        MESSAGE(std::string(rally::sim::surface_name(surface)),
                ": pasos × ruedas en el aire: ", airborne);
        CHECK(airborne > 0);
        // Un paso a 10 m/s son 4 cm: el 10 % de σ_y y el 28 % de σ_x en asfalto (menos en grava).
        CHECK(worst_landing < 0.3);
    }
}

TEST_CASE("car: reset clears the dynamic state and keeps the wheel condition") {
    const rally::sim::JoltRuntime runtime;
    SimWorld world;
    world.build(flat_world(Surface::Asphalt));
    world.seal();
    world.set_wheel_radius(rally::sim::kFrontLeft, 0.29f); // condición distinta de la nominal
    SimInput drive;
    drive.throttle = kAxisMax;
    drive.steer = kAxisMax / 2;
    run(world, drive, kStepsPerSecond);
    REQUIRE(world.car_controller().wheel(0).omega != 0.0f);
    SimInput reset;
    reset.buttons = rally::sim::kButtonReset;
    world.step(reset);
    const CarController& c = world.car_controller();
    CHECK(c.gear() == 1);
    for (int i = 0; i < kWheelCount; ++i) {
        CHECK(c.wheel(i).omega == 0.0f);
        CHECK(c.wheel(i).u == 0.0f);
        CHECK(c.wheel(i).w == 0.0f);
        CHECK(world.car_wheel(i).rotation == 0.0f);
    }
    CHECK(c.wheel_radius(rally::sim::kFrontLeft) == 0.29f);
    CHECK(world.car_wheel(rally::sim::kFrontLeft).radius == 0.29f);
    CHECK(c.wheel_radius(rally::sim::kFrontRight) == reference_car().wheel_radius);
    // La condición va en el estado: otro radio da otro estado.
    SimWorld other;
    other.build(flat_world(Surface::Asphalt));
    other.seal();
    const std::string nominal = other.save_state();
    other.set_wheel_radius(rally::sim::kFrontLeft, 0.29f);
    CHECK(other.save_state() != nominal);
}

// --- Neumático en el coche (tarea 2.5) -------------------------------------------------------

namespace {

double horizontal_speed(const SimWorld& world) {
    const rally::sim::BodyVelocity v = world.car_velocity();
    return std::sqrt(static_cast<double>(v.vx * v.vx + v.vz * v.vz));
}

double horizontal_distance(const BodyPose& a, const BodyPose& b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z));
}

SimInput axes_input(double steer, double throttle, double brake, std::uint16_t buttons = 0) {
    SimInput input;
    input.steer = static_cast<std::int16_t>(std::clamp(steer, -1.0, 1.0) * kAxisMax);
    input.throttle = static_cast<std::int16_t>(std::clamp(throttle, 0.0, 1.0) * kAxisMax);
    input.brake = static_cast<std::int16_t>(std::clamp(brake, 0.0, 1.0) * kAxisMax);
    input.buttons = buttons;
    return input;
}

// Controlador de velocidad solo para los tests: PI sobre la velocidad horizontal.
struct SpeedHold {
    double target = 0.0; // m/s
    double integral = 0.0;

    double throttle(const SimWorld& world) {
        const double error = target - horizontal_speed(world);
        integral = std::clamp(integral + error / kStepsPerSecond, -2.0, 2.0);
        return std::clamp(0.5 * error + 0.5 * integral, 0.0, 1.0);
    }
};

// Ángulo de deriva relajado medio de un eje, con el signo de beta (grados).
double axle_slip_deg(const SimWorld& world, int a, int b) {
    const CarController& c = world.car_controller();
    return -0.5 * (c.wheel(a).alpha + c.wheel(b).alpha) * 180.0 / kPi;
}

std::string name(Surface surface) { return rally::sim::surface_name(surface); }

} // namespace

// Criterio 5.
TEST_CASE("car: locked-wheel braking from 100 km/h stops in v squared over 2 mu slide g") {
    const rally::sim::JoltRuntime runtime;
    const double v0 = 100.0 / 3.6;
    std::string table;
    for (const Surface surface : kAllSurfaces) {
        const rally::sim::TireSurface& t = reference_car().tire(surface);
        SimWorld world;
        world.build(rally::tests::in_neutral(flat_world(surface, false, static_cast<float>(v0))));
        world.seal();
        const BodyPose start = world.car_pose();
        int steps = 0;
        int locked = 0;
        while (horizontal_speed(world) > 0.05 && steps < 30 * kStepsPerSecond) {
            world.step(pedal(0, kAxisMax, rally::sim::kButtonHandbrake));
            ++steps;
            bool all = true;
            for (int i = 0; i < kWheelCount; ++i) {
                all = all && world.car_controller().wheel(i).omega == 0.0f;
            }
            locked += all ? 1 : 0;
        }
        const double distance = horizontal_distance(start, world.car_pose());
        const double expected = v0 * v0 / (2.0 * t.mu_peak * t.slide_ratio * kGravity);
        char row[160];
        std::snprintf(row, sizeof(row),
                      "%s: %.1f m (esperado %.1f m, %+.1f %%); cuatro ruedas bloqueadas en el "
                      "%.0f %% de los pasos\n",
                      name(surface).c_str(), distance, expected,
                      100.0 * (distance / expected - 1.0), 100.0 * locked / std::max(steps, 1));
        table += row;
        CHECK(std::abs(distance / expected - 1.0) <= 0.15);
    }
    MESSAGE("frenada con las ruedas bloqueadas desde 100 km/h:\n", table);
}

// Criterio 6.
namespace {

struct ThresholdStop {
    double distance = 0.0;
    std::array<double, kWheelCount> kappa{}; // κ_r / κ pico medio por rueda
    double level = 0.0;                      // freno medio
};

// Frenada desde `v0` con el controlador de deslizamiento solo para el test: el freno total
// sube o baja para que el |κ_r| mayor de las cuatro ruedas se quede en κ pico.
constexpr double kThresholdGain = 0.008;

ThresholdStop threshold_stop(Surface surface, double v0, double front_share) {
    const rally::sim::TireSurface& t = reference_car().tire(surface);
    WorldDesc desc = rally::tests::in_neutral(flat_world(surface, false, static_cast<float>(v0)));
    desc.car_data.brake_front_share = static_cast<float>(front_share);
    SimWorld world;
    world.build(desc);
    world.seal();
    const BodyPose start = world.car_pose();
    double level = 0.5;
    ThresholdStop stop;
    int steps = 0;
    while (horizontal_speed(world) > 0.3 && steps < 30 * kStepsPerSecond) {
        double worst = 0.0;
        for (int i = 0; i < kWheelCount; ++i) {
            worst = std::max(worst, -static_cast<double>(world.car_controller().wheel(i).kappa));
        }
        level = std::clamp(level + kThresholdGain * (t.slip_ratio_peak - worst) / t.slip_ratio_peak,
                           0.0, 1.0);
        world.step(axes_input(0.0, 0.0, level));
        for (int i = 0; i < kWheelCount; ++i) {
            stop.kappa[static_cast<std::size_t>(i)] +=
                -world.car_controller().wheel(i).kappa / t.slip_ratio_peak;
        }
        stop.level += level;
        ++steps;
    }
    for (double& k : stop.kappa) {
        k /= std::max(steps, 1);
    }
    stop.level /= std::max(steps, 1);
    stop.distance = horizontal_distance(start, world.car_pose());
    return stop;
}

} // namespace

TEST_CASE("car: threshold braking from 100 km/h stops in v squared over 2 mu peak g") {
    const rally::sim::JoltRuntime runtime;
    const double v0 = 100.0 / 3.6;
    std::string table;
    for (const Surface surface : kAllSurfaces) {
        const rally::sim::TireSurface& t = reference_car().tire(surface);
        // Reparto de freno ideal para esta superficie, solo en la copia del coche del test: el
        // que da la frenada más corta, con los dos ejes a la vez en el pico (2.5-respuesta-plan
        // §4.5). El criterio mide el neumático, no el reparto. Búsqueda de la sección áurea en
        // [0,4, 0,9].
        double lo = 0.4;
        double hi = 0.9;
        constexpr double kGolden = 0.6180339887498949;
        double a = hi - kGolden * (hi - lo);
        double b = lo + kGolden * (hi - lo);
        ThresholdStop sa = threshold_stop(surface, v0, a);
        ThresholdStop sb = threshold_stop(surface, v0, b);
        for (int i = 0; i < 10; ++i) {
            if (sa.distance < sb.distance) {
                hi = b;
                b = a;
                sb = sa;
                a = hi - kGolden * (hi - lo);
                sa = threshold_stop(surface, v0, a);
            } else {
                lo = a;
                a = b;
                sa = sb;
                b = lo + kGolden * (hi - lo);
                sb = threshold_stop(surface, v0, b);
            }
        }
        const ThresholdStop& best = sa.distance < sb.distance ? sa : sb;
        const double share = sa.distance < sb.distance ? a : b;
        const double distance = best.distance;
        // La rodadura es un par de Coulomb sobre la rueda (decisión del 2026-10-04): frenando al
        // límite no se suma a la fricción, porque la fuerza en el suelo la limita μ·F_z. Lo que
        // sí sumaría en arena es el empuje contra la arena acumulada al hundirse, fuera del
        // alcance (2.5-respuesta-1 §1). Por eso el esperado es v²/(2·μ pico·g), no con μ + c_rr.
        const double expected = v0 * v0 / (2.0 * t.mu_peak * kGravity);
        const double with_rolling = v0 * v0 / (2.0 * (t.mu_peak + t.rolling_resistance) * kGravity);
        char row[240];
        std::snprintf(row, sizeof(row),
                      "%s: %.1f m (esperado %.1f m, %+.1f %%; con μ pico + c_rr serían %.1f m); "
                      "reparto delantero %.3f; κ_r/κ pico medio %.2f %.2f %.2f %.2f\n",
                      name(surface).c_str(), distance, expected,
                      100.0 * (distance / expected - 1.0), with_rolling, share, best.kappa[0],
                      best.kappa[1], best.kappa[2], best.kappa[3]);
        table += row;
        CHECK(std::abs(distance / expected - 1.0) <= 0.15);
    }
    MESSAGE("frenada en el umbral desde 100 km/h (reparto ideal por superficie):\n", table);
}

// Criterio 7.
TEST_CASE("car: maximum lateral acceleration at 60 km/h with a 2 degrees per second steer ramp") {
    const rally::sim::JoltRuntime runtime;
    const CarData& car = reference_car();
    struct Range {
        Surface surface;
        double low;
        double high;
    };
    const std::array<Range, 5> ranges = {
        Range{Surface::Asphalt, 0.95, 1.15}, Range{Surface::Dirt, 0.68, 0.82},
        Range{Surface::Gravel, 0.62, 0.75}, Range{Surface::Snow, 0.38, 0.47},
        Range{Surface::Sand, 0.42, 0.52}};
    std::string table;
    for (const Range& range : ranges) {
        SimWorld world;
        // 2.6: con el tren motriz abierto (el acelerador sujeta los 60 km/h).
        WorldDesc desc =
            rally::tests::open_driveline(flat_world(range.surface, false, 60.0f / 3.6f));
        desc.bodies[0].half_x = 300.0f; // el círculo de ~30 m de radio cabe entero
        world.build(desc);
        world.seal();
        world.set_telemetry(true);
        SpeedHold hold{60.0 / 3.6};
        constexpr int kWindow = kStepsPerSecond / 4; // media de 0,25 s
        std::array<double, kWindow> window{};
        double window_sum = 0.0;
        double best = 0.0;
        double best_front = 0.0;
        double best_rear = 0.0;
        double best_kappa = 0.0;
        double best_share = 0.0;
        double best_throttle = 0.0;
        double best_angle = 0.0;
        for (int s = 0; s < 15 * kStepsPerSecond; ++s) {
            const double angle = 2.0 * kPi / 180.0 * s / kStepsPerSecond; // rueda interior
            if (angle > car.max_steer) {
                break;
            }
            const double throttle = hold.throttle(world);
            world.step(axes_input(angle / car.max_steer, throttle, 0.0));
            const rally::sim::CarTelemetry& t = world.telemetry();
            const double speed = std::sqrt(static_cast<double>(t.vx * t.vx + t.vz * t.vz));
            // Aceleración normal a la trayectoria, en el plano.
            const double a_lat =
                speed > 1.0 ? std::abs(static_cast<double>(t.vx * t.az - t.vz * t.ax)) / speed
                            : 0.0;
            const std::size_t k = static_cast<std::size_t>(s % kWindow);
            window_sum += a_lat - window[k];
            window[k] = a_lat;
            if (s >= kWindow && window_sum / kWindow > best) {
                best = window_sum / kWindow;
                best_front = axle_slip_deg(world, 0, 1);
                best_rear = axle_slip_deg(world, 2, 3);
                const WheelOutput& rl = world.car_controller().wheel(rally::sim::kRearLeft);
                const WheelOutput& rr = world.car_controller().wheel(rally::sim::kRearRight);
                best_kappa = 0.5 * (rl.kappa + rr.kappa);
                // Parte de la lateral que deja la combinada en el eje trasero.
                const double pure = std::abs(rl.fy_pure) + std::abs(rr.fy_pure);
                best_share = pure > 0.0 ? (std::abs(rl.fy) + std::abs(rr.fy)) / pure : 1.0;
                best_throttle = throttle;
                best_angle = angle * 180.0 / kPi;
            }
            if (std::abs(t.beta) > 45.0 * kPi / 180.0) {
                break; // trompo
            }
        }
        const double g_max = best / kGravity;
        char row[240];
        std::snprintf(row, sizeof(row),
                      "%s: %.3f g (rango %.2f–%.2f) con la dirección a %.1f°; deriva delantera "
                      "%.1f°, trasera %.1f°; κ_r trasero %.3f; lateral trasera / pura %.2f; "
                      "acelerador %.2f\n",
                      name(range.surface).c_str(), g_max, range.low, range.high, best_angle,
                      best_front, best_rear, best_kappa, best_share, best_throttle);
        table += row;
        CHECK(g_max >= range.low);
        CHECK(g_max <= range.high);
    }
    MESSAGE("aceleración lateral máxima a 60 km/h (media de 0,25 s):\n", table);
}

// Criterio 8.
TEST_CASE("car: coasting at 50 km/h decelerates by rolling resistance and drag on each surface") {
    const rally::sim::JoltRuntime runtime;
    const CarData& car = reference_car();
    const double v0 = 50.0 / 3.6;
    std::string table;
    for (const Surface surface : kAllSurfaces) {
        const double c_rr = car.tire(surface).rolling_resistance;
        SimWorld world;
        // Sale algo más rápido para que la media de la medida quede en 50 km/h.
        world.build(rally::tests::in_neutral(flat_world(
            surface, false, static_cast<float>(v0 + 0.5 * (c_rr * kGravity + 0.1) + 0.2))));
        world.seal();
        run(world, SimInput{}, kStepsPerSecond / 2);
        const double before = forward_speed(world);
        double sum_v2 = 0.0;
        for (int s = 0; s < kStepsPerSecond; ++s) {
            world.step(SimInput{});
            const double v = forward_speed(world);
            sum_v2 += v * v;
        }
        const double measured = before - forward_speed(world); // en 1 s
        const double mean_v2 = sum_v2 / kStepsPerSecond;
        const double force =
            0.5 * rally::sim::kAirDensity * car.cda * mean_v2 + c_rr * car.mass * kGravity;
        // Las ruedas también se frenan: masa equivalente m + ΣI/r², como en la deceleración
        // por el aire de la 2.4 (2.5-respuesta-plan §4.4). Sin ella el valor es un 4,5 % menor.
        const double effective_mass =
            car.mass + kWheelCount * car.wheel_inertia / (car.wheel_radius * car.wheel_radius);
        const double expected = force / effective_mass;
        char row[200];
        std::snprintf(row, sizeof(row),
                      "%s a %.1f km/h: %.4f m/s² (%.4f g); esperado %.4f m/s² (%+.1f %%)\n",
                      name(surface).c_str(), std::sqrt(mean_v2) * 3.6, measured,
                      measured / kGravity, expected, 100.0 * (measured / expected - 1.0));
        table += row;
        CHECK(std::abs(measured / expected - 1.0) <= 0.05);
        if (surface == Surface::Sand) {
            CHECK(measured / kGravity >= 0.1);
        }
    }
    MESSAGE("deceleración en punto muerto (sin acelerador):\n", table);
}

// Criterio 9.
TEST_CASE("car: astride asphalt and gravel each wheel reports the surface it stands on") {
    const rally::sim::JoltRuntime runtime;
    SimWorld world;
    world.build(rally::tests::split_world(Surface::Asphalt, Surface::Gravel));
    world.seal();
    world.set_telemetry(true);
    run(world, SimInput{}, kStepsPerSecond);
    const CarController& c = world.car_controller();
    const rally::sim::CarTelemetry& t = world.telemetry();
    for (int i = 0; i < kWheelCount; ++i) {
        const bool left = i == rally::sim::kFrontLeft || i == rally::sim::kRearLeft;
        const Surface expected = left ? Surface::Asphalt : Surface::Gravel;
        const WheelOutput& w = c.wheel(i);
        MESSAGE("rueda ", i, ": ", name(w.surface), ", μ efectivo ", w.mu);
        CHECK(w.surface == expected);
        CHECK(t.wheels[static_cast<std::size_t>(i)].surface == static_cast<std::int8_t>(expected));
        CHECK(w.mu == doctest::Approx(rally::sim::peak_mu(reference_car().tire(expected), w.load,
                                                          reference_car().nominal_load)));
    }
}

// Régimen de arranque (2.5-respuesta-plan §1).
TEST_CASE("car: launching at full throttle from rest goes straight with no wheel reversal") {
    const rally::sim::JoltRuntime runtime;
    std::string table;
    for (const Surface surface : kAllSurfaces) {
        const rally::sim::TireSurface& t = reference_car().tire(surface);
        SimWorld world;
        world.build(flat_world(surface));
        world.seal();
        run(world, SimInput{}, kStepsPerSecond); // se asienta
        const BodyPose start = world.car_pose();
        double lowest_omega = 0.0;
        // 2.6: la ventana pasa del primer medio segundo al primer segundo: el embrague patina
        // mientras el motor sube a las rpm del arranque (~0,6 s) y el par llega con él.
        double best_kappa = 0.0;  // κ_r mayor de las traseras en el primer segundo
        double worst_curve = 0.0; // |F − curva| / D trasera al final del primer segundo
        double share = 0.0;       // F_x / D trasera al final del primer segundo
        for (int s = 0; s < 3 * kStepsPerSecond; ++s) {
            world.step(pedal(kAxisMax, 0));
            const CarController& c = world.car_controller();
            for (int i = 0; i < kWheelCount; ++i) {
                lowest_omega = std::min(lowest_omega, static_cast<double>(c.wheel(i).omega));
            }
            if (s >= kStepsPerSecond) {
                continue;
            }
            for (const int i : {rally::sim::kRearLeft, rally::sim::kRearRight}) {
                const WheelOutput& w = c.wheel(i);
                best_kappa = std::max(best_kappa, static_cast<double>(w.kappa));
                const double d = w.mu * w.load;
                if (s == kStepsPerSecond - 1) {
                    // La fuerza es la media de los 4 subpasos y κ_r el del final del paso: se
                    // comparan al final del medio segundo, con κ_r ya casi quieto.
                    const rally::sim::TireForce curve = rally::sim::tire_force(
                        t, w.kappa, std::tan(w.alpha), w.load, reference_car().nominal_load);
                    worst_curve =
                        std::max(worst_curve, std::hypot(w.fx - curve.fx, w.fy - curve.fy) / d);
                    share = std::max(share, w.fx / d);
                }
            }
        }
        const BodyPose end = world.car_pose();
        const Axes a = axes(start);
        // Desvío lateral respecto a la recta de salida y giro.
        const double dx = end.x - start.x;
        const double dz = end.z - start.z;
        const double lateral = std::abs(dx * a.rx + dz * a.rz);
        const double turned = std::abs(heading(end) - heading(start)) * 180.0 / kPi;
        char row[220];
        std::snprintf(row, sizeof(row),
                      "%s: %.1f m en 3 s, desvío %.1f mm, giro %.3f°, ω mínima %.3f rad/s; "
                      "primer segundo: κ_r trasero máximo %.2f (%.1f × κ pico), F_x/D trasera al "
                      "final %.3f, |F − curva| máx %.4f D\n",
                      name(surface).c_str(), horizontal_distance(start, end), lateral * 1000.0,
                      turned, lowest_omega, best_kappa, best_kappa / t.slip_ratio_peak, share,
                      worst_curve);
        table += row;
        CHECK(lateral < 0.05);
        CHECK(turned < 0.5);
        CHECK(lowest_omega >= 0.0);
        if (surface == Surface::Gravel || surface == Surface::Sand) {
            // La rueda que patina desliza: pasa del pico y da la fuerza de la curva en su
            // κ_r, no la del pico (2.5-respuesta-plan §1).
            CHECK(best_kappa > t.slip_ratio_peak);
            CHECK(worst_curve <= 0.01);
            CHECK(share < 0.99);
        }
    }
    MESSAGE("salida con el acelerador a fondo:\n", table);
}

// Régimen de baja velocidad: sin serpenteo. El modo es la guiñada del coche sobre la rigidez
// lateral de la huella (4,5 Hz en asfalto, 2,4 Hz en grava). ζ por decremento logarítmico entre
// los dos primeros extremos libres de la guiñada tras soltar la dirección (el segundo y el
// tercero: el primero es el final del pulso), separados medio ciclo:
//   δ = ln|r₁/r₂| y ζ = δ/√(π² + δ²).
// Sin un segundo extremo libre, el modo no oscila (ζ ≥ 1). Exige ζ ≥ 0,5 hasta 20 km/h, dentro de
// la rampa del amortiguamiento de la carcasa, y ζ ≥ 0,4 a 30 y 50 km/h, donde solo amortigua la
// relajación (2.5-respuesta-2 §2). Los cambios de signo con |r| > 0,2°/s son solo un dato.
TEST_CASE(
    "car: a steer pulse from 5 to 50 km/h dies out with damping ratio 0.5 or 0.4 above the ramp") {
    const rally::sim::JoltRuntime runtime;
    const CarData& car = reference_car();
    const double threshold = 0.2 * kPi / 180.0; // rad/s
    std::string table;
    for (const Surface surface : kAllSurfaces) {
        for (const double kmh : {5.0, 10.0, 20.0, 30.0, 50.0}) {
            SimWorld world;
            // 2.6: en N, sin el acelerador que sujetaba la velocidad. Sale más rápido lo que la
            // rodadura le quita en el segundo de asentamiento, para llegar al pulso a `kmh` (en
            // arena a 5 km/h, saliendo a 5 km/h el coche se paraba antes del pulso).
            const double c_rr = car.tire(surface).rolling_resistance;
            world.build(rally::tests::in_neutral(
                flat_world(surface, false, static_cast<float>(kmh / 3.6 + c_rr * kGravity * 1.0))));
            world.seal();
            world.set_telemetry(true);
            for (int s = 0; s < kStepsPerSecond; ++s) {
                world.step(axes_input(0.0, 0.0, 0.0));
            }
            const double pulse_speed = world.telemetry().speed;
            // El pulso empieza a la velocidad de la fila (± 15 %): si no, la fila no mide nada.
            CHECK(pulse_speed >= 0.85 * kmh / 3.6);
            CHECK(pulse_speed <= 1.15 * kmh / 3.6);
            // Pulso de 5° en las ruedas durante 0,25 s.
            const double pulse = 5.0 * kPi / 180.0 / car.max_steer;
            for (int s = 0; s < kStepsPerSecond / 4; ++s) {
                world.step(axes_input(pulse, 0.0, 0.0));
            }
            int changes = 0;
            int last_sign = 0;
            double last_above = 0.0; // s desde soltar hasta la última vez con |r| ≥ umbral
            double peak = 0.0;
            std::string extremes; // extremos de r entre cruces por 0 (°/s, en s)
            std::array<double, 3> first_extremes{};
            int extreme_count = 0;
            double extreme = 0.0;
            double extreme_t = 0.0;
            double previous_r = 0.0;
            for (int s = 0; s < 3 * kStepsPerSecond; ++s) {
                world.step(axes_input(0.0, 0.0, 0.0));
                const double r = world.telemetry().yaw_rate;
                peak = std::max(peak, std::abs(r));
                if ((r > 0.0) != (previous_r > 0.0) && s > 0) {
                    char e[48];
                    std::snprintf(e, sizeof(e), " %.3f (%.2f s)", extreme * 180.0 / kPi, extreme_t);
                    extremes += e;
                    if (extreme_count < 3) {
                        first_extremes[static_cast<std::size_t>(extreme_count)] = extreme;
                    }
                    ++extreme_count;
                    extreme = 0.0;
                }
                if (std::abs(r) > std::abs(extreme)) {
                    extreme = r;
                    extreme_t = static_cast<double>(s + 1) / kStepsPerSecond;
                }
                previous_r = r;
                if (std::abs(r) >= threshold) {
                    const int sign = r > 0.0 ? 1 : -1;
                    if (last_sign != 0 && sign != last_sign) {
                        ++changes;
                    }
                    last_sign = sign;
                    last_above = static_cast<double>(s + 1) / kStepsPerSecond;
                }
            }
            double zeta = 1.0;
            if (extreme_count >= 3 && first_extremes[1] != 0.0 && first_extremes[2] != 0.0) {
                const double decrement = std::log(std::abs(first_extremes[1] / first_extremes[2]));
                zeta = decrement / std::sqrt(kPi * kPi + decrement * decrement);
            }
            char row[220];
            std::snprintf(row, sizeof(row),
                          "%s a %.0f km/h (%.1f al pulso): guiñada máxima %.2f°/s, %d cambios "
                          "de signo, |r| < 0,2°/s desde %.2f s, ζ %.2f; extremos:",
                          name(surface).c_str(), kmh, pulse_speed * 3.6, peak * 180.0 / kPi,
                          changes, last_above, zeta);
            table += row;
            table += extremes.substr(0, 120) + "\n";
            CHECK(zeta >= (kmh <= 20.0 ? 0.5 : 0.4));
        }
    }
    MESSAGE("pulso de dirección de 5° durante 0,25 s y suelta:\n", table);
}

// Cambio de superficie con la huella deformada (2.5-respuesta-plan §3).
namespace {

// Curva estable a 60 km/h: la dirección sube a 4°/s hasta que la deriva media delantera llega
// a α pico del asfalto y se queda ahí.
struct CurveDrive {
    SpeedHold hold{60.0 / 3.6};
    double angle = 0.0;
    bool holding = false;
    int held_steps = 0;

    SimInput next(const SimWorld& world) {
        const CarData& car = reference_car();
        if (!holding) {
            if (std::abs(axle_slip_deg(world, 0, 1)) * kPi / 180.0 >=
                car.tire(Surface::Asphalt).slip_angle_peak) {
                holding = true;
            } else {
                angle += 4.0 * kPi / 180.0 / kStepsPerSecond;
            }
        } else {
            ++held_steps;
        }
        return axes_input(angle / car.max_steer, hold.throttle(world), 0.0);
    }
};

WorldDesc wide_world(const std::vector<rally::sim::BoxDesc>& boxes, float yaw) {
    WorldDesc desc;
    desc.bodies = boxes;
    desc.car = true;
    desc.car_spawn.speed = 60.0f / 3.6f;
    desc.car_spawn.yaw = yaw;
    desc.car_data = reference_car();
    // 2.6: mide el neumático en el cruce, en N (sin la inercia del motor uniendo las ruedas).
    return rally::tests::in_neutral(desc);
}

rally::sim::BoxDesc flat_box(Surface surface, double z_min, double z_max) {
    rally::sim::BoxDesc box;
    box.x = 0.0;
    box.y = -0.5;
    box.z = 0.5 * (z_min + z_max);
    box.half_x = 300.0f;
    box.half_y = 0.5f;
    box.half_z = static_cast<float>(0.5 * (z_max - z_min));
    box.surface = surface;
    return box;
}

} // namespace

TEST_CASE("car: crossing between asphalt and gravel in a steady curve gives no force spike") {
    const rally::sim::JoltRuntime runtime;
    const CarData& car = reference_car();
    for (const auto& [from, to] : {std::pair{Surface::Asphalt, Surface::Gravel},
                                   std::pair{Surface::Gravel, Surface::Asphalt}}) {
        // Pasada previa en la primera superficie: dónde está el coche y hacia dónde va tras 1 s de
        // curva estable.
        BodyPose cross;
        rally::sim::BodyVelocity cross_v;
        {
            SimWorld world;
            world.build(wide_world({flat_box(from, -300.0, 300.0)}, 0.0f));
            world.seal();
            CurveDrive drive;
            for (int s = 0; s < 10 * kStepsPerSecond && drive.held_steps < kStepsPerSecond; ++s) {
                world.step(drive.next(world));
            }
            REQUIRE(drive.held_steps == kStepsPerSecond);
            cross = world.car_pose();
            cross_v = world.car_velocity();
            MESSAGE("curva estable: dirección a ", drive.angle * 180.0 / kPi,
                    "°, deriva delantera ", axle_slip_deg(world, 0, 1), "°, trasera ",
                    axle_slip_deg(world, 2, 3), "°");
        }
        // Segunda pasada girada para que en ese punto vaya hacia -z: frontera recta en z.
        const double yaw =
            std::atan2(static_cast<double>(cross_v.vx), -static_cast<double>(cross_v.vz));
        const double c = std::cos(yaw);
        const double s = std::sin(yaw);
        const double border = -cross.x * s + cross.z * c;
        SimWorld world;
        // Las losas se solapan 0,5 m a cada lado de la frontera: con dos cajas que solo se tocan,
        // los cantos redondeados (radio convexo de Jolt) hacen un surco en el que la rueda pierde
        // la carga un paso. El suelo de la zona es un solo HeightFieldShape y no tiene juntas.
        world.build(wide_world({flat_box(from, border - 0.5, border + 400.0),
                                flat_box(to, border - 300.0, border + 0.5)},
                               static_cast<float>(yaw)));
        world.seal();
        CurveDrive drive;
        const rally::sim::TireSurface& target = car.tire(to);
        std::array<int, kWheelCount> crossed_at{};
        crossed_at.fill(-1);
        std::array<double, kWheelCount> rolled{};
        std::array<int, kWheelCount> last_sign{};
        std::array<WheelOutput, kWheelCount> before{}; // la rueda en el paso anterior
        std::array<double, kWheelCount> at_six_sigma{};
        std::array<bool, kWheelCount> measured{};
        int sign_changes = 0;
        double worst_over = 0.0; // máximo de |F| / (μ(F_z)·F_z) desde el cruce
        std::string rows;
        for (int step = 0; step < 15 * kStepsPerSecond; ++step) {
            world.step(drive.next(world));
            bool all_done = true;
            for (int i = 0; i < kWheelCount; ++i) {
                const auto k = static_cast<std::size_t>(i);
                const WheelOutput& w = world.car_controller().wheel(i);
                if (crossed_at[k] < 0 && w.contact && w.surface == to) {
                    crossed_at[k] = step;
                    last_sign[k] = before[k].fy > 0.0f ? 1 : -1;
                }
                if (crossed_at[k] < 0 || step - crossed_at[k] > kStepsPerSecond) {
                    all_done = all_done && crossed_at[k] >= 0;
                    continue;
                }
                all_done = false;
                const double force = std::hypot(w.fx, w.fy);
                worst_over = std::max(worst_over, force / (w.mu * w.load));
                const int sign = w.fy > 0.0f ? 1 : -1;
                if (last_sign[k] != 0 && sign != last_sign[k]) {
                    ++sign_changes;
                    char row[160];
                    std::snprintf(row, sizeof(row),
                                  "rueda %d: F_y cambia de signo %d pasos tras el cruce: de %.0f N "
                                  "(α_r %.3f°, κ_r %.4f, %s) a %.0f N (α_r %.3f°, κ_r %.4f, %s), "
                                  "carga %.0f N\n",
                                  i, step - crossed_at[k], static_cast<double>(before[k].fy),
                                  before[k].alpha * 180.0 / kPi,
                                  static_cast<double>(before[k].kappa),
                                  name(before[k].surface).c_str(), static_cast<double>(w.fy),
                                  w.alpha * 180.0 / kPi, static_cast<double>(w.kappa),
                                  name(w.surface).c_str(), static_cast<double>(w.load));
                    rows += row;
                }
                last_sign[k] = sign;
                const double vx = w.omega * car.wheel_radius - w.slip_x;
                rolled[k] += std::abs(vx) / kStepsPerSecond;
                if (!measured[k] && rolled[k] >= 6.0 * target.relaxation_lat) {
                    measured[k] = true;
                    // Estacionaria en la superficie nueva con el deslizamiento cinemático de ahora.
                    const rally::sim::TireForce steady = rally::sim::tire_force(
                        target, static_cast<float>(w.slip_x / std::abs(vx)),
                        static_cast<float>(w.slip_y / std::abs(vx)), w.load, car.nominal_load);
                    const double expected = std::hypot(steady.fx, steady.fy);
                    at_six_sigma[k] = force / expected - 1.0;
                    char row[160];
                    std::snprintf(row, sizeof(row),
                                  "rueda %d: tras rodar 6σ_y, |F| %.0f N frente a la "
                                  "estacionaria %.0f N (%+.1f %%)\n",
                                  i, force, expected, 100.0 * at_six_sigma[k]);
                    rows += row;
                }
            }
            for (int i = 0; i < kWheelCount; ++i) {
                before[static_cast<std::size_t>(i)] = world.car_controller().wheel(i);
            }
            if (all_done) {
                break;
            }
        }
        MESSAGE("cruce de ", name(from), " a ", name(to), " a 60 km/h: |F|/(μF_z) máximo ",
                worst_over, "; cambios de signo de F_y en el segundo siguiente ", sign_changes,
                "\n", rows);
        for (int i = 0; i < kWheelCount; ++i) {
            const auto k = static_cast<std::size_t>(i);
            REQUIRE(crossed_at[k] >= 0);
            CHECK(measured[k]);
            CHECK(std::abs(at_six_sigma[k]) <= 0.05);
        }
        CHECK(worst_over <= 1.0001);
        CHECK(sign_changes == 0);
    }
}

// --- Zona muerta del giro de Jolt (2.5-respuesta-1 §3, 2.5-respuesta-2 §3) ------------------

namespace {

// Giro mínimo por paso que Jolt 5.6.0 aplica (Body.inl:93): |ω|·dt > 1e-6 rad.
constexpr double kJoltRotationThreshold = 1.0e-6 * kStepsPerSecond; // rad/s

// Coche de referencia en `desc`, asentado 3 s (o cayendo, si sale alto), con la velocidad del
// coche sustituida por una guiñada de `factor` veces el umbral; da un paso.
struct ThresholdStep {
    bool skipped = false;                   // el controlador usó la velocidad lineal
    bool rotated = false;                   // Jolt giró el cuerpo en el paso
    double before = 0.0;                    // |ω| puesta (rad/s)
    double after = 0.0;                     // |ω| tras el paso
    std::array<double, kWheelCount> slip{}; // |s| que vio el neumático (m/s)
};

ThresholdStep threshold_step(const WorldDesc& desc, int settle_steps, double factor) {
    SimWorld world;
    world.build(desc);
    world.seal();
    run(world, SimInput{}, settle_steps);
    rally::sim::BodyVelocity v = world.car_velocity();
    v.wx = 0.0f;
    v.wy = static_cast<float>(factor * kJoltRotationThreshold);
    v.wz = 0.0f;
    if (settle_steps >= kStepsPerSecond) {
        v.vx = v.vy = v.vz = 0.0f; // en el suelo: solo la guiñada
    }
    world.set_car_velocity(v);
    const BodyPose before = world.car_pose();
    world.step(SimInput{});
    const BodyPose after = world.car_pose();
    ThresholdStep r;
    r.skipped = world.car_controller().rotation_skipped();
    r.rotated = before.qx != after.qx || before.qy != after.qy || before.qz != after.qz ||
                before.qw != after.qw;
    r.before = factor * kJoltRotationThreshold;
    const rally::sim::BodyVelocity w = world.car_velocity();
    r.after = std::sqrt(static_cast<double>(w.wx * w.wx + w.wy * w.wy + w.wz * w.wz));
    for (int i = 0; i < kWheelCount; ++i) {
        const WheelOutput& out = world.car_controller().wheel(i);
        r.slip[static_cast<std::size_t>(i)] = std::hypot(out.slip_x, out.slip_y);
    }
    return r;
}

} // namespace

TEST_CASE("car: the tire skips the body rotation exactly when Jolt does, in the air") {
    // En el aire nada cambia la guiñada en el paso: la ω que decide el controlador (antes del
    // paso) es la que integra Jolt (después), y las dos condiciones se pueden comparar a ±1 %.
    const rally::sim::JoltRuntime runtime;
    WorldDesc desc = flat_world(Surface::Asphalt);
    desc.car_spawn.y = 20.0;
    for (const double factor : {1.01, 0.99}) {
        const ThresholdStep r = threshold_step(desc, 1, factor);
        MESSAGE("en el aire, ×", factor, " del umbral: ω ", r.before, " → ", r.after,
                " rad/s; Jolt gira: ", r.rotated, "; el neumático usa la lineal: ", r.skipped);
        CHECK(r.after == doctest::Approx(r.before).epsilon(1e-4));
        CHECK(r.rotated == (factor > 1.0));
        CHECK(r.skipped == !r.rotated);
    }
}

TEST_CASE("car: on the ground the tire sees the contact velocity that Jolt integrates") {
    // Apoyado, el paso frena una guiñada de 1,01 veces el umbral un 27 % (amortiguamiento de
    // neumático y suspensión): el controlador decide con la ω de antes del paso, la única que
    // conoce, y Jolt gira con la de después. Justo por encima del umbral (de 1 a ~1,4 veces) el
    // neumático usa v + ω×r en un paso en que Jolt no gira; es una franja de un paso, muy lejos
    // del resto de ~35 µrad/s del coche parado. Se comprueba a 0,99 y a 1,5 veces el umbral,
    // donde los dos están del mismo lado; 1,01 solo se mide.
    const rally::sim::JoltRuntime runtime;
    const CarData& car = reference_car();
    // Distancia horizontal del centro de masas a los contactos delanteros y traseros.
    const double com_z = (1.0 - car.front_weight) * car.wheelbase - 0.5 * car.wheelbase;
    const double half_track = 0.5 * car.track;
    const double front = std::hypot(half_track, 0.5 * car.wheelbase + com_z);
    const double rear = std::hypot(half_track, 0.5 * car.wheelbase - com_z);
    for (const double factor : {0.99, 1.01, 1.5}) {
        const ThresholdStep r =
            threshold_step(flat_world(Surface::Asphalt), 3 * kStepsPerSecond, factor);
        MESSAGE("en el suelo, ×", factor, " del umbral: ω ", r.before, " → ", r.after, " (×",
                r.after / kJoltRotationThreshold, "); Jolt gira: ", r.rotated,
                "; el neumático usa la lineal: ", r.skipped, "; |s| ", r.slip[0], " ", r.slip[1],
                " ", r.slip[2], " ", r.slip[3], " m/s");
        if (factor == 1.01) {
            continue; // la franja de un paso: solo se mide
        }
        CHECK(r.skipped == !r.rotated);
        for (int i = 0; i < kWheelCount; ++i) {
            // Sin giro, el cuerpo parado no traslada los contactos y el neumático ve 0; con giro,
            // ve ω·d, lo que se mueve el contacto.
            const double expected = r.skipped ? 0.0 : r.before * (i < 2 ? front : rear);
            CHECK(r.slip[static_cast<std::size_t>(i)] ==
                  doctest::Approx(expected).epsilon(0.02).scale(1e-6));
        }
    }
}

// Aterrizaje dentro de la rampa del amortiguamiento de la carcasa (2.5-respuesta-2 §2 y
// 2.5-respuesta-3 §1). A 5 m/s el amortiguamiento hace que la fuerza llegue en el primer paso con
// carga en lugar de crecer a lo largo de σ (con la dirección a 1/4, la estacionaria ya ronda μF_z).
// Solo se exige |F| ≤ μ(F_z)·F_z en todos los pasos y ruedas; la fuerza del primer paso es un dato.
// Los cambios de signo y la convergencia a la curva no se miden aquí: tras caer 0,6 m girando, el
// coche rebota (F_z de 6000 a 1160 N) y el morro se mueve de lado, y eso mide la reacción del
// coche, no el neumático (sin amortiguamiento fallaban igual). Lo del neumático lo mide «toma de
// contacto» en test_tire.cpp.
TEST_CASE("car: landing at 5 m/s stays within mu Fz on every wheel and step") {
    const rally::sim::JoltRuntime runtime;
    std::string table;
    for (const Surface surface : {Surface::Asphalt, Surface::Gravel}) {
        WorldDesc desc = flat_world(surface, false, 5.0f);
        desc.car_spawn.y = 0.6; // cae desde 0,6 m rodando a 5 m/s
        SimWorld world;
        world.build(desc);
        world.seal();
        SimInput steer;
        steer.steer = kAxisMax / 4;
        std::array<bool, kWheelCount> landed{};
        std::array<double, kWheelCount> first{}; // |F| / (μF_z) en el primer paso con carga
        double worst = 0.0;
        for (int s = 0; s < 3 * kStepsPerSecond; ++s) {
            world.step(steer);
            for (int i = 0; i < kWheelCount; ++i) {
                const auto k = static_cast<std::size_t>(i);
                const WheelOutput& w = world.car_controller().wheel(i);
                if (!(w.contact && w.load > 0.0f)) {
                    continue;
                }
                const double fraction =
                    std::hypot(w.fx, w.fy) / (static_cast<double>(w.mu) * w.load);
                worst = std::max(worst, fraction);
                if (!landed[k]) {
                    landed[k] = true;
                    first[k] = fraction;
                }
            }
        }
        char row[200];
        std::snprintf(row, sizeof(row),
                      "%s: primer paso con carga %.1f / %.1f / %.1f / %.1f %% de μF_z (DI, DD, TI, "
                      "TD); |F|/(μF_z) máximo %.4f\n",
                      name(surface).c_str(), 100.0 * first[0], 100.0 * first[1], 100.0 * first[2],
                      100.0 * first[3], worst);
        table += row;
        for (const bool l : landed) {
            CHECK(l);
        }
        CHECK(worst <= 1.0001);
    }
    MESSAGE("aterrizaje desde 0,6 m rodando a 5 m/s:\n", table);
}

// Tarea 2.5b: ayuda de dirección en el coche entero.
namespace {

// Mundo llano y grande (cabe un círculo de más de 100 m de radio) con la ayuda elegida.
WorldDesc assist_world(Surface surface, double kmh, bool assist) {
    WorldDesc desc = flat_world(surface, false, static_cast<float>(kmh / 3.6));
    desc.bodies[0].half_x = 400.0f;
    desc.bodies[0].half_z = 400.0f;
    desc.aids.steer_assist = assist;
    // 2.6: miden la ayuda con el tren motriz abierto, como en la 2.5b; la chicane con el tren
    // motriz real va aparte ("car: the 2.5b chicane with the real drivetrain...").
    return rally::tests::open_driveline(desc);
}

double deg_of(double rad) { return rad * 180.0 / kPi; }

// Ángulo de Jolt de la rueda `index` (positivo a la izquierda), en grados y en valor absoluto.
double wheel_steer_deg(const SimWorld& world, int index) {
    return std::abs(deg_of(world.car_wheel(index).steer));
}

} // namespace

// Criterio 1: desactivada, la dirección a fondo da el ángulo máximo; activada, a 10 km/h también.
TEST_CASE("car: full lock gives the maximum wheel angle with the assist off, and at 10 km/h on") {
    const rally::sim::JoltRuntime runtime;
    const CarData& car = reference_car();
    struct Case {
        double kmh;
        bool assist;
    };
    for (const Case c :
         {Case{10.0, false}, Case{60.0, false}, Case{100.0, false}, Case{10.0, true}}) {
        INFO(c.kmh, " km/h, ayuda ", c.assist ? "sí" : "no");
        SimWorld world;
        world.build(assist_world(Surface::Asphalt, c.kmh, c.assist));
        world.seal();
        // Hasta el tope a la velocidad de las manos (84 pasos como mucho) y unos pasos más.
        for (int s = 0; s < 100; ++s) {
            world.step(axes_input(1.0, 0.0, 0.0));
        }
        CHECK(world.controls().steer.applied == kAxisMax);
        // Giro a la derecha: la interior es la derecha.
        CHECK(wheel_steer_deg(world, rally::sim::kFrontRight) ==
              doctest::Approx(deg_of(car.max_steer)).epsilon(1.0e-5));
    }
}

// Criterio 1: activada, a 60 y 100 km/h en asfalto y en grava con la dirección a fondo, el ángulo
// de deriva de la rueda delantera exterior queda en α_pico ± 10 %. Se mide en los pasos en que
// la ayuda controla esa rueda: la trasera agarra (assist_rear_sliding = 0) y el tope de la derecha
// es > 0. Solo en asfalto a 60 km/h hay régimen (velocidad ± 1 km/h y guiñada ± 2 %) y se exige.
// En los otros tres casos el coche hace un ciclo límite (la trasera pasa de su pico, la ayuda
// endereza las ruedas y vuelve a empezar; en grava a 100 km/h, de unos 2,8 s): es del equilibrio
// del coche en el límite (2.6 y 2.10), no de la ayuda (mensajes/2.5b-respuesta-1.md).
TEST_CASE("car: with the assist and full lock the outer front wheel settles at the peak slip "
          "angle") {
    const rally::sim::JoltRuntime runtime;
    const CarData& car = reference_car();
    std::string table;
    for (const Surface surface : {Surface::Asphalt, Surface::Gravel}) {
        const double peak = deg_of(car.tire(surface).slip_angle_peak);
        for (const double kmh : {60.0, 100.0}) {
            INFO(name(surface), " a ", kmh, " km/h");
            const bool steady = surface == Surface::Asphalt && kmh == 60.0;
            SimWorld world;
            world.build(assist_world(surface, kmh, true));
            world.seal();
            world.set_telemetry(true);
            SpeedHold hold{kmh / 3.6};
            // 6 s para asentarse y 4 s de medida (más de un ciclo límite).
            constexpr int kSettle = 6 * kStepsPerSecond;
            constexpr int kMeasure = 4 * kStepsPerSecond;
            int grip_steps = 0;
            double alpha_sum = 0.0;
            double speed_min = 1.0e9;
            double speed_max = 0.0;
            double yaw_min = 1.0e9;
            double yaw_max = -1.0e9;
            double alpha_min = 1.0e9;
            double alpha_max = 0.0;
            bool spun = false;
            for (int s = 0; s < kSettle + kMeasure; ++s) {
                world.step(axes_input(1.0, hold.throttle(world), 0.0));
                const rally::sim::CarTelemetry& t = world.telemetry();
                if (std::abs(t.beta) > 45.0 * kPi / 180.0) {
                    spun = true;
                    break;
                }
                if (s < kSettle) {
                    continue;
                }
                const double speed = horizontal_speed(world) * 3.6;
                speed_min = std::min(speed_min, speed);
                speed_max = std::max(speed_max, speed);
                yaw_min = std::min(yaw_min, static_cast<double>(t.yaw_rate));
                yaw_max = std::max(yaw_max, static_cast<double>(t.yaw_rate));
                const rally::sim::SteerContext& context = world.steer_context();
                if (context.rear_sliding || context.right_max <= 0) {
                    continue;
                }
                // Giro a la derecha: la exterior es la izquierda.
                const double alpha =
                    std::abs(deg_of(world.car_controller().wheel(rally::sim::kFrontLeft).alpha));
                ++grip_steps;
                alpha_sum += alpha;
                alpha_min = std::min(alpha_min, alpha);
                alpha_max = std::max(alpha_max, alpha);
            }
            REQUIRE_FALSE(spun);
            REQUIRE(grip_steps >= kStepsPerSecond / 2);
            const double alpha_mean = alpha_sum / grip_steps;
            char row[300];
            std::snprintf(row, sizeof(row),
                          "%s %.0f km/h: α exterior %.2f° (%.2f–%.2f; pico %.1f°) en %.2f s de "
                          "%.0f con la trasera agarrando; v %.1f–%.1f km/h, guiñada %.3f–%.3f "
                          "rad/s%s\n",
                          name(surface).c_str(), kmh, alpha_mean, alpha_min, alpha_max, peak,
                          grip_steps / static_cast<double>(kStepsPerSecond),
                          kMeasure / static_cast<double>(kStepsPerSecond), speed_min, speed_max,
                          yaw_min, yaw_max, steady ? " (régimen)" : " (ciclo límite)");
            table += row;
            if (steady) {
                CHECK(grip_steps == kMeasure);
                CHECK(speed_min >= kmh - 1.0);
                CHECK(speed_max <= kmh + 1.0);
                CHECK(yaw_max - yaw_min <= 0.04 * std::abs(yaw_max));
            }
            CHECK(alpha_mean >= 0.9 * peak);
            CHECK(alpha_mean <= 1.1 * peak);
        }
    }
    MESSAGE("dirección a fondo con la ayuda (4 s tras 6 s):\n", table);
}

// Criterio 1: el contravolante a fondo da el ángulo máximo hacia fuera, con la trasera por
// encima de su pico.
TEST_CASE("car: with the assist, full countersteer with the rear sliding reaches full lock") {
    const rally::sim::JoltRuntime runtime;
    const CarData& car = reference_car();
    for (const Surface surface : {Surface::Asphalt, Surface::Gravel}) {
        INFO(name(surface));
        SimWorld world;
        world.build(assist_world(surface, 0.0, true));
        world.seal();
        // Derrape girando a la derecha a 60 km/h: velocidad 35° a la izquierda del morro (el
        // morro mira a -z, la derecha es +x) y guiñada a la derecha de 1 rad/s.
        const double v = 60.0 / 3.6;
        const double beta = -35.0 * kPi / 180.0;
        rally::sim::BodyVelocity velocity;
        velocity.vx = static_cast<float>(v * std::sin(beta));
        velocity.vz = static_cast<float>(-v * std::cos(beta));
        velocity.wy = -1.0f;
        world.set_car_velocity(velocity);
        world.step(axes_input(0.0, 0.0, 0.0));
        int steps = 0;
        bool sliding_throughout = true;
        while (world.controls().steer.applied != -kAxisMax && steps < 200) {
            world.step(axes_input(-1.0, 0.0, 0.0));
            sliding_throughout = sliding_throughout && world.steer_context().rear_sliding &&
                                 world.steer_context().active;
            ++steps;
        }
        CHECK(sliding_throughout);
        CHECK(world.controls().steer.applied == -kAxisMax);
        // Giro a la izquierda: la interior es la izquierda.
        CHECK(wheel_steer_deg(world, rally::sim::kFrontLeft) ==
              doctest::Approx(deg_of(car.max_steer)).epsilon(1.0e-5));
        MESSAGE(name(surface), ": contravolante a fondo en ", steps, " pasos");
    }
}

// Chicane con teclado a 100 km/h en asfalto: la entrada hacia el otro lado es un giro nuevo y
// recibe su ventana. Criterio: mientras la ayuda controla la rueda exterior (la trasera agarra y
// el tope de ese lado es > 0), esa rueda no pasa de 1,25·α_pico durante más de 0,1 s.
// Referencia de trompo (mensajes/2.5b-respuesta-1.md): con la ayuda, el coche hace un trompo en
// este test; sin ella, no, porque la delantera pasa de su pico y subvira. Es del equilibrio del
// coche en el límite (tracción en la 2.6; chasis y neumático en la 2.10): β máximo, guiñada
// máxima y si hay trompo salen en la salida, sin criterio.
TEST_CASE("car: with the assist a keyboard chicane at 100 km/h keeps the outer wheel near its "
          "peak") {
    const rally::sim::JoltRuntime runtime;
    const CarData& car = reference_car();
    const double peak = deg_of(car.tire(Surface::Asphalt).slip_angle_peak);
    SimWorld world;
    world.build(assist_world(Surface::Asphalt, 100.0, true));
    world.seal();
    world.set_telemetry(true);
    SpeedHold hold{100.0 / 3.6};
    // Recta 0,5 s y después derecha, izquierda y derecha a fondo, 2 s cada una.
    constexpr int kStraight = kStepsPerSecond / 2;
    constexpr int kPhase = 2 * kStepsPerSecond;
    int controlled = 0;
    int above_peak = 0;
    int above_limit = 0;
    double worst = 0.0;
    double max_beta = 0.0;
    double max_yaw = 0.0;
    for (int s = 0; s < kStraight + 3 * kPhase; ++s) {
        const int phase = s < kStraight ? 0 : 1 + (s - kStraight) / kPhase;
        const double steer = phase == 0 ? 0.0 : (phase % 2 == 1 ? 1.0 : -1.0);
        world.step(axes_input(steer, hold.throttle(world), 0.0, rally::sim::kButtonSteerDigital));
        const rally::sim::CarTelemetry& t = world.telemetry();
        max_beta = std::max(max_beta, std::abs(deg_of(t.beta)));
        max_yaw = std::max(max_yaw, std::abs(static_cast<double>(t.yaw_rate)));
        const std::int32_t applied = world.controls().steer.applied;
        const rally::sim::SteerContext& context = world.steer_context();
        const std::int32_t limit = applied > 0 ? context.right_max : context.left_max;
        if (applied == 0 || context.rear_sliding || limit <= 0) {
            continue;
        }
        const int outer = applied > 0 ? rally::sim::kFrontLeft : rally::sim::kFrontRight;
        const double alpha = std::abs(deg_of(world.car_controller().wheel(outer).alpha));
        ++controlled;
        worst = std::max(worst, alpha);
        above_peak += alpha > peak ? 1 : 0;
        above_limit += alpha > 1.25 * peak ? 1 : 0;
    }
    const double steps_per_second = kStepsPerSecond;
    MESSAGE("chicane con teclado a 100 km/h, con la trasera agarrando (",
            controlled / steps_per_second, " s): pico de α exterior ", worst, "° (α_pico ", peak,
            "°); por encima de α_pico ", above_peak / steps_per_second,
            " s; por encima de 1,25·α_pico ", above_limit / steps_per_second,
            " s. Referencia de trompo: β máximo ", max_beta, "°, guiñada máxima ", max_yaw,
            " rad/s, trompo (|β| > 90°): ", (max_beta > 90.0 ? "sí" : "no"));
    CHECK(controlled > kStepsPerSecond);
    CHECK(above_limit <= kStepsPerSecond / 10);
}

namespace {

struct Chicane {
    double max_beta = 0.0;  // °
    double max_yaw = 0.0;   // rad/s
    double spin_kmh = -1.0; // velocidad al pasar |β| de 90° (−1: sin trompo)
    int lcp_iterations = 0; // máximo de los pasos
    std::array<int, rally::sim::kMaxLcpIterations + 1> iterations{}; // pasos por máximo
};

// La chicane de la 2.5b (asfalto, 100 km/h, ayuda y teclado: recta 0,5 s y derecha, izquierda y
// derecha a fondo, 2 s cada una) con el tren motriz real: el acelerador sujeta los 100 km/h o,
// con `lift`, queda a 0 desde el primer giro.
Chicane run_chicane(const WorldDesc& desc, bool lift) {
    SimWorld world;
    world.build(desc);
    world.seal();
    world.set_telemetry(true);
    SpeedHold hold{100.0 / 3.6};
    constexpr int kStraight = kStepsPerSecond / 2;
    constexpr int kPhase = 2 * kStepsPerSecond;
    Chicane out;
    for (int s = 0; s < kStraight + 3 * kPhase; ++s) {
        const int phase = s < kStraight ? 0 : 1 + (s - kStraight) / kPhase;
        const double steer = phase == 0 ? 0.0 : (phase % 2 == 1 ? 1.0 : -1.0);
        const double throttle = lift && phase > 0 ? 0.0 : hold.throttle(world);
        world.step(axes_input(steer, throttle, 0.0, rally::sim::kButtonSteerDigital));
        const rally::sim::CarTelemetry& t = world.telemetry();
        const double beta = std::abs(deg_of(t.beta));
        out.max_beta = std::max(out.max_beta, beta);
        out.max_yaw = std::max(out.max_yaw, std::abs(static_cast<double>(t.yaw_rate)));
        if (beta > 90.0 && out.spin_kmh < 0.0) {
            out.spin_kmh = static_cast<double>(t.speed) * 3.6;
        }
        out.lcp_iterations = std::max(out.lcp_iterations, static_cast<int>(t.lcp_iterations));
        ++out.iterations[static_cast<std::size_t>(t.lcp_iterations)];
    }
    CHECK(world.car_controller().lcp_failures_total() == 0);
    return out;
}

std::string chicane_row(const char* what, const Chicane& c) {
    char row[200];
    std::snprintf(row, sizeof(row), "%s: β máx. %.0f°, guiñada máx. %.2f rad/s, %s\n", what,
                  c.max_beta, c.max_yaw,
                  c.spin_kmh < 0.0
                      ? "sin trompo"
                      : ("trompo a " + std::to_string(int(c.spin_kmh + 0.5)) + " km/h").c_str());
    return row;
}

} // namespace

TEST_CASE("car: the 2.5b chicane with the real drivetrain, holding 100 km/h and lifting off") {
    const rally::sim::JoltRuntime runtime;
    // Referencia de la 2.5b (tracción provisional): β máx. 74°, guiñada máx. 2,54 rad/s; con el
    // acelerador a 0 desde el primer giro, trompo a 19 km/h. Solo mide (equilibrio: 2.10).
    WorldDesc real = flat_world(Surface::Asphalt, false, 100.0f / 3.6f);
    real.bodies[0].half_x = 400.0f;
    real.bodies[0].half_z = 400.0f;
    real.aids.steer_assist = true;
    const Chicane hold = run_chicane(real, false);
    const Chicane lift = run_chicane(real, true);
    const Chicane open = run_chicane(rally::tests::open_driveline(real), false);
    std::string spread;
    for (std::size_t i = 1; i < hold.iterations.size(); ++i) {
        spread += std::to_string(i) + ": " + std::to_string(hold.iterations[i]) + "; ";
    }
    MESSAGE("chicane de la 2.5b con el tren motriz:\n",
            chicane_row("tren real, sujetando 100 km/h", hold),
            chicane_row("tren real, acelerador a 0 desde el primer giro", lift),
            chicane_row("tren abierto (como la 2.5b), sujetando 100 km/h", open),
            "iteraciones del LCP (máximo por paso) con el tren real: ", spread);
}
