#include "sim/car_controller.h"

#include "sim/check.h"
#include "sim/layers.h"
#include "sim/rmath.h"
#include "sim/steer_assist.h"
#include "sim/test_zone.h"

#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Body/BodyFilter.h>
#include <Jolt/Physics/Body/BodyLock.h>
#include <Jolt/Physics/Collision/CastResult.h>
#include <Jolt/Physics/Collision/CollisionCollector.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/CylinderShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
#include <Jolt/Physics/Collision/ShapeCast.h>
#include <Jolt/Physics/PhysicsSystem.h>
#include <Jolt/Physics/StateRecorder.h>
#include <Jolt/Physics/Vehicle/VehicleAntiRollBar.h>
#include <Jolt/Physics/Vehicle/Wheel.h>

namespace rally::sim {

namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;

// Ejes del coche en su espacio local.
const JPH::Vec3 kCarUp(0.0f, 1.0f, 0.0f);
const JPH::Vec3 kCarForward(0.0f, 0.0f, -1.0f);

float sign_of(float x) { return x < 0.0f ? -1.0f : 1.0f; }

// Rueda de Jolt con todo su estado inicializado. Wheel deja sin valor el contacto (posición,
// normal, ejes, velocidad) y la longitud de la suspensión, y VehicleConstraint::SaveState los
// guarda: con una rueda que aún no ha tocado el suelo, el estado (y la huella) llevaría lo que
// hubiera en la memoria.
class CarWheel final : public JPH::Wheel {
public:
    explicit CarWheel(const JPH::WheelSettings& settings) : JPH::Wheel(settings) {
        mSuspensionLength = settings.mSuspensionMaxLength;
        mContactPosition = JPH::RVec3::sZero();
        mContactPointVelocity = JPH::Vec3::sZero();
        mContactNormal = JPH::Vec3::sZero();
        mContactLongitudinal = JPH::Vec3::sZero();
        mContactLateral = JPH::Vec3::sZero();
        mAxlePlaneConstant = 0.0;
    }
};

float magnitude(float x, float y) { return rmath::sqrt(x * x + y * y); }

// Giro mínimo de un cuerpo en un paso que Jolt 5.6.0 aplica (Body.inl:93,
// Body::AddRotationStep), rad.
constexpr float kJoltMinRotationStep = 1.0e-6f;

} // namespace

double ReliefSource::height(MicroRelief relief, Surface surface, double x, double z) const {
    switch (relief) {
    case MicroRelief::None:
        return 0.0;
    case MicroRelief::Zone:
        return zone != nullptr ? zone->micro_height(x, z) : 0.0;
    case MicroRelief::Noise:
        return noise != nullptr ? noise->surface_noise(surface, x, z) : 0.0;
    }
    return 0.0;
}

JPH::VehicleController*
CarControllerSettings::ConstructController(JPH::VehicleConstraint& constraint) const {
    return new CarController(constraint, *this);
}

CarController::CarController(JPH::VehicleConstraint& constraint,
                             const CarControllerSettings& settings)
    : JPH::VehicleController(constraint), car_(settings.car),
      explicit_wheels_(settings.explicit_wheels) {
    contact_surface_.fill(Surface::Dirt);
    radius_.fill(car_.wheel_radius);
}

JPH::Ref<JPH::VehicleControllerSettings> CarController::GetSettings() const {
    auto* settings = new CarControllerSettings;
    settings->car = car_;
    settings->explicit_wheels = explicit_wheels_;
    return settings;
}

JPH::Wheel* CarController::ConstructWheel(const JPH::WheelSettings& wheel) const {
    return new CarWheel(wheel);
}

void CarController::reset() {
    // Estado dinámico a cero; la condición (radio_) no cambia.
    gear_ = Gear::Forward;
    u_.fill(0.0f);
    w_.fill(0.0f);
    steer_.fill(0.0f);
    outputs_.fill(WheelOutput{});
    contact_h_.fill(0.0f);
    applied_force_ = JPH::Vec3::sZero();
    for (int i = 0; i < kWheelCount; ++i) {
        JPH::Wheel* w = mConstraint.GetWheel(static_cast<JPH::uint>(i));
        w->SetAngularVelocity(0.0f);
        w->SetRotationAngle(0.0f);
        w->SetSteerAngle(0.0f);
    }
}

void CarController::set_rolling(float speed) {
    for (int i = 0; i < kWheelCount; ++i) {
        mConstraint.GetWheel(static_cast<JPH::uint>(i))
            ->SetAngularVelocity(speed / radius_[static_cast<std::size_t>(i)]);
    }
}

void CarController::set_contact_relief(int index, float h, Surface surface) {
    contact_h_[static_cast<std::size_t>(index)] = h;
    contact_surface_[static_cast<std::size_t>(index)] = surface;
}

SteerAssistInput CarController::steer_assist_input() const {
    const JPH::Body* body = mConstraint.GetVehicleBody();
    const JPH::RMat44 transform = body->GetWorldTransform();
    const JPH::Vec3 right_axis = transform.GetAxisX();
    const JPH::Vec3 forward_axis = transform.Multiply3x3(kCarForward);
    const auto attachment = [&](int i) {
        return transform *
               mConstraint.GetWheel(static_cast<JPH::uint>(i))->GetSettings()->mPosition;
    };
    // Ángulo de la velocidad del cuerpo en un punto, en ejes del coche (+ derecha).
    const auto vel_angle = [&](JPH::RVec3Arg point) {
        const JPH::Vec3 v = body->GetPointVelocity(point);
        return rmath::atan2(v.Dot(right_axis), v.Dot(forward_axis));
    };
    // Con carga: contacto y empuje de la suspensión en el paso anterior (los dos en el estado de
    // Jolt, como la superficie en el nuestro).
    const auto loaded = [&](int i) {
        const JPH::Wheel* w = mConstraint.GetWheel(static_cast<JPH::uint>(i));
        return w->HasContact() && w->GetSuspensionLambda() > 0.0f;
    };
    const auto wheel_angle = [&](int i) {
        const JPH::Wheel* w = mConstraint.GetWheel(static_cast<JPH::uint>(i));
        return vel_angle(w->GetContactPosition());
    };
    const auto tan_peak = [&](int i) {
        return car_.tire(contact_surface_[static_cast<std::size_t>(i)]).tan_alpha_peak;
    };

    SteerAssistInput in;
    const JPH::RVec3 front_axle = 0.5 * (attachment(kFrontLeft) + attachment(kFrontRight));
    const JPH::Vec3 front_velocity = body->GetPointVelocity(front_axle);
    in.speed = magnitude(front_velocity.GetX(), front_velocity.GetZ());
    in.forward_speed = front_velocity.Dot(forward_axis);
    in.reverse = gear_ == Gear::Reverse;
    // Giro a la derecha: decide la exterior (FL); sin carga, la interior (FR). Y al revés.
    const std::array<std::array<int, 2>, 2> wheels = {
        {{kFrontLeft, kFrontRight}, {kFrontRight, kFrontLeft}}};
    for (std::size_t side = 0; side < 2; ++side) {
        for (std::size_t k = 0; k < 2; ++k) {
            const int i = wheels[side][k];
            if (loaded(i)) {
                in.side[side] = SteerSide{true, k == 0, wheel_angle(i), tan_peak(i)};
                break;
            }
        }
    }
    for (int i : {kRearLeft, kRearRight}) {
        if (loaded(i)) {
            in.rear_loaded = true;
            in.rear_tan_alpha_peak = rmath::max(in.rear_tan_alpha_peak, tan_peak(i));
        }
    }
    in.rear_vel_angle = vel_angle(0.5 * (attachment(kRearLeft) + attachment(kRearRight)));
    return in;
}

void CarController::PreCollide(float, JPH::PhysicsSystem&) {
    // Selector (D4): bajar parado pasa de adelante a N y de N a R; subir, al revés.
    const JPH::Body* body = mConstraint.GetVehicleBody();
    const bool stopped = body->GetLinearVelocity().Length() < kGearStopSpeed;
    if (controls_.shift_down && stopped && gear_ != Gear::Reverse) {
        gear_ = static_cast<Gear>(static_cast<int>(gear_) - 1);
    }
    if (controls_.shift_up && gear_ != Gear::Forward) {
        gear_ = static_cast<Gear>(static_cast<int>(gear_) + 1);
    }

    // Dirección: la rueda interior gira max_steer con la dirección a fondo; la exterior, la
    // mezcla de ese ángulo con el de Ackermann puro (sim/steer_assist.h). En Jolt el ángulo es
    // positivo a la izquierda; steer > 0 es a la derecha.
    const float inner = wheel_angle_for_axis(car_, rmath::abs(controls_.steer), false);
    const float outer = wheel_angle_for_axis(car_, rmath::abs(controls_.steer), true);
    const bool right = controls_.steer > 0.0f;
    const float left_angle = right ? outer : inner;
    const float right_angle = right ? inner : outer;
    const float jolt_sign = right ? -1.0f : 1.0f;
    steer_[kFrontLeft] = jolt_sign * left_angle;
    steer_[kFrontRight] = jolt_sign * right_angle;
    steer_[kRearLeft] = 0.0f;
    steer_[kRearRight] = 0.0f;
    for (int i = 0; i < kWheelCount; ++i) {
        mConstraint.GetWheel(static_cast<JPH::uint>(i))
            ->SetSteerAngle(steer_[static_cast<std::size_t>(i)]);
        contact_h_[static_cast<std::size_t>(i)] = 0.0f;
    }
}

void CarController::PostCollide(float dt, JPH::PhysicsSystem&) {
    JPH::Body* body = mConstraint.GetVehicleBody();
    const float h = dt / static_cast<float>(kWheelSubsteps);

    // Tracción provisional: par total = acelerador × min(par máximo, potencia / ω medio),
    // repartido por ejes y a partes iguales entre izquierda y derecha (abierto). Con el freno
    // de mano tirado, el central y el embrague se desacoplan: sin par (D3).
    float omega_mean = 0.0f;
    for (int i = 0; i < kWheelCount; ++i) {
        omega_mean +=
            rmath::abs(mConstraint.GetWheel(static_cast<JPH::uint>(i))->GetAngularVelocity());
    }
    omega_mean *= 1.0f / kWheelCount;
    float drive =
        controls_.throttle *
        rmath::min(car_.drive_torque, car_.drive_power / rmath::max(omega_mean, 1.0e-3f)) *
        static_cast<float>(static_cast<int>(gear_));
    if (controls_.handbrake > 0.0f) {
        drive = 0.0f;
    }

    // Jolt no gira un cuerpo si su giro en el paso es pequeño: PhysicsSystem.cpp:1641 llama a
    // body.AddRotationStep(body.GetAngularVelocity() * delta_time), y Body.inl:92-93 solo gira
    // si (ω·dt).Length() > 1e-6 (Jolt 5.6.0; con un paso de colisión, delta_time es el dt del
    // paso). Por debajo (~0,24 mrad/s a 240 Hz) el giro no ocurre, pero su traslación sí: con
    // v + ω×r el contacto daba cero mientras el cuerpo se trasladaba (el coche parado se
    // deslizaba ~4 µm/s sobre la huella sin que el neumático lo viera). Entonces el neumático
    // usa la velocidad que Jolt integra de verdad: la lineal. La misma expresión que Jolt, con
    // la velocidad angular de antes del paso (la de después aún no se conoce).
    const bool rotation_skipped =
        !((body->GetAngularVelocity() * dt).Length() > kJoltMinRotationStep);
    rotation_skipped_ = rotation_skipped;

    applied_force_ = JPH::Vec3::sZero();
    for (int i = 0; i < kWheelCount; ++i) {
        const auto index = static_cast<std::size_t>(i);
        JPH::Wheel* w = mConstraint.GetWheel(static_cast<JPH::uint>(i));
        const bool front = i == kFrontLeft || i == kFrontRight;
        WheelOutput& out = outputs_[index];
        out = WheelOutput{};
        const float r = radius_[index];

        TireInput in;
        in.radius = r;
        in.drive_torque =
            drive * 0.5f * (front ? car_.drive_front_share : 1.0f - car_.drive_front_share);
        float brake = controls_.brake * car_.brake_torque * 0.5f *
                      (front ? car_.brake_front_share : 1.0f - car_.brake_front_share);
        if (!front) {
            brake += controls_.handbrake * car_.handbrake_torque;
        }
        out.brake_torque = brake;

        JPH::RVec3 point = JPH::RVec3::sZero();
        JPH::Vec3 longitudinal = JPH::Vec3::sZero();
        JPH::Vec3 lateral = JPH::Vec3::sZero();
        if (w->HasContact()) {
            // Carga: la impulsión normal de la suspensión (con la barra y el tope) del paso
            // anterior.
            in.load = rmath::max(0.0f, w->GetSuspensionLambda() / dt);
            const Surface surface = contact_surface_[index];
            in.surface = surface;
            point = w->GetContactPosition();
            longitudinal = w->GetContactLongitudinal();
            lateral = w->GetContactLateral();
            const JPH::Vec3 body_velocity =
                rotation_skipped ? body->GetLinearVelocity() : body->GetPointVelocity(point);
            const JPH::Vec3 velocity = body_velocity - w->GetContactPointVelocity();
            in.vx = velocity.Dot(longitudinal);
            in.vy = velocity.Dot(lateral);
            out.contact = true;
            out.surface = surface;
            out.load = in.load;
            out.h = contact_h_[index];
            out.compression = w->GetSettings()->mSuspensionMaxLength - w->GetSuspensionLength();
        }
        // Freno, freno de mano y rodadura: pares de Coulomb sobre la rueda.
        const TireSurface& tire = car_.tire(in.surface);
        in.stop_torque = brake + tire.rolling_resistance * in.load * r;

        // Giro y huella en subpasos con la velocidad del cuerpo fija; al cuerpo, la media.
        WheelDynamics state{w->GetAngularVelocity(), u_[index], w_[index]};
        float sum_fx = 0.0f;
        float sum_fy = 0.0f;
        float sum_fx_pure = 0.0f;
        float sum_fy_pure = 0.0f;
        for (int s = 0; s < kWheelSubsteps; ++s) {
            const TireForce f = step_tire(car_, in, h, !explicit_wheels_, state);
            sum_fx += f.fx;
            sum_fy += f.fy;
            sum_fx_pure += f.fx_pure;
            sum_fy_pure += f.fy_pure;
        }
        w->SetAngularVelocity(state.omega);
        u_[index] = state.u;
        w_[index] = state.w;
        const float fx = sum_fx / kWheelSubsteps;
        const float fy = sum_fy / kWheelSubsteps;

        out.omega = state.omega;
        out.locked = state.omega == 0.0f;
        out.u = state.u;
        out.w = state.w;
        out.kappa = state.u / tire.relaxation_long;
        out.alpha = rmath::atan(state.w / tire.relaxation_lat);
        out.slip_x = state.omega * r - in.vx;
        out.slip_y = -in.vy;
        out.contact_speed = magnitude(in.vx, in.vy);
        out.fx = fx;
        out.fy = fy;
        out.fx_pure = sum_fx_pure / kWheelSubsteps;
        out.fy_pure = sum_fy_pure / kWheelSubsteps;
        out.mu = out.contact ? peak_mu(tire, in.load, car_.nominal_load) : 0.0f;
        if (out.contact) {
            const JPH::Vec3 force = longitudinal * fx + lateral * fy;
            body->AddForce(force, point);
            applied_force_ += force;
        }
    }

    // Resistencia aerodinámica en el centro de masas.
    const JPH::Vec3 v = body->GetLinearVelocity();
    const JPH::Vec3 drag = -0.5f * kAirDensity * car_.cda * v.Length() * v;
    body->AddForce(drag);
    applied_force_ += drag;

    // Ángulo de giro de cada rueda (dibujo), en [0, 2π).
    for (int i = 0; i < kWheelCount; ++i) {
        JPH::Wheel* w = mConstraint.GetWheel(static_cast<JPH::uint>(i));
        float angle = rmath::fmod(w->GetRotationAngle() + w->GetAngularVelocity() * dt, kTwoPi);
        if (angle < 0.0f) {
            angle += kTwoPi;
        }
        w->SetRotationAngle(angle);
    }
}

void CarController::SaveState(JPH::StateRecorder& stream) const {
    // Dinámico.
    stream.Write(static_cast<std::int8_t>(gear_));
    for (std::size_t i = 0; i < kWheelCount; ++i) {
        stream.Write(u_[i]);
        stream.Write(w_[i]);
        stream.Write(steer_[i]);
    }
    // Superficie del último contacto: la lee la ayuda de dirección al principio del paso
    // siguiente.
    for (std::size_t i = 0; i < kWheelCount; ++i) {
        stream.Write(static_cast<std::uint8_t>(contact_surface_[i]));
    }
    // Condición.
    for (std::size_t i = 0; i < kWheelCount; ++i) {
        stream.Write(radius_[i]);
    }
}

void CarController::RestoreState(JPH::StateRecorder& stream) {
    std::int8_t gear = 0;
    stream.Read(gear);
    gear_ = static_cast<Gear>(gear);
    for (std::size_t i = 0; i < kWheelCount; ++i) {
        stream.Read(u_[i]);
        stream.Read(w_[i]);
        stream.Read(steer_[i]);
        mConstraint.GetWheel(static_cast<JPH::uint>(i))->SetSteerAngle(steer_[i]);
    }
    for (std::size_t i = 0; i < kWheelCount; ++i) {
        std::uint8_t surface = 0;
        stream.Read(surface);
        contact_surface_[i] = static_cast<Surface>(surface);
    }
    for (std::size_t i = 0; i < kWheelCount; ++i) {
        stream.Read(radius_[i]);
    }
}

CarWheelTester::CarWheelTester(JPH::ObjectLayer layer, const ReliefSource& relief,
                               CarController& controller)
    : layer_(layer), relief_(relief), controller_(controller) {}

namespace {

// El contacto más cercano del cilindro (como en VehicleCollisionTesterCastCylinder de Jolt).
class ClosestHit final : public JPH::CastShapeCollector {
public:
    ClosestHit(JPH::PhysicsSystem& system, const JPH::RShapeCast& cast)
        : system_(system), cast_(cast) {}

    void AddHit(const JPH::ShapeCastResult& result) override {
        const float early_out = result.GetEarlyOutFraction();
        if (early_out >= GetEarlyOutFraction()) {
            return;
        }
        const JPH::BodyLockRead lock(system_.GetBodyLockInterfaceNoLock(), result.mBodyID2);
        const JPH::Body* hit = &lock.GetBody();
        if (hit->IsSensor()) {
            return;
        }
        UpdateEarlyOutFraction(early_out);
        body = hit;
        sub_shape = result.mSubShapeID2;
        normal = -result.mPenetrationAxis.Normalized();
        fraction = result.mFraction;
    }

    const JPH::Body* body = nullptr;
    JPH::SubShapeID sub_shape;
    JPH::Vec3 normal = JPH::Vec3::sZero();
    float fraction = 0.0f;

private:
    JPH::PhysicsSystem& system_;
    const JPH::RShapeCast& cast_;
};

} // namespace

bool CarWheelTester::Collide(JPH::PhysicsSystem& system, const JPH::VehicleConstraint& constraint,
                             JPH::uint wheel_index, JPH::RVec3Arg origin, JPH::Vec3Arg direction,
                             const JPH::BodyID& vehicle_body, JPH::Body*& out_body,
                             JPH::SubShapeID& out_sub_shape, JPH::RVec3& out_position,
                             JPH::Vec3& out_normal, float& out_suspension_length) const {
    const JPH::DefaultBroadPhaseLayerFilter broad_phase_filter =
        system.GetDefaultBroadPhaseLayerFilter(layer_);
    const JPH::DefaultObjectLayerFilter object_filter = system.GetDefaultLayerFilter(layer_);
    const JPH::IgnoreSingleBodyFilter body_filter(vehicle_body);
    const JPH::WheelSettings* settings = constraint.GetWheel(wheel_index)->GetSettings();
    const float max_length = settings->mSuspensionMaxLength;
    // Radio de la condición de la rueda, no el de los WheelSettings de Jolt.
    const float radius = controller_.wheel_radius(static_cast<int>(wheel_index));
    const float half_width = 0.5f * settings->mWidth;

    // Cilindro con el eje a lo ancho de la rueda (gira alrededor de su y local).
    JPH::RMat44 start =
        constraint.GetWheelWorldTransform(wheel_index, JPH::Vec3::sAxisY(), JPH::Vec3::sAxisX());
    start.SetTranslation(origin);
    JPH::CylinderShape cylinder(half_width, radius, rmath::min(half_width, radius) * 0.1f);
    cylinder.SetEmbedded();
    const JPH::RShapeCast cast(&cylinder, JPH::Vec3::sOne(), start, direction * max_length);
    JPH::ShapeCastSettings cast_settings;
    cast_settings.mUseShrunkenShapeAndConvexRadius = true;
    cast_settings.mReturnDeepestPoint = true;
    ClosestHit hit(system, cast);
    system.GetNarrowPhaseQueryNoLock().CastShape(cast, cast_settings,
                                                 cast.mCenterOfMassStart.GetTranslation(), hit,
                                                 broad_phase_filter, object_filter, body_filter);
    if (hit.body == nullptr) {
        return false;
    }
    out_body = const_cast<JPH::Body*>(hit.body);
    out_sub_shape = hit.sub_shape;
    out_normal = hit.normal;
    out_suspension_length = max_length * hit.fraction;

    const auto* material =
        static_cast<const SurfaceMaterial*>(out_body->GetShape()->GetMaterial(out_sub_shape));
    const Surface surface = material->surface();
    // El cilindro da el punto más profundo del contacto, que en suelo llano salta de un borde
    // al otro del ancho de la rueda: el brazo de la suspensión y la muestra de h saltarían con
    // él. El contacto se pone bajo el centro de la rueda, en la normal.
    const JPH::RVec3 center = origin + JPH::RVec3(direction * out_suspension_length);
    double h = 0.0;
    if (material->relief() != MicroRelief::None) {
        // Huella: largo en la dirección de la rueda, ancho a su derecha, en el plano.
        const JPH::Wheel* wheel = constraint.GetWheel(wheel_index);
        const JPH::Quat rotation = constraint.GetVehicleBody()->GetRotation() *
                                   JPH::Quat::sRotation(kCarUp, wheel->GetSteerAngle());
        const JPH::Vec3 forward = rotation * kCarForward;
        const JPH::Vec3 right = rotation * JPH::Vec3(1.0f, 0.0f, 0.0f);
        const float half_length = 0.5f * controller_.car().footprint_length;
        double sum = 0.0;
        for (int a = -1; a <= 1; ++a) {
            for (int b = -1; b <= 1; ++b) {
                const JPH::Vec3 offset = forward * (static_cast<float>(a) * half_length) +
                                         right * (static_cast<float>(b) * half_width);
                sum += relief_.height(material->relief(), surface, center.GetX() + offset.GetX(),
                                      center.GetZ() + offset.GetZ());
            }
        }
        h = sum / 9.0;
    }
    const auto lift = static_cast<float>(h);
    out_suspension_length = rmath::max(0.0f, out_suspension_length - lift);
    out_position = origin + JPH::RVec3(direction * out_suspension_length - out_normal * radius);
    controller_.set_contact_relief(static_cast<int>(wheel_index), lift, surface);
    return true;
}

void CarWheelTester::PredictContactProperties(JPH::PhysicsSystem&, const JPH::VehicleConstraint&,
                                              JPH::uint, JPH::RVec3Arg, JPH::Vec3Arg,
                                              const JPH::BodyID&, JPH::Body*&, JPH::SubShapeID&,
                                              JPH::RVec3&, JPH::Vec3&, float&) const {
    // No se usa: las ruedas se prueban en cada paso (SetNumStepsBetweenCollisionTest* = 1).
    RALLY_CHECK(false, "CarWheelTester::PredictContactProperties: no se esperaba la llamada");
}

JPH::BodyCreationSettings car_body_settings(const CarData& car, JPH::RVec3Arg position,
                                            JPH::QuatArg rotation, float speed) {
    // Carrocería: caja apoyada a ground_clearance del suelo y centrada entre los ejes. Centro
    // de masas a cg_height, a (1 - front_weight) de la batalla por detrás del eje delantero
    // (el eje delantero está en z = -batalla/2).
    const JPH::Vec3 box_center(0.0f, car.ground_clearance + 0.5f * car.box_height, 0.0f);
    const JPH::Vec3 center_of_mass(
        0.0f, car.cg_height, -0.5f * car.wheelbase + (1.0f - car.front_weight) * car.wheelbase);
    const JPH::Ref<JPH::BoxShape> box = new JPH::BoxShape(
        JPH::Vec3(0.5f * car.box_width, 0.5f * car.box_height, 0.5f * car.box_length),
        JPH::cDefaultConvexRadius, new SurfaceMaterial(Surface::Asphalt, MicroRelief::None));
    // Ajustes con Ref: la forma exterior guarda la interior con un Ref y la borra.
    const JPH::Ref<JPH::RotatedTranslatedShapeSettings> placed =
        new JPH::RotatedTranslatedShapeSettings(box_center, JPH::Quat::sIdentity(), box);
    const JPH::Ref<JPH::OffsetCenterOfMassShapeSettings> shifted =
        new JPH::OffsetCenterOfMassShapeSettings(center_of_mass - box_center, placed);
    const JPH::ShapeSettings::ShapeResult shape = shifted->Create();
    RALLY_CHECK(shape.IsValid(), "car_body_settings: Jolt no pudo crear la forma del coche");

    JPH::BodyCreationSettings settings(shape.Get(), position, rotation, JPH::EMotionType::Dynamic,
                                       layers::kMoving);
    settings.mOverrideMassProperties = JPH::EOverrideMassProperties::MassAndInertiaProvided;
    settings.mMassPropertiesOverride.mMass = car.mass;
    settings.mMassPropertiesOverride.mInertia =
        JPH::Mat44::sScale(JPH::Vec3(car.inertia_pitch, car.inertia_yaw, car.inertia_roll));
    settings.mAllowSleeping = false;
    settings.mLinearDamping = 0.0f;
    settings.mAngularDamping = 0.0f;
    settings.mLinearVelocity = rotation * (kCarForward * speed);
    return settings;
}

JPH::Ref<JPH::VehicleConstraint> make_car_constraint(JPH::Body& body, const CarData& car,
                                                     float gravity, bool explicit_wheels) {
    const float max_length = kMinSuspensionLength + car.travel;

    JPH::VehicleConstraintSettings settings;
    settings.mUp = kCarUp;
    settings.mForward = kCarForward;
    settings.mMaxPitchRollAngle = kPi; // sin límite
    for (int i = 0; i < kWheelCount; ++i) {
        const bool front = i == kFrontLeft || i == kFrontRight;
        const bool left = i == kFrontLeft || i == kRearLeft;
        const float frequency = front ? car.frequency_front : car.frequency_rear;
        const float axle_share = front ? car.front_weight : 1.0f - car.front_weight;
        const float static_load = 0.5f * axle_share * car.mass * gravity;

        JPH::Ref<JPH::WheelSettings> wheel = new JPH::WheelSettings;
        wheel->mWheelForward = kCarForward;
        wheel->mWheelUp = kCarUp;
        wheel->mSteeringAxis = kCarUp;
        wheel->mSuspensionDirection = JPH::Vec3(0.0f, -1.0f, 0.0f);
        wheel->mSuspensionMinLength = kMinSuspensionLength;
        wheel->mSuspensionMaxLength = max_length;
        wheel->mSuspensionPreloadLength = 0.0f;
        wheel->mRadius = car.wheel_radius;
        wheel->mWidth = car.wheel_width;
        const float x = (left ? -0.5f : 0.5f) * car.track;
        const float z = (front ? -0.5f : 0.5f) * car.wheelbase;

        // Muelle y amortiguador con la masa suspendida de la esquina (carga estática / g), para
        // que la frecuencia del archivo sea la del coche: k = m·(2πf)², c = 2·ζ·√(k·m). Jolt los
        // usa tal cual (StiffnessAndDamping). No hay masa no suspendida aparte.
        const float corner_mass = static_load / gravity;
        const float omega = kTwoPi * frequency;
        const float stiffness = corner_mass * omega * omega;
        const float damping = 2.0f * car.damping_ratio * rmath::sqrt(stiffness * corner_mass);
        wheel->mSuspensionSpring.mMode = JPH::ESpringMode::StiffnessAndDamping;
        wheel->mSuspensionSpring.mStiffness = stiffness;
        wheel->mSuspensionSpring.mDamping = damping;
        const float sag = static_load / stiffness; // = g / (2πf)²
        RALLY_CHECK(sag < car.travel, "make_car_constraint: la compresión de equilibrio no cabe "
                                      "en el recorrido de la suspensión");
        // En equilibrio la suspensión mide max_length - sag y el centro de la rueda está a su
        // radio del suelo (el origen del coche).
        wheel->mPosition = JPH::Vec3(x, car.wheel_radius + max_length - sag, z);
        settings.mWheels.push_back(wheel);
    }
    settings.mAntiRollBars.resize(2);
    settings.mAntiRollBars[0].mLeftWheel = kFrontLeft;
    settings.mAntiRollBars[0].mRightWheel = kFrontRight;
    settings.mAntiRollBars[0].mStiffness = car.antiroll_front;
    settings.mAntiRollBars[1].mLeftWheel = kRearLeft;
    settings.mAntiRollBars[1].mRightWheel = kRearRight;
    settings.mAntiRollBars[1].mStiffness = car.antiroll_rear;

    auto* controller = new CarControllerSettings;
    controller->car = car;
    controller->explicit_wheels = explicit_wheels;
    settings.mController = controller;

    JPH::Ref<JPH::VehicleConstraint> constraint = new JPH::VehicleConstraint(body, settings);
    constraint->SetNumStepsBetweenCollisionTestActive(1);
    constraint->SetNumStepsBetweenCollisionTestInactive(1);
    return constraint;
}

} // namespace rally::sim
