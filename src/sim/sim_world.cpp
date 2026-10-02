#include "sim/sim_world.h"

#include "sim/check.h"
#include "sim/fnv1a.h"
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

constexpr float kMoveAcceleration = 10.0f; // m/s² con el acelerador a fondo
constexpr float kTurnRate = 1.5f;          // rad/s alrededor de Y con la dirección a fondo
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
    float controlled_damping = 0.0f; // amortiguamiento lineal, para last_impact
    // Salida del cuerpo controlado (recolocar). En doubles y no en RVec3, que está alineado
    // a 32 bytes y rellenaría Impl.
    double spawn[3] = {0.0, 0.0, 0.0};
    // Mandos aplicados y botones del paso anterior. Es estado de la simulación que no está
    // en el SaveState de Jolt: en el cambio de host (fase 5) viaja con el estado del juego.
    ControlState controls;
    float last_impact = 0.0f; // salida de diagnóstico; no es estado de la simulación
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
        JPH::BodyCreationSettings settings(
            new JPH::BoxShape(JPH::Vec3(box.half_x, box.half_y, box.half_z)),
            JPH::RVec3(box.x, box.y, box.z), JPH::Quat::sIdentity(),
            dynamic ? JPH::EMotionType::Dynamic : JPH::EMotionType::Static,
            dynamic ? layers::kMoving : layers::kStatic);
        if (i == desc.controlled) {
            // Como un coche simplificado: se traslada en los tres ejes y solo gira alrededor
            // de la vertical. No vuelca ni cabecea.
            settings.mAllowedDOFs = JPH::EAllowedDOFs::TranslationX |
                                    JPH::EAllowedDOFs::TranslationY |
                                    JPH::EAllowedDOFs::TranslationZ | JPH::EAllowedDOFs::RotationY;
        }
        const JPH::Body* body = bodies.CreateBody(settings);
        RALLY_CHECK(body != nullptr, "SimWorld::build: Jolt no tiene cuerpos libres");
        w.bodies.push_back(body->GetID());
        if (i == desc.controlled) {
            w.controlled = body->GetID();
            w.controlled_mass = settings.GetMassProperties().mMass;
            w.controlled_damping = settings.mLinearDamping;
            w.spawn[0] = box.x;
            w.spawn[1] = box.y;
            w.spawn[2] = box.z;
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

    w.controls.step(input);
    const bool reset_pressed = (w.controls.pressed & kButtonReset) != 0;
    if (reset_pressed) {
        w.controls.zero_axes();
    }
    // Velocidad que tendría el cuerpo controlado tras el paso sin contactos, para el impacto.
    JPH::Vec3 expected_velocity = JPH::Vec3::sZero();
    if (!w.controlled.IsInvalid()) {
        JPH::BodyInterface& bodies = w.system.GetBodyInterfaceNoLock();
        constexpr float kAxisScale = 1.0f / kAxisMax;
        const float throttle = rmath::clamp(
            static_cast<float>(w.controls.throttle.applied - w.controls.brake.applied) * kAxisScale,
            -1.0f, 1.0f);
        const float steer =
            rmath::clamp(static_cast<float>(w.controls.steer.applied) * kAxisScale, -1.0f, 1.0f);
        // Adelante es -z local. Con la rotación limitada a Y, el empuje queda horizontal.
        JPH::Vec3 input_acceleration = JPH::Vec3::sZero();
        if (throttle != 0.0f) {
            const JPH::Vec3 forward = bodies.GetRotation(w.controlled) * JPH::Vec3(0, 0, -1);
            input_acceleration = forward * (throttle * kMoveAcceleration);
            bodies.AddForce(w.controlled, input_acceleration * w.controlled_mass);
        }
        // La dirección fija la velocidad de giro en cada paso (también a 0): sin dirección no
        // gira, aunque un choque lo empuje de lado. Un giro positivo alrededor de +y lleva -z
        // hacia -x: girar a la derecha es negativo.
        bodies.SetAngularVelocity(w.controlled, JPH::Vec3(0.0f, -steer * kTurnRate, 0.0f));
        if ((w.controls.pressed & kButtonJump) != 0) {
            bodies.AddImpulse(w.controlled, JPH::Vec3(0.0f, w.controlled_mass * kJumpSpeed, 0.0f));
        }
        // Como integra Jolt (MotionProperties::ApplyForceTorqueAndDragInternal): gravedad y
        // fuerzas, y después el amortiguamiento. El impulso de salto ya está en la velocidad.
        expected_velocity = (bodies.GetLinearVelocity(w.controlled) +
                             (w.system.GetGravity() + input_acceleration) * kStepSeconds) *
                            rmath::max(0.0f, 1.0f - w.controlled_damping * kStepSeconds);
    }

    const JPH::EPhysicsUpdateError error =
        w.system.Update(kStepSeconds, 1, &w.temp_allocator, &w.job_system);
    RALLY_CHECK(error == JPH::EPhysicsUpdateError::None,
                "SimWorld::step: Jolt devolvió un error en Update");

    if (!w.controlled.IsInvalid()) {
        JPH::BodyInterface& bodies = w.system.GetBodyInterfaceNoLock();
        if (reset_pressed) {
            // Mover y poner a cero, nunca quitar y añadir. Un traslado no es un impacto.
            bodies.SetPositionAndRotation(w.controlled,
                                          JPH::RVec3(w.spawn[0], w.spawn[1], w.spawn[2]),
                                          JPH::Quat::sIdentity(), JPH::EActivation::Activate);
            bodies.SetLinearAndAngularVelocity(w.controlled, JPH::Vec3::sZero(),
                                               JPH::Vec3::sZero());
            w.last_impact = 0.0f;
        } else {
            const JPH::Vec3 velocity = bodies.GetLinearVelocity(w.controlled);
            w.last_impact = (velocity - expected_velocity).Length();
        }
    }
}

const ControlState& SimWorld::controls() const { return impl_->controls; }

float SimWorld::last_impact() const { return impl_->last_impact; }

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

BodyVelocity SimWorld::velocity(int index) const {
    RALLY_CHECK(index >= 0 && index < body_count(), "SimWorld::velocity: índice fuera de rango");
    JPH::Vec3 linear;
    JPH::Vec3 angular;
    impl_->system.GetBodyInterfaceNoLock().GetLinearAndAngularVelocity(
        impl_->bodies[static_cast<std::size_t>(index)], linear, angular);
    return BodyVelocity{linear.GetX(),  linear.GetY(),  linear.GetZ(),
                        angular.GetX(), angular.GetY(), angular.GetZ()};
}

std::string SimWorld::save_state() const {
    JPH::StateRecorderImpl recorder;
    impl_->system.SaveState(recorder);
    return recorder.GetData();
}

namespace {

void add_axis(Fnv1a64& hash, const SmoothedAxis& axis) {
    hash.add_u32(static_cast<std::uint32_t>(axis.applied));
    hash.add_byte(axis.digital ? 1 : 0);
}

} // namespace

std::uint64_t SimWorld::state_hash() const {
    Fnv1a64 hash;
    hash.add_bytes(save_state());
    const ControlState& c = impl_->controls;
    add_axis(hash, c.steer);
    add_axis(hash, c.throttle);
    add_axis(hash, c.brake);
    hash.add_u32(static_cast<std::uint32_t>(c.handbrake));
    hash.add_u16(c.previous_buttons);
    hash.add_u16(c.pressed);
    return hash.value();
}

} // namespace rally::sim
