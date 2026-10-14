// Lo que la cabeza, la vibración y el sonido sacan de cada paso (tarea 2.7): fuerza específica,
// velocidad de la suspensión, sucesos y las medidas por superficie.

#include "car_worlds.h"
#include "platform/car_feedback.h"
#include "sim/jolt_runtime.h"
#include "sim/sim_input.h"
#include "sim/sim_world.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using rally::platform::CarFeedback;
using rally::platform::Quatd;
using rally::sim::CarTelemetry;
using rally::sim::kAxisMax;
using rally::sim::kStepsPerSecond;
using rally::sim::SimInput;
using rally::sim::SimWorld;
using rally::sim::Surface;

namespace {

Quatd rotation(const rally::sim::BodyPose& p) { return {p.qx, p.qy, p.qz, p.qw}; }

// Un paso del mundo y de CarFeedback, como DriveDemo::step.
void step(SimWorld& world, CarFeedback& feedback, const SimInput& in) {
    world.step(in);
    feedback.step(world.telemetry(), rotation(world.car_pose()));
}

SimInput pedals(double throttle, double brake) {
    SimInput in;
    in.throttle = static_cast<std::int16_t>(std::clamp(throttle, 0.0, 1.0) * kAxisMax);
    in.brake = static_cast<std::int16_t>(std::clamp(brake, 0.0, 1.0) * kAxisMax);
    return in;
}

} // namespace

TEST_CASE("car_feedback: parked uphill on the 15 percent ramp the head leans back") {
    // Fuerza específica: mirando cuesta arriba, f·adelante = g·sin θ, con θ = atan 0,15 (8,53°,
    // sin θ = 0,1483). Longitudinal = −3·tanh(2,0·0,1483 / 3) = −0,296 cm. Con la aceleración
    // cinemática daría 0.
    const rally::sim::JoltRuntime runtime;
    rally::sim::WorldDesc desc = rally::tests::in_neutral(rally::tests::ramp_world());
    desc.car_spawn.y = 0.25;
    SimWorld world;
    world.build(desc);
    world.seal();
    world.set_telemetry(true);
    CarFeedback feedback;
    feedback.rebuild(desc.car_data);
    for (int i = 0; i < 4 * kStepsPerSecond; ++i) {
        step(world, feedback, pedals(0.0, 1.0));
    }
    const double forward = feedback.head().current().forward;
    MESSAGE("cabeza en la rampa: ", forward * 100.0, " cm; entrada longitudinal ",
            feedback.head_input().forward, " m/s²");
    CHECK(forward == doctest::Approx(-0.00296).epsilon(0.05));
}

namespace {

CarTelemetry telemetry(float throttle, float boost_bar, bool shifting = false) {
    CarTelemetry t;
    t.throttle = throttle;
    t.engine_throttle = shifting ? 0.0f : throttle;
    t.boost = boost_bar * 1.0e5f;
    t.shifting = shifting;
    return t;
}

} // namespace

TEST_CASE("car_feedback: the blow-off counts pedal releases with boost, with hysteresis") {
    const rally::sim::CarData& car = rally::tests::reference_car();
    SUBCASE("one release with boost") {
        CarFeedback f;
        f.rebuild(car);
        for (int i = 0; i < 100; ++i) {
            f.step(telemetry(1.0f, 1.0f), {});
        }
        for (int i = 0; i < 100; ++i) {
            f.step(telemetry(0.0f, 0.9f), {});
        }
        CHECK(f.blowoffs() == 1);
    }
    SUBCASE("ten automatic upshifts at full pedal") {
        // El corte del cambio baja el acelerador efectivo, no el pedal: sin soplido.
        CarFeedback f;
        f.rebuild(car);
        for (int shift = 0; shift < 10; ++shift) {
            for (int i = 0; i < 200; ++i) {
                f.step(telemetry(1.0f, 1.0f), {});
            }
            for (int i = 0; i < 19; ++i) { // 0,08 s
                f.step(telemetry(1.0f, 1.0f, true), {});
            }
        }
        CHECK(f.blowoffs() == 0);
        CHECK(f.shifts() == 10);
    }
    SUBCASE("a pedal hovering between 0.15 and 0.25") {
        CarFeedback f;
        f.rebuild(car);
        for (int i = 0; i < 100; ++i) {
            f.step(telemetry(1.0f, 1.0f), {});
        }
        for (int i = 0; i < 400; ++i) {
            f.step(telemetry(i % 20 < 10 ? 0.15f : 0.25f, 0.8f), {});
        }
        CHECK(f.blowoffs() == 1);
    }
    SUBCASE("no boost, no blow-off") {
        CarFeedback f;
        f.rebuild(car);
        f.step(telemetry(1.0f, 0.1f), {});
        f.step(telemetry(1.0f, 0.1f), {});
        f.step(telemetry(0.0f, 0.1f), {});
        CHECK(f.blowoffs() == 0);
    }
}

TEST_CASE(
    "car_feedback: suspension speed is 0 on the reset step, the next one and after a rebuild") {
    const rally::sim::CarData& car = rally::tests::reference_car();
    CarFeedback f;
    f.rebuild(car);
    CarTelemetry t;
    t.wheels[0].compression = 0.10f;
    f.step(t, {});
    CHECK(f.suspension_speed()[0] == 0.0f); // primer paso
    t.wheels[0].compression = 0.11f;
    f.step(t, {});
    CHECK(f.suspension_speed()[0] == doctest::Approx(0.01f * 240.0f));
    // Recolocar: la compresión salta y la velocidad es 0; la cabeza, en reposo.
    t.wheels[0].compression = 0.02f;
    t.pressed = rally::sim::kButtonReset;
    t.impact = 3.0f; // un impacto en el paso que recoloca no es un suceso
    f.step(t, {});
    CHECK(f.suspension_speed()[0] == 0.0f);
    CHECK(f.head().current().up == 0.0);
    CHECK(f.impacts() == 0);
    t.pressed = 0;
    t.impact = 0.0f;
    t.wheels[0].compression = 0.12f; // el siguiente: las ruedas vuelven al suelo
    f.step(t, {});
    CHECK(f.suspension_speed()[0] == 0.0f);
    t.wheels[0].compression = 0.13f;
    f.step(t, {});
    CHECK(f.suspension_speed()[0] == doctest::Approx(0.01f * 240.0f));
    // Reconstruir: el siguiente paso vuelve a ser el primero.
    f.rebuild(car);
    t.wheels[0].compression = 0.15f;
    f.step(t, {});
    CHECK(f.suspension_speed()[0] == 0.0f);
    // Un impacto cuenta una vez mientras dura.
    t.impact = 2.0f;
    f.step(t, {});
    t.impact = 4.0f;
    f.step(t, {});
    t.impact = 0.0f;
    f.step(t, {});
    CHECK(f.impacts() == 1);
}

namespace {

struct Straight {
    double mean_abs = 0.0;    // m/s: media de |ż| (cuatro ruedas)
    double p99 = 0.0;         // m/s: percentil 99 de la media de cada paso
    double head_up_rms = 0.0; // m
    double speed = 0.0;       // m/s medida
};

// El coche de referencia en recta sobre una losa con el microrrelieve de `surface`, a `speed`
// m/s sostenidos con el acelerador (control proporcional); 2 s de asentamiento y 4 s de medida.
Straight measure_straight(Surface surface, double speed) {
    const rally::sim::JoltRuntime runtime;
    rally::sim::WorldDesc desc = rally::tests::flat_world(surface, true, static_cast<float>(speed));
    SimWorld world;
    world.build(desc);
    world.seal();
    world.set_telemetry(true);
    CarFeedback feedback;
    feedback.rebuild(desc.car_data);
    std::vector<double> per_step;
    double sum = 0.0;
    double up2 = 0.0;
    double v_sum = 0.0;
    const int settle = 2 * kStepsPerSecond;
    const int total = 6 * kStepsPerSecond;
    for (int i = 0; i < total; ++i) {
        const double v = world.telemetry().v_forward;
        step(world, feedback, pedals(0.3 + 0.5 * (speed - v), 0.0));
        if (i < settle) {
            continue;
        }
        double s = 0.0;
        for (const float z : feedback.suspension_speed()) {
            s += std::abs(z);
        }
        s *= 0.25;
        per_step.push_back(s);
        sum += s;
        const double up = feedback.head().current().up;
        up2 += up * up;
        v_sum += world.telemetry().speed;
    }
    std::sort(per_step.begin(), per_step.end());
    const auto n = static_cast<double>(per_step.size());
    Straight out;
    out.mean_abs = sum / n;
    out.p99 = per_step[static_cast<std::size_t>(0.99 * (n - 1))];
    out.head_up_rms = std::sqrt(up2 / n);
    out.speed = v_sum / n;
    return out;
}

} // namespace

TEST_CASE("car_feedback: suspension speed and head bounce per surface on a straight") {
    // Medida (2.7-respuesta-plan §2): de ella salen kBumpSpeedStart y kBumpSpeedFull. Sin umbral,
    // salvo que el microrrelieve no deje el término de los baches por encima de ~0,3 en grava a
    // 100 km/h.
    const std::array<Surface, 5> surfaces = {Surface::Asphalt, Surface::Gravel, Surface::Dirt,
                                             Surface::Snow, Surface::Sand};
    const std::array<double, 3> speeds_kmh = {30.0, 60.0, 100.0};
    std::string table =
        "\n| superficie | km/h | |ż| medio (m/s) | p99 (m/s) | término de baches |\n";
    double gravel_100 = 0.0;
    for (const Surface s : surfaces) {
        for (const double kmh : speeds_kmh) {
            const Straight m = measure_straight(s, kmh / 3.6);
            const double term = rally::platform::bump_term(static_cast<float>(m.mean_abs));
            char row[160];
            std::snprintf(row, sizeof(row), "| %s | %.0f | %.4f | %.4f | %.2f |\n",
                          rally::sim::surface_name(s), m.speed * 3.6, m.mean_abs, m.p99, term);
            table += row;
            if (s == Surface::Gravel && kmh == 100.0) {
                gravel_100 = term;
            }
            CHECK(std::isfinite(m.mean_abs));
        }
    }
    MESSAGE(table);
    CHECK(gravel_100 <= 0.3);

    // Cabeza a ciegas (2.7-inicio §6): RMS vertical en recta a 80 km/h en asfalto y en arena.
    const Straight asphalt = measure_straight(Surface::Asphalt, 80.0 / 3.6);
    const Straight sand = measure_straight(Surface::Sand, 80.0 / 3.6);
    MESSAGE("cabeza, RMS vertical a 80 km/h: asfalto ", asphalt.head_up_rms * 1000.0, " mm; arena ",
            sand.head_up_rms * 1000.0, " mm");
}
