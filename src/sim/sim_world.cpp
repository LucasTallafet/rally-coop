#include "sim/sim_world.h"

#include "sim/check.h"
#include "sim/jolt_config.h"
#include "sim/layers.h"
#include "sim/rmath.h"

#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/StateRecorderImpl.h>

#include <cstddef>

namespace rally::sim {

namespace {

// Capacidades del PhysicsSystem. Jolt reserva todo en Init; el tramo (fase 3) las revisará.
constexpr JPH::uint kMaxBodies = 1024;
constexpr JPH::uint kMaxBodyPairs = 1024;
constexpr JPH::uint kMaxContactConstraints = 1024;
constexpr std::size_t kTempAllocatorBytes = 4 * 1024 * 1024;

constexpr float kMoveAcceleration = 10.0f; // m/s² con el eje a fondo
constexpr float kJumpSpeed = 5.0f;         // m/s hacia arriba que suma el impulso de salto

enum class State : std::uint8_t { Empty, Built, Sealed };

} // namespace

struct SimWorld::Impl {
    Impl() : temp_allocator(kTempAllocatorBytes), job_system(JPH::cMaxPhysicsJobs) {
        system.Init(kMaxBodies, 0, kMaxBodyPairs, kMaxContactConstraints, broad_phase_layers,
                    object_vs_broad_phase, object_pairs);
    }

    // Las capas tienen que vivir más que el PhysicsSystem: van antes.
    const layers::BroadPhaseLayers broad_phase_layers;
    const layers::ObjectVsBroadPhase object_vs_broad_phase;
    const layers::ObjectPairs object_pairs;
    JPH::TempAllocatorImpl temp_allocator;
    JPH::JobSystemSingleThreaded job_system;
    JPH::PhysicsSystem system;

    std::vector<JPH::BodyID> bodies; // en el orden de la descripción
    JPH::BodyID controlled;          // inválido si no hay cuerpo controlado
    float controlled_mass = 0.0f;
    // Botones del paso anterior, para detectar pulsaciones. Es estado de la simulación que no
    // está en el SaveState de Jolt: en el cambio de host (fase 5) viaja con el estado del juego.
    std::uint16_t previous_buttons = 0;
    State state = State::Empty;
};

SimWorld::SimWorld() : impl_(std::make_unique<Impl>()) {}

SimWorld::~SimWorld() = default;

void SimWorld::build(const WorldDesc& desc) {
    Impl& w = *impl_;
    RALLY_CHECK(w.state == State::Empty,
                "SimWorld::build: el mundo ya está construido (tras seal() no se añaden cuerpos)");
    RALLY_CHECK(desc.bodies.size() <= kMaxBodies, "SimWorld::build: demasiados cuerpos");
    const int count = static_cast<int>(desc.bodies.size());
    RALLY_CHECK(desc.controlled >= -1 && desc.controlled < count,
                "SimWorld::build: índice de cuerpo controlado fuera de rango");
    RALLY_CHECK(desc.controlled == -1 ||
                    desc.bodies[static_cast<std::size_t>(desc.controlled)].motion ==
                        BodyMotion::Dynamic,
                "SimWorld::build: el cuerpo controlado tiene que ser dinámico");

    JPH::BodyInterface& bodies = w.system.GetBodyInterfaceNoLock();
    w.bodies.reserve(desc.bodies.size());
    for (int i = 0; i < count; ++i) {
        const BoxDesc& box = desc.bodies[static_cast<std::size_t>(i)];
        const bool dynamic = box.motion == BodyMotion::Dynamic;
        const JPH::BodyCreationSettings settings(
            new JPH::BoxShape(JPH::Vec3(box.half_x, box.half_y, box.half_z)),
            JPH::RVec3(box.x, box.y, box.z), JPH::Quat::sIdentity(),
            dynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static,
            dynamic ? layers::kMoving : layers::kStatic);
        const JPH::Body* body = bodies.CreateBody(settings);
        RALLY_CHECK(body != nullptr, "SimWorld::build: Jolt no tiene cuerpos libres");
        w.bodies.push_back(body->GetID());
        if (i == desc.controlled) {
            w.controlled = body->GetID();
            w.controlled_mass = settings.GetMassProperties().mMass;
        }
    }

    // AddBodiesPrepare reordena el array por capas: se le pasa una copia para conservar el
    // orden de la descripción en `bodies`.
    if (count > 0) {
        std::vector<JPH::BodyID> batch = w.bodies;
        const JPH::BodyInterface::AddState add_state = bodies.AddBodiesPrepare(batch.data(), count);
        bodies.AddBodiesFinalize(batch.data(), count, add_state, JPH::EActivation::Activate);
    }
    w.state = State::Built;
}

void SimWorld::seal() {
    Impl& w = *impl_;
    RALLY_CHECK(w.state == State::Built, "SimWorld::seal: falta build() o ya está sellado");
    w.system.OptimizeBroadPhase();
    w.state = State::Sealed;
}

bool SimWorld::is_sealed() const { return impl_->state == State::Sealed; }

void SimWorld::step(const SimInput& input) {
    Impl& w = *impl_;
    RALLY_CHECK(w.state == State::Sealed,
                "SimWorld::step: el mundo no está sellado (falta seal())");

    if (!w.controlled.IsInvalid()) {
        JPH::BodyInterface& bodies = w.system.GetBodyInterfaceNoLock();
        constexpr float kAxisScale = 1.0f / kAxisMax;
        float move_x = rmath::clamp(input.move_x * kAxisScale, -1.0f, 1.0f);
        float move_z = rmath::clamp(input.move_z * kAxisScale, -1.0f, 1.0f);
        const float length_sq = move_x * move_x + move_z * move_z;
        if (length_sq > 1.0f) {
            const float inverse_length = 1.0f / rmath::sqrt(length_sq);
            move_x *= inverse_length;
            move_z *= inverse_length;
        }
        if (length_sq > 0.0f) {
            const float force = w.controlled_mass * kMoveAcceleration;
            bodies.AddForce(w.controlled, JPH::Vec3(move_x * force, 0.0f, move_z * force));
        }
        const bool jump_pressed =
            (input.buttons & kButtonJump) != 0 && (w.previous_buttons & kButtonJump) == 0;
        if (jump_pressed) {
            bodies.AddImpulse(w.controlled, JPH::Vec3(0.0f, w.controlled_mass * kJumpSpeed, 0.0f));
        }
    }
    w.previous_buttons = input.buttons;

    const JPH::EPhysicsUpdateError error =
        w.system.Update(kStepSeconds, 1, &w.temp_allocator, &w.job_system);
    RALLY_CHECK(error == JPH::EPhysicsUpdateError::None,
                "SimWorld::step: Jolt devolvió un error en Update");
}

int SimWorld::body_count() const { return static_cast<int>(impl_->bodies.size()); }

std::uint32_t SimWorld::body_id(int index) const {
    RALLY_CHECK(index >= 0 && index < body_count(), "SimWorld::body_id: índice fuera de rango");
    return impl_->bodies[static_cast<std::size_t>(index)].GetIndexAndSequenceNumber();
}

BodyPose SimWorld::pose(int index) const {
    RALLY_CHECK(index >= 0 && index < body_count(), "SimWorld::pose: índice fuera de rango");
    JPH::RVec3 position;
    JPH::Quat rotation;
    impl_->system.GetBodyInterfaceNoLock().GetPositionAndRotation(
        impl_->bodies[static_cast<std::size_t>(index)], position, rotation);
    return BodyPose{position.GetX(), position.GetY(), position.GetZ(), rotation.GetX(),
                    rotation.GetY(), rotation.GetZ(), rotation.GetW()};
}

std::string SimWorld::save_state() const {
    JPH::StateRecorderImpl recorder;
    impl_->system.SaveState(recorder);
    return recorder.GetData();
}

} // namespace rally::sim
