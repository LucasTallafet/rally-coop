#include "new_counter.h"
#include "sim/jolt_config.h"
#include "sim/jolt_runtime.h"
#include "sim/pcg32.h"
#include "sim/sim_input.h"
#include "sim/sim_world.h"

#include <Jolt/Core/Memory.h>
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
using rally::sim::kButtonJump;
using rally::sim::SimInput;
using rally::sim::SimWorld;
using rally::sim::WorldDesc;

// Huella del SaveState tras 60 s de entradas pseudoaleatorias (run_random_inputs).
// Si el test de huella falla por un cambio intencionado, se copia aquí el valor nuevo y se
// dice en el mensaje del commit.
constexpr std::uint64_t kExpectedFingerprint = 0xb9600f45966b5f98ull;

constexpr int kSecondsOfInput = 60;
constexpr int kStepsPerFrame = 4; // la entrada cambia cada fotograma de 60 fps
constexpr int kControlled = 5;

constexpr int kWarmupSteps = 10;
constexpr int kCountedSteps = 1000;
// Reservas de Jolt 5.6.0 en cada paso que deja sucio el árbol de la capa móvil (algún
// cuerpo cambió de caja envolvente; si no, ninguna): la broad phase lo reconstruye y
// QuadTree::UpdatePrepare y QuadTree::BuildTree hacen un new[] cada uno (con su delete[]
// en la misma llamada). Desde que la caja controlada solo gira en Y hay pasos sin
// reservas (un 25 % con las entradas de este test). Tamaño constante y sin
// efecto en la reproducibilidad; aceptado (docs/progreso.md, hallazgos). Si un cambio
// intencionado lo altera, se actualiza como la huella.
constexpr int kJoltAllocationsPerStep = 2;
// Total de reservas de Jolt en los kCountedSteps pasos medidos con las entradas del test
// (753 pasos con reservas y 247 sin ellas). Si un cambio intencionado lo altera, se
// actualiza como la huella y se dice en el mensaje del commit.
constexpr int kExpectedJoltAllocations = 1506;

// Suelo de 20 × 20 m con cuatro muros de 3 m, la caja controlada apoyada en el centro y
// otra caja suelta para que haya contactos entre cuerpos dinámicos.
WorldDesc arena() {
    WorldDesc desc;
    desc.bodies = {
        {BodyMotion::Static, 0.0, -0.5, 0.0, 10.0f, 0.5f, 10.0f},
        {BodyMotion::Static, 0.0, 1.5, -10.5, 11.0f, 1.5f, 0.5f},
        {BodyMotion::Static, 0.0, 1.5, 10.5, 11.0f, 1.5f, 0.5f},
        {BodyMotion::Static, -10.5, 1.5, 0.0, 0.5f, 1.5f, 10.0f},
        {BodyMotion::Static, 10.5, 1.5, 0.0, 0.5f, 1.5f, 10.0f},
        {BodyMotion::Dynamic, 0.0, 0.5, 0.0, 0.5f, 0.5f, 0.5f},
        {BodyMotion::Dynamic, 3.0, 0.5, 2.0, 0.5f, 0.5f, 0.5f},
    };
    desc.controlled = kControlled;
    return desc;
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
        input.throttle = random_axis(rng);
        input.steer = random_axis(rng);
        input.buttons = rng.bounded(90) == 0 ? kButtonJump : 0;
        for (int i = 0; i < kStepsPerFrame && inputs.size() < inputs.capacity(); ++i) {
            inputs.push_back(input);
        }
    }
    return inputs;
}

struct RunResult {
    std::string state;
    std::vector<BodyPose> poses;
    double max_distance = 0.0; // distancia horizontal máxima de la caja controlada al centro
};

RunResult run_random_inputs() {
    const std::vector<SimInput> inputs = random_inputs(kSecondsOfInput);
    SimWorld world;
    world.build(arena());
    world.seal();

    RunResult result;
    double max_distance_sq = 0.0;
    for (const SimInput& input : inputs) {
        world.step(input);
        const BodyPose pose = world.pose(kControlled);
        max_distance_sq = std::max(max_distance_sq, pose.x * pose.x + pose.z * pose.z);
    }
    result.max_distance = std::sqrt(max_distance_sq);
    for (int i = 0; i < world.body_count(); ++i) {
        result.poses.push_back(world.pose(i));
    }
    result.state = world.save_state();
    return result;
}

// Ángulo de giro alrededor de +y de una orientación que solo gira en Y.
double yaw(const BodyPose& pose) { return 2.0 * std::atan2(pose.qy, pose.qw); }

// La caja controlada solo gira alrededor de Y: el cuaternión no tiene componente x ni z.
bool upright(const BodyPose& pose) { return std::abs(pose.qx) < 1e-6 && std::abs(pose.qz) < 1e-6; }

bool same_bits(double a, double b) {
    return std::bit_cast<std::uint64_t>(a) == std::bit_cast<std::uint64_t>(b);
}

bool same_pose(const BodyPose& a, const BodyPose& b) {
    return same_bits(a.x, b.x) && same_bits(a.y, b.y) && same_bits(a.z, b.z) &&
           same_bits(a.qx, b.qx) && same_bits(a.qy, b.qy) && same_bits(a.qz, b.qz) &&
           same_bits(a.qw, b.qw);
}

// FNV-1a de 64 bits.
std::uint64_t fnv1a64(const std::string& bytes) {
    std::uint64_t hash = 0xcbf2'9ce4'8422'2325ull;
    for (const char c : bytes) {
        hash ^= static_cast<unsigned char>(c);
        hash *= 0x0000'0100'0000'01b3ull;
    }
    return hash;
}

// Cuenta las reservas que pasan por el asignador de Jolt mientras vive.
class JoltAllocationCounter {
public:
    JoltAllocationCounter()
        : allocate_(JPH::Allocate), reallocate_(JPH::Reallocate),
          aligned_allocate_(JPH::AlignedAllocate) {
        s_count = 0;
        s_allocate = allocate_;
        s_reallocate = reallocate_;
        s_aligned_allocate = aligned_allocate_;
        JPH::Allocate = [](std::size_t size) {
            ++s_count;
            return s_allocate(size);
        };
        JPH::Reallocate = [](void* block, std::size_t old_size, std::size_t new_size) {
            ++s_count;
            return s_reallocate(block, old_size, new_size);
        };
        JPH::AlignedAllocate = [](std::size_t size, std::size_t alignment) {
            ++s_count;
            return s_aligned_allocate(size, alignment);
        };
    }
    ~JoltAllocationCounter() {
        JPH::Allocate = allocate_;
        JPH::Reallocate = reallocate_;
        JPH::AlignedAllocate = aligned_allocate_;
    }
    JoltAllocationCounter(const JoltAllocationCounter&) = delete;
    JoltAllocationCounter& operator=(const JoltAllocationCounter&) = delete;

    int count() const { return s_count; }

private:
    static inline int s_count = 0;
    static inline JPH::AllocateFunction s_allocate = nullptr;
    static inline JPH::ReallocateFunction s_reallocate = nullptr;
    static inline JPH::AlignedAllocateFunction s_aligned_allocate = nullptr;

    JPH::AllocateFunction allocate_;
    JPH::ReallocateFunction reallocate_;
    JPH::AlignedAllocateFunction aligned_allocate_;
};

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

TEST_CASE("sim_world: steer turns the controlled box, throttle pushes it forward, jump lifts it") {
    const rally::sim::JoltRuntime runtime;
    SimWorld world;
    world.build(arena());
    world.seal();

    // Sin entrada, la caja se queda donde está.
    for (int i = 0; i < rally::sim::kStepsPerSecond; ++i) {
        world.step(SimInput{});
    }
    const BodyPose rest = world.pose(kControlled);
    CHECK(std::abs(rest.x) < 1e-3);
    CHECK(std::abs(rest.z) < 1e-3);
    CHECK(std::abs(rest.y - 0.5) < 0.02);

    // Un segundo con la dirección a fondo a la derecha: gira unos 1,5 rad en el sitio (de
    // mirar hacia -z a mirar casi hacia +x) sin volcar. El rozamiento con el suelo frena un
    // poco el giro dentro de cada paso (sale -1,465 rad).
    SimInput turn;
    turn.steer = kAxisMax;
    for (int i = 0; i < rally::sim::kStepsPerSecond; ++i) {
        world.step(turn);
    }
    const BodyPose turned = world.pose(kControlled);
    CHECK(yaw(turned) == doctest::Approx(-1.5).epsilon(0.05));
    CHECK(upright(turned));
    CHECK(std::abs(turned.x - rest.x) < 1e-3);
    CHECK(std::abs(turned.z - rest.z) < 1e-3);

    // Medio segundo de acelerador sin dirección: avanza en su dirección adelante (-z local)
    // y ya no gira.
    SimInput push;
    push.throttle = kAxisMax;
    for (int i = 0; i < rally::sim::kStepsPerSecond / 2; ++i) {
        world.step(push);
    }
    const BodyPose pushed = world.pose(kControlled);
    const double heading = yaw(turned);
    const double forward_x = -std::sin(heading);
    const double forward_z = -std::cos(heading);
    const double dx = pushed.x - turned.x;
    const double dz = pushed.z - turned.z;
    const double distance = std::sqrt(dx * dx + dz * dz);
    CHECK(distance > 0.5);
    CHECK((dx * forward_x + dz * forward_z) / distance > 0.999);
    CHECK(yaw(pushed) == doctest::Approx(heading).epsilon(1e-6));
    CHECK(upright(pushed));

    // El salto actúa al pulsar, no mientras se mantiene: sube y vuelve a caer una vez.
    SimInput jump;
    jump.buttons = kButtonJump;
    double highest = pushed.y;
    for (int i = 0; i < rally::sim::kStepsPerSecond * 2; ++i) {
        world.step(jump);
        highest = std::max(highest, world.pose(kControlled).y);
    }
    CHECK(highest > 1.5);
    CHECK(std::abs(world.pose(kControlled).y - 0.5) < 0.02);
}

TEST_CASE("sim_world: 60 s of random inputs are bit-identical across two runs") {
    const rally::sim::JoltRuntime runtime;
    const RunResult first = run_random_inputs();
    const RunResult second = run_random_inputs();

    // Las entradas tienen que haber movido la caja; si no, el test pasaría sin probar nada.
    CHECK(first.max_distance > 3.0);
    CHECK(upright(first.poses[kControlled]));

    REQUIRE(first.poses.size() == second.poses.size());
    for (std::size_t i = 0; i < first.poses.size(); ++i) {
        CHECK(same_pose(first.poses[i], second.poses[i]));
    }
    REQUIRE(!first.state.empty());
    CHECK(first.state == second.state);
}

TEST_CASE("sim_world: state fingerprint after 60 s of random inputs matches the stored one") {
    const rally::sim::JoltRuntime runtime;
    const std::uint64_t fingerprint = fnv1a64(run_random_inputs().state);

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

TEST_CASE("sim_world: step allocates nothing of ours and only the known Jolt blocks") {
    const rally::sim::JoltRuntime runtime;
    const std::vector<SimInput> inputs = random_inputs(5);
    SimWorld world;
    world.build(arena());
    world.seal();

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
    // Las entradas mueven la caja en la mayoría de los pasos: si no hubiera ninguno con
    // reservas, el contador de Jolt no estaría probando nada.
    CHECK(steps_with_blocks > kCountedSteps / 2);
    CHECK(jolt.count() == kExpectedJoltAllocations);
}
