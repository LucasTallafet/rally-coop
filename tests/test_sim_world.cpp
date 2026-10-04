#include "car_worlds.h"
#include "jolt_allocation_counter.h"
#include "new_counter.h"
#include "platform/input_shaping.h"
#include "sim/jolt_config.h"
#include "sim/jolt_runtime.h"
#include "sim/pcg32.h"
#include "sim/sim_input.h"
#include "sim/sim_world.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace {

using rally::sim::BodyMotion;
using rally::sim::BodyPose;
using rally::sim::BoxDesc;
using rally::sim::kAxisMax;
using rally::sim::kButtonSteerDigital;
using rally::sim::kButtonThrottleDigital;
using rally::sim::SimInput;
using rally::sim::SimWorld;
using rally::sim::WorldDesc;

// Huella (SimWorld::state_hash: SaveState más ControlState) tras 60 s de entradas
// pseudoaleatorias (run_random_inputs).
// Si el test de huella falla por un cambio intencionado, se copia aquí el valor nuevo y se
// dice en el mensaje del commit.
constexpr std::uint64_t kExpectedFingerprint = 0x899d7d7e75fa66f1ull;

constexpr int kSecondsOfInput = 60;
constexpr int kStepsPerFrame = 4; // la entrada cambia cada fotograma de 60 fps

constexpr int kWarmupSteps = 10;
constexpr int kCountedSteps = 1000;
// Reservas de Jolt 5.6.0 en cada paso que deja sucio el árbol de la capa móvil (algún
// cuerpo cambió de caja envolvente; si no, ninguna): la broad phase lo reconstruye y
// QuadTree::UpdatePrepare y QuadTree::BuildTree hacen un new[] cada uno (con su delete[]
// en la misma llamada). Tamaño constante y sin
// efecto en la reproducibilidad; aceptado (docs/progreso.md, hallazgos). Si un cambio
// intencionado lo altera, se actualiza como la huella.
constexpr int kJoltAllocationsPerStep = 2;
// Total de reservas de Jolt en los kCountedSteps pasos medidos con las entradas del test
// (990 pasos con reservas y 10 sin ellas). Si un cambio intencionado lo altera, se
// actualiza como la huella y se dice en el mensaje del commit.
constexpr int kExpectedJoltAllocations = 1980;

// Suelo de 20 × 20 m con cuatro muros de 3 m, el coche de referencia parado en el centro
// mirando a -z y una caja suelta para que haya contactos entre cuerpos dinámicos.
WorldDesc arena() {
    WorldDesc desc;
    desc.bodies = {
        {BodyMotion::Static, 0.0, -0.5, 0.0, 10.0f, 0.5f, 10.0f},
        {BodyMotion::Static, 0.0, 1.5, -10.5, 11.0f, 1.5f, 0.5f},
        {BodyMotion::Static, 0.0, 1.5, 10.5, 11.0f, 1.5f, 0.5f},
        {BodyMotion::Static, -10.5, 1.5, 0.0, 0.5f, 1.5f, 10.0f},
        {BodyMotion::Static, 10.5, 1.5, 0.0, 0.5f, 1.5f, 10.0f},
        {BodyMotion::Dynamic, 3.0, 0.5, 2.0, 0.5f, 0.5f, 0.5f},
    };
    desc.car = true;
    desc.car_data = rally::tests::reference_car();
    return desc;
}

// Pedal (acelerador o freno) en [0, kAxisMax]: suelto, a fondo o intermedio.
std::int16_t random_pedal(rally::sim::Pcg32& rng) {
    switch (rng.bounded(3)) {
    case 0:
        return 0;
    case 1:
        return kAxisMax;
    default:
        return static_cast<std::int16_t>(rng.bounded(kAxisMax + 1u));
    }
}

std::int16_t random_axis(rally::sim::Pcg32& rng) {
    switch (rng.bounded(4)) {
    case 0:
        return 0;
    case 1:
        return kAxisMax;
    case 2:
        return -kAxisMax;
    default:
        return static_cast<std::int16_t>(static_cast<int>(rng.bounded(2u * kAxisMax + 1u)) -
                                         kAxisMax);
    }
}

// Una entrada por paso, constante dentro de cada fotograma, como la lee el bucle.
std::vector<SimInput> random_inputs(int seconds) {
    rally::sim::Pcg32 rng(0x5eed'1234'abcd'0001ull, 7u);
    std::vector<SimInput> inputs;
    inputs.reserve(static_cast<std::size_t>(seconds * rally::sim::kStepsPerSecond));
    while (inputs.size() < inputs.capacity()) {
        SimInput input;
        input.steer = random_axis(rng);
        input.throttle = random_pedal(rng);
        // Freno en un tercio de los fotogramas, a veces a la vez que el acelerador.
        input.brake = rng.bounded(3) == 0 ? random_pedal(rng) : 0;
        std::uint16_t buttons = 0;
        const auto maybe = [&rng, &buttons](std::uint32_t one_in, std::uint16_t bit) {
            if (rng.bounded(one_in) == 0) {
                buttons |= bit;
            }
        };
        maybe(8, rally::sim::kButtonHandbrake);
        maybe(30, rally::sim::kButtonShiftUp);
        maybe(30, rally::sim::kButtonShiftDown);
        // Recolocar de vez en cuando (unas 6 veces en 60 s).
        maybe(600, rally::sim::kButtonReset);
        // Mitad de los fotogramas con cada eje digital: pasan por los limitadores del teclado.
        maybe(2, kButtonSteerDigital);
        maybe(2, kButtonThrottleDigital);
        maybe(2, rally::sim::kButtonBrakeDigital);
        input.buttons = buttons;
        for (int i = 0; i < kStepsPerFrame && inputs.size() < inputs.capacity(); ++i) {
            inputs.push_back(input);
        }
    }
    return inputs;
}

struct RunResult {
    std::string state;
    std::uint64_t hash = 0; // SimWorld::state_hash
    std::vector<BodyPose> poses;
    BodyPose car;
    double max_distance = 0.0; // distancia horizontal máxima del coche al centro
};

// `telemetry`: con la captura de la telemetría encendida (solo lee: misma huella).
RunResult run_random_inputs(bool telemetry = false) {
    const std::vector<SimInput> inputs = random_inputs(kSecondsOfInput);
    SimWorld world;
    world.build(arena());
    world.seal();
    world.set_telemetry(telemetry);

    RunResult result;
    double max_distance_sq = 0.0;
    for (const SimInput& input : inputs) {
        world.step(input);
        const BodyPose pose = world.car_pose();
        max_distance_sq = std::max(max_distance_sq, pose.x * pose.x + pose.z * pose.z);
    }
    result.max_distance = std::sqrt(max_distance_sq);
    for (int i = 0; i < world.body_count(); ++i) {
        result.poses.push_back(world.pose(i));
    }
    result.car = world.car_pose();
    result.state = world.save_state();
    result.hash = world.state_hash();
    return result;
}

// Rumbo del morro (-z local): 0 mirando a -z, > 0 girando a la derecha.
double heading(const BodyPose& p) {
    const double fx = -2.0 * (p.qx * p.qz + p.qw * p.qy);
    const double fz = -(1.0 - 2.0 * (p.qx * p.qx + p.qy * p.qy));
    return std::atan2(fx, -fz);
}

bool same_bits(double a, double b) {
    return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}

bool same_pose(const BodyPose& a, const BodyPose& b) {
    return same_bits(a.x, b.x) && same_bits(a.y, b.y) && same_bits(a.z, b.z) &&
           same_bits(a.qx, b.qx) && same_bits(a.qy, b.qy) && same_bits(a.qz, b.qz) &&
           same_bits(a.qw, b.qw);
}

using rally::tests::JoltAllocationCounter;

} // namespace

TEST_CASE("sim_world: the same description gives the same body ids") {
    const rally::sim::JoltRuntime runtime;
    SimWorld first;
    SimWorld second;
    first.build(arena());
    second.build(arena());

    REQUIRE(first.body_count() == static_cast<int>(arena().bodies.size()));
    REQUIRE(second.body_count() == first.body_count());
    for (int i = 0; i < first.body_count(); ++i) {
        CHECK(first.body_id(i) == second.body_id(i));
        for (int j = 0; j < i; ++j) {
            CHECK(first.body_id(i) != first.body_id(j));
        }
    }
}

TEST_CASE("sim_world: only a sealed world can step") {
    const rally::sim::JoltRuntime runtime;
    SimWorld world;
    CHECK_FALSE(world.is_sealed());
    world.build(arena());
    CHECK_FALSE(world.is_sealed());
    world.seal();
    CHECK(world.is_sealed());
    world.step(SimInput{});
}

TEST_CASE("sim_world: throttle drives the car forward and steering right turns it right") {
    const rally::sim::JoltRuntime runtime;
    SimWorld world;
    world.build(arena());
    world.seal();

    // Sin entrada, el coche se queda donde está.
    for (int i = 0; i < rally::sim::kStepsPerSecond; ++i) {
        world.step(SimInput{});
    }
    const BodyPose rest = world.car_pose();
    CHECK(std::abs(rest.x) < 1e-3);
    CHECK(std::abs(rest.z) < 1e-3);

    // Un segundo de acelerador a fondo: avanza hacia -z sin girar.
    SimInput push;
    push.throttle = kAxisMax;
    for (int i = 0; i < rally::sim::kStepsPerSecond; ++i) {
        world.step(push);
    }
    const BodyPose pushed = world.car_pose();
    MESSAGE("1 s a fondo: ", rest.z - pushed.z, " m hacia delante");
    CHECK(rest.z - pushed.z > 2.0);
    CHECK(std::abs(pushed.x - rest.x) < 0.05);
    CHECK(std::abs(heading(pushed)) < 0.01);

    // Medio segundo girando a la derecha: el rumbo crece.
    SimInput turn;
    turn.steer = kAxisMax;
    for (int i = 0; i < rally::sim::kStepsPerSecond / 2; ++i) {
        world.step(turn);
    }
    MESSAGE("rumbo tras girar a la derecha: ", heading(world.car_pose()), " rad");
    CHECK(heading(world.car_pose()) > 0.1);
}

TEST_CASE("sim_world: 60 s of random inputs are bit-identical across two runs") {
    const rally::sim::JoltRuntime runtime;
    const RunResult first = run_random_inputs();
    const RunResult second = run_random_inputs();

    // Las entradas tienen que haber movido el coche; si no, el test pasaría sin probar nada.
    CHECK(first.max_distance > 3.0);
    CHECK(same_pose(first.car, second.car));

    REQUIRE(first.poses.size() == second.poses.size());
    for (std::size_t i = 0; i < first.poses.size(); ++i) {
        CHECK(same_pose(first.poses[i], second.poses[i]));
    }
    REQUIRE(!first.state.empty());
    CHECK(first.state == second.state);
    CHECK(first.hash == second.hash);
}

TEST_CASE("sim_world: reset puts the car back at its spawn, still, in the same step") {
    const rally::sim::JoltRuntime runtime;
    SimWorld world;
    world.build(arena());
    world.seal();
    const BodyPose spawn = world.car_pose();

    // Conduce girando un segundo: lejos y girado.
    SimInput drive;
    drive.throttle = kAxisMax;
    drive.steer = kAxisMax;
    for (int i = 0; i < rally::sim::kStepsPerSecond; ++i) {
        world.step(drive);
    }
    const BodyPose away = world.car_pose();
    REQUIRE(std::hypot(away.x - spawn.x, away.z - spawn.z) > 1.0);
    REQUIRE(std::abs(heading(away)) > 0.2);
    REQUIRE(world.controls().throttle.applied == kAxisMax);

    // Recolocar sin soltar nada: al volver de step() está en la salida, mirando al frente
    // (orientación identidad) y quieto.
    SimInput reset = drive;
    reset.buttons |= rally::sim::kButtonReset;
    world.step(reset);
    const BodyPose back = world.car_pose();
    CHECK(same_pose(back, BodyPose{spawn.x, spawn.y, spawn.z, 0.0, 0.0, 0.0, 1.0}));
    const rally::sim::BodyVelocity still = world.car_velocity();
    CHECK(still.vx == 0.0f);
    CHECK(still.vy == 0.0f);
    CHECK(still.vz == 0.0f);
    CHECK(still.wx == 0.0f);
    CHECK(still.wy == 0.0f);
    CHECK(still.wz == 0.0f);
    CHECK(world.last_impact() == 0.0f);
    for (int i = 0; i < rally::sim::kWheelCount; ++i) {
        CHECK(world.car_controller().wheel(i).omega == 0.0f);
    }
    // Los ejes aplicados vuelven a 0; los botones del paso anterior se quedan.
    const rally::sim::ControlState& controls = world.controls();
    CHECK(controls.steer.applied == 0);
    CHECK(controls.throttle.applied == 0);
    CHECK(controls.brake.applied == 0);
    CHECK(controls.handbrake == 0);
    CHECK(controls.previous_buttons == reset.buttons);

    // Mantener recolocar no vuelve a recolocar: es una pulsación. El coche vuelve a moverse.
    for (int i = 0; i < rally::sim::kStepsPerSecond / 2; ++i) {
        world.step(reset);
    }
    const BodyPose moved = world.car_pose();
    CHECK(std::hypot(moved.x - spawn.x, moved.z - spawn.z) > 0.1);
}

TEST_CASE("sim_world: state fingerprint after 60 s of random inputs matches the stored one") {
    const rally::sim::JoltRuntime runtime;
    const std::uint64_t fingerprint = run_random_inputs().hash;

    char message[320];
    std::snprintf(message, sizeof(message),
                  "huella actual 0x%016llxull, guardada 0x%016llxull. Si el cambio de la "
                  "simulación es intencionado, copia la actual en kExpectedFingerprint "
                  "(tests/test_sim_world.cpp) y dilo en el mensaje del commit.",
                  static_cast<unsigned long long>(fingerprint),
                  static_cast<unsigned long long>(kExpectedFingerprint));
    INFO(message);
    CHECK(fingerprint == kExpectedFingerprint);
}

TEST_CASE("sim_world: capturing telemetry leaves the fingerprint unchanged") {
    const rally::sim::JoltRuntime runtime;
    CHECK(run_random_inputs(true).hash == kExpectedFingerprint);
}

TEST_CASE("sim_world: step allocates nothing of ours and only the known Jolt blocks") {
    const rally::sim::JoltRuntime runtime;
    const std::vector<SimInput> inputs = random_inputs(5);
    SimWorld world;
    world.build(arena());
    world.seal();
    // Con la captura de la telemetría, lo mismo: solo lee y no reserva.
    SUBCASE("without telemetry") {}
    SUBCASE("with telemetry") { world.set_telemetry(true); }

    std::size_t next = 0;
    for (int i = 0; i < kWarmupSteps; ++i) {
        world.step(inputs[next++]);
    }

    // Reservas de Jolt de cada paso: 0 si nada ha cambiado de caja envolvente, o el bloque
    // conocido. Se cuentan los pasos de cada clase sin reservar nada nosotros.
    const JoltAllocationCounter jolt;
    int steps_with_blocks = 0;
    int steps_without = 0;
    int steps_other = 0;
    const std::uint64_t ours_before = rally::tests::global_new_count();
    for (int i = 0; i < kCountedSteps; ++i) {
        const int before = jolt.count();
        world.step(inputs[next++]);
        const int step_count = jolt.count() - before;
        if (step_count == kJoltAllocationsPerStep) {
            ++steps_with_blocks;
        } else if (step_count == 0) {
            ++steps_without;
        } else {
            ++steps_other;
        }
    }
    const std::uint64_t ours = rally::tests::global_new_count() - ours_before;

    // El contador global está activo: si no, el 0 de abajo no probaría nada.
    const std::uint64_t probe_before = rally::tests::global_new_count();
    const auto probe = std::make_unique<int>(1);
    REQUIRE(rally::tests::global_new_count() == probe_before + 1);

    // Nuestro código no reserva nada en el paso. Jolt tampoco pasa por el operator new
    // global: si lo hiciera, sus reservas aparecerían aquí también.
    CHECK(ours == 0);
    INFO("Si un cambio intencionado altera las reservas de Jolt, actualiza "
         "kJoltAllocationsPerStep o kExpectedJoltAllocations (tests/test_sim_world.cpp) y "
         "dilo en el mensaje del commit.");
    MESSAGE("pasos con ", kJoltAllocationsPerStep, " reservas de Jolt: ", steps_with_blocks,
            "; sin reservas: ", steps_without);
    CHECK(steps_other == 0);
    // Las entradas mueven el coche en la mayoría de los pasos: si no hubiera ninguno con
    // reservas, el contador de Jolt no estaría probando nada.
    CHECK(steps_with_blocks > kCountedSteps / 2);
    CHECK(jolt.count() == kExpectedJoltAllocations);
}

namespace {

using rally::platform::kImpactThreshold;

// Mayor impacto de `steps` pasos con la misma entrada.
float max_impact(SimWorld& world, const SimInput& input, int steps) {
    float peak = 0.0f;
    for (int i = 0; i < steps; ++i) {
        world.step(input);
        peak = std::max(peak, world.last_impact());
    }
    return peak;
}

} // namespace

TEST_CASE("sim_world impact: driving and turning on the floor stays below the threshold") {
    const rally::sim::JoltRuntime runtime;
    SimWorld world;
    world.build(arena());
    world.seal();
    max_impact(world, SimInput{}, rally::sim::kStepsPerSecond);

    // Acelerador a fondo 0,5 s hacia -z con el teclado (sube con el limitador).
    SimInput drive;
    drive.throttle = kAxisMax;
    drive.buttons = kButtonThrottleDigital;
    const float driving = max_impact(world, drive, rally::sim::kStepsPerSecond / 2);
    // Suelta y gira con el freno hasta pararse, sin llegar al muro.
    SimInput coast;
    coast.steer = kAxisMax;
    coast.brake = kAxisMax / 2;
    const float turning = max_impact(world, coast, 2 * rally::sim::kStepsPerSecond);
    MESSAGE("impacto conduciendo: ", driving, " m/s; girando y frenando: ", turning, " m/s");
    CHECK(std::abs(world.car_pose().z) > 1.0); // se ha movido de verdad
    CHECK(driving < kImpactThreshold);
    CHECK(turning < kImpactThreshold);
}

TEST_CASE("sim_world impact: hitting a wall is above the threshold") {
    const rally::sim::JoltRuntime runtime;
    SimWorld world;
    world.build(arena());
    world.seal();
    SimInput drive;
    drive.throttle = kAxisMax;
    // Hacia -z hasta el muro (a 9,5 m del centro).
    const float hit = max_impact(world, drive, 3 * rally::sim::kStepsPerSecond);
    MESSAGE("impacto contra el muro: ", hit, " m/s");
    CHECK(hit > kImpactThreshold);
}
