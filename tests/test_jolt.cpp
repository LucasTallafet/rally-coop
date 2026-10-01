#include "sim/jolt_config.h"
#include "sim/jolt_runtime.h"

#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/StateRecorderImpl.h>
#include <doctest/doctest.h>

#include <bit>
#include <cstdint>
#include <cstring>
#include <string>

namespace {

// Capas mínimas para el test: una estática y una móvil.
namespace layers {
constexpr JPH::ObjectLayer kStatic = 0;
constexpr JPH::ObjectLayer kMoving = 1;
constexpr JPH::uint kCount = 2;
} // namespace layers

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return layers::kCount; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return JPH::BroadPhaseLayer(static_cast<JPH::BroadPhaseLayer::Type>(layer));
    }
};

class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer bp) const override {
        return layer == layers::kMoving ||
               bp.GetValue() == static_cast<JPH::BroadPhaseLayer::Type>(layers::kMoving);
    }
};

class ObjectPairs final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        return a == layers::kMoving || b == layers::kMoving;
    }
};

struct DropResult {
    JPH::RVec3 position;
    JPH::Quat rotation;
    JPH::Vec3 linear_velocity;
    JPH::Vec3 angular_velocity;
    std::string saved_state; // SaveState completo del PhysicsSystem
};

constexpr int kStepsPerSecond = 240;
constexpr int kSeconds = 10;
constexpr float kDeltaTime = 1.0f / kStepsPerSecond;
constexpr float kHalfExtent = 0.5f;

// Deja caer una caja girada desde 10 m sobre un suelo durante 10 s a 240 Hz, en un hilo.
DropResult run_box_drop() {
    JPH::TempAllocatorImpl temp_allocator(4 * 1024 * 1024);
    JPH::JobSystemSingleThreaded job_system(JPH::cMaxPhysicsJobs);

    const BroadPhaseLayers broad_phase_layers;
    const ObjectVsBroadPhase object_vs_broad_phase;
    const ObjectPairs object_pairs;

    JPH::PhysicsSystem system;
    system.Init(16, 0, 16, 16, broad_phase_layers, object_vs_broad_phase, object_pairs);

    JPH::BodyInterface& bodies = system.GetBodyInterface();

    const JPH::BodyCreationSettings floor(new JPH::BoxShape(JPH::Vec3(50.0f, 1.0f, 50.0f)),
                                          JPH::RVec3(0.0, -1.0, 0.0), JPH::Quat::sIdentity(),
                                          JPH::EMotionType::Static, layers::kStatic);
    bodies.CreateAndAddBody(floor, JPH::EActivation::DontActivate);

    // Girada sobre un eje no alineado y con giro inicial, para que rebote y vuelque.
    JPH::BodyCreationSettings box(
        new JPH::BoxShape(JPH::Vec3::sReplicate(kHalfExtent)), JPH::RVec3(0.0, 10.0, 0.0),
        JPH::Quat::sRotation(JPH::Vec3(1.0f, 1.0f, 0.3f).Normalized(), 0.7f),
        JPH::EMotionType::Dynamic, layers::kMoving);
    box.mAngularVelocity = JPH::Vec3(0.4f, -1.1f, 2.3f);
    const JPH::BodyID box_id = bodies.CreateAndAddBody(box, JPH::EActivation::Activate);

    for (int step = 0; step < kStepsPerSecond * kSeconds; ++step) {
        const JPH::EPhysicsUpdateError error =
            system.Update(kDeltaTime, 1, &temp_allocator, &job_system);
        REQUIRE(error == JPH::EPhysicsUpdateError::None);
    }

    DropResult result;
    bodies.GetPositionAndRotation(box_id, result.position, result.rotation);
    result.linear_velocity = bodies.GetLinearVelocity(box_id);
    result.angular_velocity = bodies.GetAngularVelocity(box_id);

    JPH::StateRecorderImpl recorder;
    system.SaveState(recorder);
    result.saved_state = recorder.GetData();
    return result;
}

template <typename T>
bool same_bytes(const T& a, const T& b) {
    return std::memcmp(&a, &b, sizeof(T)) == 0;
}

} // namespace

TEST_CASE("jolt: game code and library were built with the same JPH_* definitions") {
    CHECK(rally::sim::jolt_abi_matches());
}

TEST_CASE("jolt: box drop is bit-identical across two runs") {
    const rally::sim::JoltRuntime runtime;

    const DropResult first = run_box_drop();
    const DropResult second = run_box_drop();

    // La caja tiene que haber caído y quedado en reposo sobre el suelo; si no, el test
    // podría pasar sin que la simulación hiciera nada.
    CHECK(first.position.GetY() > kHalfExtent - 0.05);
    CHECK(first.position.GetY() < kHalfExtent * 1.8);
    CHECK(first.linear_velocity.Length() < 0.01f);

    // Doble precisión: la posición se compara como doubles, bit a bit.
    static_assert(sizeof(JPH::Real) == sizeof(double));
    CHECK(std::bit_cast<std::uint64_t>(first.position.GetX()) ==
          std::bit_cast<std::uint64_t>(second.position.GetX()));
    CHECK(std::bit_cast<std::uint64_t>(first.position.GetY()) ==
          std::bit_cast<std::uint64_t>(second.position.GetY()));
    CHECK(std::bit_cast<std::uint64_t>(first.position.GetZ()) ==
          std::bit_cast<std::uint64_t>(second.position.GetZ()));
    CHECK(same_bytes(first.rotation, second.rotation));
    CHECK(same_bytes(first.linear_velocity, second.linear_velocity));
    CHECK(same_bytes(first.angular_velocity, second.angular_velocity));

    REQUIRE(!first.saved_state.empty());
    CHECK(first.saved_state == second.saved_state);
}
