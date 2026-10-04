#include "sim/car_controller.h"

#include "sim/check.h"
#include "sim/layers.h"
#include "sim/rmath.h"
#include "sim/test_zone.h"

#include <Jolt/Physics/Body/Body.h>
#include <Jolt/Physics/Collision/Shape/BoxShape.h>
#include <Jolt/Physics/Collision/Shape/OffsetCenterOfMassShape.h>
#include <Jolt/Physics/Collision/Shape/RotatedTranslatedShape.h>
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

// Lleva `omega` hacia 0 en `delta` sin pasarlo (freno y rodadura nunca invierten el giro).
float toward_zero(float omega, float delta) {
    if (rmath::abs(omega) <= delta) {
        return 0.0f;
    }
    return omega - sign_of(omega) * delta;
}

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
    // Ancla: muelle y amortiguador críticos sobre la cuarta parte de la masa.
    const float wheel_mass = 0.25f * car_.mass;
    const float omega = kTwoPi * car_.hold_frequency;
    anchor_stiffness_ = wheel_mass * omega * omega;
    anchor_damping_ = 2.0f * wheel_mass * omega;
    contact_surface_.fill(Surface::Dirt);
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
    gear_ = Gear::Forward;
    anchored_.fill(0);
    anchor_ = {};
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
            ->SetAngularVelocity(speed / car_.wheel_radius);
    }
}

void CarController::set_contact_relief(int index, float h, Surface surface) {
    contact_h_[static_cast<std::size_t>(index)] = h;
    contact_surface_[static_cast<std::size_t>(index)] = surface;
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
    // mezcla de ese ángulo con el de Ackermann puro. En Jolt el ángulo es positivo a la
    // izquierda; steer > 0 es a la derecha.
    const float inner = rmath::abs(controls_.steer) * car_.max_steer;
    float outer = inner;
    if (inner > 0.0f) {
        const float ackermann =
            rmath::atan(car_.wheelbase / (car_.wheelbase / rmath::tan(inner) + car_.track));
        outer = (1.0f - car_.ackermann) * inner + car_.ackermann * ackermann;
    }
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
    const float r = car_.wheel_radius;
    const float inertia = car_.wheel_inertia;
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

    applied_force_ = JPH::Vec3::sZero();
    for (int i = 0; i < kWheelCount; ++i) {
        const auto index = static_cast<std::size_t>(i);
        JPH::Wheel* w = mConstraint.GetWheel(static_cast<JPH::uint>(i));
        const bool front = i == kFrontLeft || i == kFrontRight;
        WheelOutput& out = outputs_[index];
        out = WheelOutput{};

        const float drive_torque =
            drive * 0.5f * (front ? car_.drive_front_share : 1.0f - car_.drive_front_share);
        float stop_torque = controls_.brake * car_.brake_torque * 0.5f *
                            (front ? car_.brake_front_share : 1.0f - car_.brake_front_share);
        if (!front) {
            stop_torque += controls_.handbrake * car_.handbrake_torque;
        }
        out.brake_torque = stop_torque;
        const JPH::WheelSettings* settings = w->GetSettings();
        out.compression = settings->mSuspensionMaxLength - w->GetSuspensionLength();
        float omega = w->GetAngularVelocity();

        if (!w->HasContact()) {
            // En el aire: el par motor la acelera y el freno la para, sin neumático.
            anchored_[index] = 0;
            out.compression = 0.0f;
            for (int s = 0; s < kWheelSubsteps; ++s) {
                omega = toward_zero(omega + h * drive_torque / inertia, h * stop_torque / inertia);
            }
            w->SetAngularVelocity(omega);
            out.omega = omega;
            continue;
        }

        // Carga: la impulsión normal de la suspensión (con la barra y el tope) del paso
        // anterior.
        const float load = rmath::max(0.0f, w->GetSuspensionLambda() / dt);
        const Surface surface = contact_surface_[index];
        const float mu = car_.mu(surface);
        const JPH::RVec3 point = w->GetContactPosition();
        const JPH::Vec3 longitudinal = w->GetContactLongitudinal();
        const JPH::Vec3 lateral = w->GetContactLateral();
        const JPH::Vec3 velocity = body->GetPointVelocity(point) - w->GetContactPointVelocity();
        const float vx = velocity.Dot(longitudinal);
        const float vy = velocity.Dot(lateral);
        const float speed = rmath::sqrt(vx * vx + vy * vy);
        out.contact = true;
        out.surface = surface;
        out.load = load;
        out.h = contact_h_[index];

        const float denominator = rmath::max(rmath::abs(vx), car_.min_slip_speed);
        const float c_kappa = car_.long_stiffness * load;
        const float c_alpha = car_.lat_stiffness * load;
        const float alpha = rmath::atan2(vy, denominator);
        out.alpha = alpha;
        const float limit = mu * load;
        const float stop_total = stop_torque + car_.rolling_resistance * load * r;

        // Ancla de parada: con el freno ganando al motor y la rueda casi quieta, un muelle
        // sujeta el punto de contacto. Su fuerza no pasa de μ·Fz ni de lo que da el freno.
        const bool hold =
            stop_torque > 0.0f && stop_torque > rmath::abs(drive_torque) && load > 0.0f;
        if (!hold) {
            anchored_[index] = 0;
        } else if (anchored_[index] == 0 && speed < car_.hold_speed &&
                   rmath::abs(omega * r) < car_.hold_speed) {
            anchored_[index] = 1;
            anchor_[index] = {point.GetX(), point.GetY(), point.GetZ()};
        }

        float fx = 0.0f;
        float fy = 0.0f;
        if (anchored_[index] != 0) {
            const std::array<double, 3>& anchor = anchor_[index];
            const JPH::Vec3 offset(point - JPH::RVec3(anchor[0], anchor[1], anchor[2]));
            const float dx = offset.Dot(longitudinal);
            const float dy = offset.Dot(lateral);
            fx = -anchor_stiffness_ * dx - anchor_damping_ * vx;
            fy = -anchor_stiffness_ * dy - anchor_damping_ * vy;
            const float anchor_limit = rmath::min(limit, stop_torque / r);
            const float magnitude = rmath::sqrt(fx * fx + fy * fy);
            if (magnitude > anchor_limit) {
                // Se rompe: Coulomb. El ancla se arrastra hasta donde el muelle da el límite;
                // si además la rueda ya se mueve, se suelta.
                const float scale = anchor_limit / magnitude;
                fx *= scale;
                fy *= scale;
                const float new_dx = -(fx + anchor_damping_ * vx) / anchor_stiffness_;
                const float new_dy = -(fy + anchor_damping_ * vy) / anchor_stiffness_;
                const JPH::RVec3 moved =
                    point - JPH::RVec3(longitudinal * new_dx + lateral * new_dy);
                anchor_[index] = {moved.GetX(), moved.GetY(), moved.GetZ()};
                if (speed > car_.hold_speed) {
                    anchored_[index] = 0;
                }
            }
            omega = 0.0f;
            out.anchored = true;
            out.contact_speed = speed;
            out.kappa = -vx / denominator;
        } else {
            // Giro de la rueda en subpasos con la velocidad del cuerpo fija. Semi-implícito en
            // el neumático: ω' = ω + h·(T − r·Fx(ω)) / (I + h·r·∂Fx/∂ω), con ∂Fx/∂ω = 0 si la
            // fuerza está en el límite. El freno y la rodadura llevan ω hacia 0 con el mismo
            // denominador y nunca la invierten.
            const float blend = rmath::clamp(speed / car_.low_speed_blend, 0.0f, 1.0f);
            const float fy_free = blend * (-c_alpha * alpha) +
                                  (1.0f - blend) * (-c_alpha / car_.low_speed_blend * vy);
            float sum_fx = 0.0f;
            float sum_fy = 0.0f;
            float kappa = 0.0f;
            for (int s = 0; s < kWheelSubsteps; ++s) {
                kappa = (omega * r - vx) / denominator;
                float sub_fx = c_kappa * kappa;
                float sub_fy = fy_free;
                float slope = c_kappa * r / denominator; // ∂Fx/∂ω
                const float magnitude = rmath::sqrt(sub_fx * sub_fx + sub_fy * sub_fy);
                if (magnitude > limit) {
                    const float scale = magnitude > 0.0f ? limit / magnitude : 0.0f;
                    sub_fx *= scale;
                    sub_fy *= scale;
                    slope = 0.0f;
                }
                if (explicit_wheels_) {
                    slope = 0.0f;
                }
                const float denominator_i = inertia + h * r * slope;
                const float free = omega + h * (drive_torque - r * sub_fx) / denominator_i;
                const float next = toward_zero(free, h * stop_total / denominator_i);
                sum_fx += sub_fx + slope * (next - omega);
                sum_fy += sub_fy;
                omega = next;
            }
            fx = sum_fx / kWheelSubsteps;
            fy = sum_fy / kWheelSubsteps;
            out.kappa = kappa;
        }
        w->SetAngularVelocity(omega);
        out.omega = omega;
        out.fx = fx;
        out.fy = fy;

        const JPH::Vec3 force = longitudinal * fx + lateral * fy;
        body->AddForce(force, point);
        applied_force_ += force;
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
    stream.Write(static_cast<std::int8_t>(gear_));
    for (std::size_t i = 0; i < kWheelCount; ++i) {
        stream.Write(anchored_[i]);
        stream.Write(anchor_[i]);
        stream.Write(steer_[i]);
    }
}

void CarController::RestoreState(JPH::StateRecorder& stream) {
    std::int8_t gear = 0;
    stream.Read(gear);
    gear_ = static_cast<Gear>(gear);
    for (std::size_t i = 0; i < kWheelCount; ++i) {
        stream.Read(anchored_[i]);
        stream.Read(anchor_[i]);
        stream.Read(steer_[i]);
        mConstraint.GetWheel(static_cast<JPH::uint>(i))->SetSteerAngle(steer_[i]);
    }
}

CarWheelTester::CarWheelTester(JPH::ObjectLayer layer, const ReliefSource& relief,
                               CarController& controller)
    : cylinder_(new JPH::VehicleCollisionTesterCastCylinder(layer)), relief_(relief),
      controller_(controller) {}

bool CarWheelTester::Collide(JPH::PhysicsSystem& system, const JPH::VehicleConstraint& constraint,
                             JPH::uint wheel_index, JPH::RVec3Arg origin, JPH::Vec3Arg direction,
                             const JPH::BodyID& vehicle_body, JPH::Body*& out_body,
                             JPH::SubShapeID& out_sub_shape, JPH::RVec3& out_position,
                             JPH::Vec3& out_normal, float& out_suspension_length) const {
    if (!cylinder_->Collide(system, constraint, wheel_index, origin, direction, vehicle_body,
                            out_body, out_sub_shape, out_position, out_normal,
                            out_suspension_length)) {
        return false;
    }
    const auto* material =
        static_cast<const SurfaceMaterial*>(out_body->GetShape()->GetMaterial(out_sub_shape));
    const Surface surface = material->surface();
    const float radius = constraint.GetWheel(wheel_index)->GetSettings()->mRadius;
    // El cilindro da el punto más profundo del contacto, que en suelo llano salta de un borde
    // al otro del ancho de la rueda: el brazo de la suspensión, la muestra de h y el ancla
    // saltarían con él. El contacto se pone bajo el centro de la rueda, en la normal.
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
        const float half_width = 0.5f * controller_.car().wheel_width;
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

void CarWheelTester::PredictContactProperties(
    JPH::PhysicsSystem& system, const JPH::VehicleConstraint& constraint, JPH::uint wheel_index,
    JPH::RVec3Arg origin, JPH::Vec3Arg direction, const JPH::BodyID& vehicle_body,
    JPH::Body*& io_body, JPH::SubShapeID& io_sub_shape, JPH::RVec3& io_position,
    JPH::Vec3& io_normal, float& io_suspension_length) const {
    // No se usa: las ruedas se prueban en cada paso (SetNumStepsBetweenCollisionTest* = 1).
    cylinder_->PredictContactProperties(system, constraint, wheel_index, origin, direction,
                                        vehicle_body, io_body, io_sub_shape, io_position, io_normal,
                                        io_suspension_length);
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
