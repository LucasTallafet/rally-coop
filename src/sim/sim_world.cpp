#include "sim/sim_world.h"

#include "sim/check.h"
#include "sim/fnv1a.h"
#include "sim/jolt_config.h"
#include "sim/layers.h"
#include "sim/rmath.h"

#include <Jolt/Core/JobSystemSingleThreaded.h>
#include <Jolt/Core/TempAllocator.h>
#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/RayCast.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/HeightFieldShape.h>
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

// Material del suelo de la zona: lleva su superficie (el neumático la leerá en la 2.4).
class SurfaceMaterial final : public JPH::PhysicsMaterial {
public:
    explicit SurfaceMaterial(Surface surface) : surface_(surface) {}
    const char* GetDebugName() const override { return surface_key(surface_); }
    Surface surface() const { return surface_; }

private:
    Surface surface_;
};

// Bloques de 4 × 4 muestras con 8 bits por muestra: en la rampa (la zona más inclinada) el
// error de altura es de ~1,5 mm.
constexpr JPH::uint kGroundBlockSize = 4;
constexpr JPH::uint kGroundBitsPerSample = 8;

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
    float controlled_half_y = 0.0f; // para apoyar la caja en el suelo al recolocar
    // Mandos aplicados y botones del paso anterior. Es estado de la simulación que no está
    // en el SaveState de Jolt: en el cambio de host (fase 5) viaja con el estado del juego.
    ControlState controls;
    float last_impact = 0.0f; // salida de diagnóstico; no es estado de la simulación
    // Telemetría (solo lectura; no es estado de la simulación).
    bool telemetry_enabled = false;
    std::int64_t steps = 0; // pasos desde build, para la telemetría
    CarTelemetry telemetry;
    State state = State::Empty;

    // Zona de pruebas (si la descripción la pide).
    std::unique_ptr<TestZone> zone;
    std::unique_ptr<LapTimer> lap;
    JPH::BodyID ground;
    std::uint64_t ground_hash = 0;
    std::uint64_t zone_hash = 0;
    std::size_t ground_bytes = 0;

    // Rellena `telemetry` al final de un paso. Solo lee.
    void capture_telemetry(const SimInput& input, JPH::Vec3Arg before_velocity, bool reset);
};

SimWorld::SimWorld() : impl_(std::make_unique<Impl>()) {}

SimWorld::~SimWorld() = default;

void SimWorld::build(const WorldDesc& desc) {
    Impl& w = *impl_;
    RALLY_CHECK(w.state == State::Empty,
                "SimWorld::build: el mundo ya está construido (tras seal() no se añaden cuerpos)");
    RALLY_CHECK(desc.bodies.size() + (desc.test_zone ? 1 + kWallCount : 0) <= kMaxBodies,
                "SimWorld::build: demasiados cuerpos");
    const int count = static_cast<int>(desc.bodies.size());
    RALLY_CHECK(desc.controlled >= -1 && desc.controlled < count,
                "SimWorld::build: índice de cuerpo controlado fuera de rango");
    RALLY_CHECK(desc.controlled == -1 ||
                    desc.bodies[static_cast<std::size_t>(desc.controlled)].motion ==
                        BodyMotion::Dynamic,
                "SimWorld::build: el cuerpo controlado tiene que ser dinámico");

    JPH::BodyInterface& bodies = w.system.GetBodyInterfaceNoLock();
    // Lote de cuerpos en el orden de creación: el suelo y los muros (si hay zona) y la
    // descripción.
    std::vector<JPH::BodyID> batch;
    if (desc.test_zone) {
        w.zone = std::make_unique<TestZone>(desc.blind_surface);
        w.lap = std::make_unique<LapTimer>(w.zone->finish_line());
        std::vector<float> heights;
        std::vector<std::uint8_t> surfaces;
        w.zone->fill_ground(heights, surfaces);
        w.ground_hash = sim::ground_hash(heights, surfaces);
        w.zone_hash = sim::zone_hash(heights, surfaces, *w.zone);
        JPH::PhysicsMaterialList materials;
        for (int s = 0; s < kSurfaceCount; ++s) {
            materials.push_back(new SurfaceMaterial(static_cast<Surface>(s)));
        }
        JPH::HeightFieldShapeSettings ground_settings(
            heights.data(),
            JPH::Vec3(static_cast<float>(zone::kGroundOrigin), 0.0f,
                      static_cast<float>(zone::kGroundOrigin)),
            JPH::Vec3(static_cast<float>(zone::kGroundSpacing), 1.0f,
                      static_cast<float>(zone::kGroundSpacing)),
            zone::kGroundSamples, surfaces.data(), materials);
        ground_settings.mBlockSize = kGroundBlockSize;
        ground_settings.mBitsPerSample = kGroundBitsPerSample;
        const JPH::ShapeSettings::ShapeResult shape = ground_settings.Create();
        RALLY_CHECK(shape.IsValid(), "SimWorld::build: Jolt no pudo crear el suelo de la zona");
        w.ground_bytes = shape.Get()->GetStats().mSizeBytes;
        const JPH::BodyCreationSettings settings(shape.Get(), JPH::RVec3::sZero(),
                                                 JPH::Quat::sIdentity(), JPH::EMotionType::Static,
                                                 layers::kStatic);
        const JPH::Body* body = bodies.CreateBody(settings);
        RALLY_CHECK(body != nullptr, "SimWorld::build: Jolt no tiene cuerpos libres");
        w.ground = body->GetID();
        batch.push_back(w.ground);
        // Muros perimetrales, después del suelo y antes de la descripción (orden fijo).
        for (const WallBox& wall : w.zone->walls()) {
            const JPH::BodyCreationSettings wall_settings(
                new JPH::BoxShape(JPH::Vec3(static_cast<float>(wall.half_x),
                                            static_cast<float>(wall.half_y),
                                            static_cast<float>(wall.half_z))),
                JPH::RVec3(wall.cx, wall.cy, wall.cz), JPH::Quat::sIdentity(),
                JPH::EMotionType::Static, layers::kStatic);
            const JPH::Body* wall_body = bodies.CreateBody(wall_settings);
            RALLY_CHECK(wall_body != nullptr, "SimWorld::build: Jolt no tiene cuerpos libres");
            batch.push_back(wall_body->GetID());
        }
    }
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
            w.controlled_half_y = box.half_y;
        }
    }

    // AddBodiesPrepare reordena el array por capas: se le pasa una copia para conservar el
    // orden de la descripción en `bodies`.
    batch.insert(batch.end(), w.bodies.begin(), w.bodies.end());
    if (!batch.empty()) {
        const int batch_count = static_cast<int>(batch.size());
        const JPH::BodyInterface::AddState add_state =
            bodies.AddBodiesPrepare(batch.data(), batch_count);
        bodies.AddBodiesFinalize(batch.data(), batch_count, add_state, JPH::EActivation::Activate);
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
    // Posición antes del paso, para el cronómetro.
    double before_x = 0.0;
    double before_z = 0.0;
    // Velocidad antes del paso, para la aceleración de la telemetría.
    JPH::Vec3 before_velocity = JPH::Vec3::sZero();
    if (!w.controlled.IsInvalid()) {
        JPH::BodyInterface& bodies = w.system.GetBodyInterfaceNoLock();
        const JPH::RVec3 before = bodies.GetPosition(w.controlled);
        before_x = before.GetX();
        before_z = before.GetZ();
        if (w.telemetry_enabled) {
            before_velocity = bodies.GetLinearVelocity(w.controlled);
        }
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
            JPH::RVec3 target(w.spawn[0], w.spawn[1], w.spawn[2]);
            JPH::Quat rotation = JPH::Quat::sIdentity();
            if (w.zone) {
                const JPH::RVec3 at = bodies.GetPosition(w.controlled);
                const RespawnPoint& r = w.zone->respawns()[static_cast<std::size_t>(
                    w.zone->nearest_respawn(at.GetX(), at.GetZ()))];
                target =
                    JPH::RVec3(r.x, w.zone->ground_height(r.x, r.z) + w.controlled_half_y, r.z);
                rotation = JPH::Quat::sRotation(JPH::Vec3::sAxisY(), static_cast<float>(r.yaw));
            }
            bodies.SetPositionAndRotation(w.controlled, target, rotation,
                                          JPH::EActivation::Activate);
            bodies.SetLinearAndAngularVelocity(w.controlled, JPH::Vec3::sZero(),
                                               JPH::Vec3::sZero());
            w.last_impact = 0.0f;
        } else {
            const JPH::Vec3 velocity = bodies.GetLinearVelocity(w.controlled);
            w.last_impact = (velocity - expected_velocity).Length();
        }
        if (w.lap) {
            const JPH::RVec3 after = bodies.GetPosition(w.controlled);
            w.lap->update(before_x, before_z, after.GetX(), after.GetZ(), reset_pressed);
        }
    }
    ++w.steps;
    if (w.telemetry_enabled) {
        w.capture_telemetry(input, before_velocity, reset_pressed);
    }
}

void SimWorld::Impl::capture_telemetry(const SimInput& input, JPH::Vec3Arg before_velocity,
                                       bool reset) {
    Impl& w = *this;
    CarTelemetry& t = w.telemetry;
    t = CarTelemetry{};
    t.step = w.steps;
    t.target = input;
    if (w.controlled.IsInvalid()) {
        return;
    }
    const JPH::BodyInterface& body_interface = w.system.GetBodyInterfaceNoLock();
    const JPH::RVec3 com = body_interface.GetCenterOfMassPosition(w.controlled);
    const JPH::Quat rotation = body_interface.GetRotation(w.controlled);
    JPH::Vec3 v;
    JPH::Vec3 omega;
    body_interface.GetLinearAndAngularVelocity(w.controlled, v, omega);
    const JPH::Vec3 forward = rotation * JPH::Vec3(0.0f, 0.0f, -1.0f);
    const JPH::Vec3 right = rotation * JPH::Vec3(1.0f, 0.0f, 0.0f);
    const JPH::Vec3 up = rotation * JPH::Vec3(0.0f, 1.0f, 0.0f);

    t.x = com.GetX();
    t.y = com.GetY();
    t.z = com.GetZ();
    t.vx = v.GetX();
    t.vy = v.GetY();
    t.vz = v.GetZ();
    t.v_forward = v.Dot(forward);
    t.v_right = v.Dot(right);
    t.v_up = v.Dot(up);
    t.speed = v.Length();
    t.heading = rmath::atan2(forward.GetX(), -forward.GetZ());
    t.yaw_rate = -omega.Dot(up);
    t.beta = rmath::atan2(t.v_right, t.v_forward);
    // Recolocar es un traslado, no una aceleración.
    const JPH::Vec3 a = reset ? JPH::Vec3::sZero() : (v - before_velocity) / kStepSeconds;
    t.ax = a.GetX();
    t.ay = a.GetY();
    t.az = a.GetZ();
    t.a_forward = a.Dot(forward);
    t.a_right = a.Dot(right);

    constexpr float kAxisScale = 1.0f / kAxisMax;
    const ControlState& c = w.controls;
    t.steer = static_cast<float>(c.steer.applied) * kAxisScale;
    t.throttle = static_cast<float>(c.throttle.applied) * kAxisScale;
    t.brake = static_cast<float>(c.brake.applied) * kAxisScale;
    t.handbrake = static_cast<float>(c.handbrake) * kAxisScale;
    t.pressed = c.pressed;
    if (w.zone) {
        t.surface = static_cast<std::int8_t>(w.zone->surface_at(t.x, t.z));
    }
    if (w.lap) {
        const LapState& lap_state = w.lap->state();
        t.laps = lap_state.laps;
        t.lap_steps = lap_state.current();
    }
    t.impact = w.last_impact;
}

void SimWorld::set_telemetry(bool enabled) { impl_->telemetry_enabled = enabled; }

bool SimWorld::telemetry_enabled() const { return impl_->telemetry_enabled; }

const CarTelemetry& SimWorld::telemetry() const { return impl_->telemetry; }

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

const TestZone* SimWorld::zone() const { return impl_->zone.get(); }

const LapState* SimWorld::lap() const { return impl_->lap ? &impl_->lap->state() : nullptr; }

std::uint64_t SimWorld::ground_hash() const { return impl_->ground_hash; }

std::uint64_t SimWorld::zone_hash() const { return impl_->zone_hash; }

std::size_t SimWorld::ground_bytes() const { return impl_->ground_bytes; }

GroundProbe SimWorld::probe_ground(double x, double z) const {
    GroundProbe probe;
    if (impl_->ground.IsInvalid()) {
        return probe;
    }
    constexpr double kTop = 1000.0;
    const JPH::TransformedShape shape =
        impl_->system.GetBodyInterfaceNoLock().GetTransformedShape(impl_->ground);
    const JPH::RRayCast ray(JPH::RVec3(x, kTop, z), JPH::Vec3(0.0f, -2.0f * kTop, 0.0f));
    JPH::RayCastResult hit;
    if (!shape.CastRay(ray, hit)) {
        return probe;
    }
    probe.hit = true;
    probe.y = ray.GetPointOnRay(hit.mFraction).GetY();
    const auto* material = static_cast<const SurfaceMaterial*>(shape.GetMaterial(hit.mSubShapeID2));
    probe.surface = material->surface();
    return probe;
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
    if (impl_->lap) {
        const LapState& lap = impl_->lap->state();
        const auto add_i64 = [&hash](std::int64_t value) {
            const auto bits = static_cast<std::uint64_t>(value);
            hash.add_u32(static_cast<std::uint32_t>(bits));
            hash.add_u32(static_cast<std::uint32_t>(bits >> 32));
        };
        add_i64(lap.steps);
        add_i64(lap.lap_start);
        hash.add_u32(static_cast<std::uint32_t>(lap.debt));
        hash.add_u32(static_cast<std::uint32_t>(lap.laps));
        add_i64(lap.last_lap);
        add_i64(lap.best_lap);
    }
    return hash.value();
}

} // namespace rally::sim
