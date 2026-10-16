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

// Medio intervalo de la diferencia centrada de ∂T_e/∂ω_e (rad/s).
constexpr float kEngineSlopeDelta = 0.5f;
// El automático solo ve derrape por encima de esta velocidad (m/s).
constexpr float kDriftMinSpeed = 5.0f;

// Parte del par de la caja que va a cada rueda (diferenciales abiertos): s_i.
std::array<float, kWheelCount> drive_shares(const CarData& car) {
    const float a = car.drive_front_share;
    return {0.5f * a, 0.5f * a, 0.5f * (1.0f - a), 0.5f * (1.0f - a)};
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
    : JPH::VehicleController(constraint), car_(settings.car), aids_(settings.aids),
      explicit_wheels_(settings.explicit_wheels) {
    contact_surface_.fill(Surface::Dirt);
    radius_.fill(car_.wheel_radius);
    drivetrain_reset(car_, drive_);
}

JPH::Ref<JPH::VehicleControllerSettings> CarController::GetSettings() const {
    auto* settings = new CarControllerSettings;
    settings->car = car_;
    settings->aids = aids_;
    settings->explicit_wheels = explicit_wheels_;
    return settings;
}

JPH::Wheel* CarController::ConstructWheel(const JPH::WheelSettings& wheel) const {
    return new CarWheel(wheel);
}

void CarController::reset() {
    // Estado dinámico a su valor inicial; la condición (radio_) no cambia.
    drivetrain_reset(car_, drive_);
    drive_out_ = DrivetrainOutput{};
    drive_out_.engine_omega = drive_.engine_omega;
    drive_out_.gear = drive_.gear;
    drive_out_.target_gear = drive_.target_gear;
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
    float drive_speed = 0.0f;
    const std::array<float, kWheelCount> shares = drive_shares(car_);
    for (int i = 0; i < kWheelCount; ++i) {
        const float omega = speed / radius_[static_cast<std::size_t>(i)];
        mConstraint.GetWheel(static_cast<JPH::uint>(i))->SetAngularVelocity(omega);
        drive_speed += shares[static_cast<std::size_t>(i)] * omega;
    }
    if (!(speed > 0.0f)) {
        return;
    }
    int gear = kGearCount;
    for (int g = 1; g <= kGearCount; ++g) {
        if (gear_ratio(car_, g) * drive_speed <= car_.upshift_full) {
            gear = g;
            break;
        }
    }
    drive_.gear = static_cast<std::int8_t>(gear);
    drive_.target_gear = drive_.gear;
    drive_.engine_omega = gear_ratio(car_, gear) * drive_speed;
    drive_.clutch = ClutchMode::Closed;
}

void CarController::set_gear(int gear) {
    const std::array<float, kWheelCount> shares = drive_shares(car_);
    float drive_speed = 0.0f;
    for (int i = 0; i < kWheelCount; ++i) {
        drive_speed += shares[static_cast<std::size_t>(i)] *
                       mConstraint.GetWheel(static_cast<JPH::uint>(i))->GetAngularVelocity();
    }
    drive_.gear = static_cast<std::int8_t>(gear);
    drive_.target_gear = drive_.gear;
    drive_.engine_omega = rmath::max(car_.idle_speed, gear_ratio(car_, gear) * drive_speed);
    drive_.clutch = gear != kNeutralGear && drive_.engine_omega >= car_.open_speed
                        ? ClutchMode::Closed
                        : ClutchMode::Launch;
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
    in.reverse = drive_.gear == kReverseGear;
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
    // Caja (sim/drivetrain.h): el cambio en curso, los botones con la regla D4 y el automático.
    const JPH::Body* body = mConstraint.GetVehicleBody();
    const JPH::Vec3 velocity = body->GetLinearVelocity();
    const JPH::RMat44 transform = body->GetWorldTransform();
    const float v_forward = velocity.Dot(transform.Multiply3x3(kCarForward));
    const float v_right = velocity.Dot(transform.GetAxisX());
    const std::array<float, kWheelCount> shares = drive_shares(car_);
    GearboxInput gearbox;
    gearbox.mode = aids_.gearbox;
    gearbox.shift_up = controls_.shift_up;
    gearbox.shift_down = controls_.shift_down;
    gearbox.throttle = controls_.throttle;
    gearbox.stopped = velocity.Length() < kGearStopSpeed;
    gearbox.drifting = magnitude(v_forward, v_right) > kDriftMinSpeed &&
                       rmath::abs(rmath::atan2(v_right, v_forward)) > car_.drift_angle;
    for (int i = 0; i < kWheelCount; ++i) {
        gearbox.drive_speed +=
            shares[static_cast<std::size_t>(i)] *
            mConstraint.GetWheel(static_cast<JPH::uint>(i))->GetAngularVelocity();
    }
    const std::int16_t drift_before = drive_.drift_steps;
    update_gearbox(car_, gearbox, drive_);
    drive_out_.drift_hold = drive_.drift_steps > drift_before;

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
    const std::array<float, kWheelCount> shares = drive_shares(car_);

    // Turbo, una vez por paso, con el acelerador del piloto (en un cambio sin levantar el pie la
    // presión se mantiene).
    drive_.boost = turbo_step(car_, drive_.boost, drive_.engine_omega, controls_.throttle, dt);
    const bool shifting = drive_.shift_steps > 0;
    // D3: el freno de mano desacopla el embrague y, en 4×4, el central.
    const bool handbrake_open = controls_.handbrake > kHandbrakeDecouple;
    const float ratio = shifting ? 0.0f : gear_ratio(car_, drive_.gear);

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

    // Entradas del neumático de cada rueda, fijas en los subpasos (velocidad del cuerpo fija).
    std::array<TireInput, kWheelCount> inputs{};
    std::array<JPH::RVec3, kWheelCount> points{};
    std::array<JPH::Vec3, kWheelCount> longitudinals{};
    std::array<JPH::Vec3, kWheelCount> laterals{};
    std::array<WheelDynamics, kWheelCount> states{};
    // ABS (car_controller.h): ruedas que modula en este paso, con su pie pedido, su rodadura, su
    // giro objetivo y el signo de v_x.
    std::array<bool, kWheelCount> abs_wheel{};
    std::array<float, kWheelCount> abs_foot{};
    std::array<float, kWheelCount> abs_rolling{};
    std::array<float, kWheelCount> abs_target{};
    std::array<float, kWheelCount> abs_sign{};
    for (int i = 0; i < kWheelCount; ++i) {
        const auto index = static_cast<std::size_t>(i);
        JPH::Wheel* w = mConstraint.GetWheel(static_cast<JPH::uint>(i));
        const bool front = i == kFrontLeft || i == kFrontRight;
        WheelOutput& out = outputs_[index];
        out = WheelOutput{};
        const float r = radius_[index];

        TireInput& in = inputs[index];
        in.radius = r;
        const float foot = controls_.brake * car_.brake_torque * 0.5f *
                           (front ? car_.brake_front_share : 1.0f - car_.brake_front_share);
        float brake = foot;
        if (!front) {
            brake += controls_.handbrake * car_.handbrake_torque;
        }
        out.brake_torque = brake;

        points[index] = JPH::RVec3::sZero();
        longitudinals[index] = JPH::Vec3::sZero();
        laterals[index] = JPH::Vec3::sZero();
        if (w->HasContact()) {
            // Carga: la impulsión normal de la suspensión (con la barra y el tope) del paso
            // anterior.
            in.load = rmath::max(0.0f, w->GetSuspensionLambda() / dt);
            const Surface surface = contact_surface_[index];
            in.surface = surface;
            points[index] = w->GetContactPosition();
            longitudinals[index] = w->GetContactLongitudinal();
            laterals[index] = w->GetContactLateral();
            const JPH::Vec3 body_velocity = rotation_skipped
                                                ? body->GetLinearVelocity()
                                                : body->GetPointVelocity(points[index]);
            const JPH::Vec3 velocity = body_velocity - w->GetContactPointVelocity();
            in.vx = velocity.Dot(longitudinals[index]);
            in.vy = velocity.Dot(laterals[index]);
            out.contact = true;
            out.surface = surface;
            out.load = in.load;
            out.h = contact_h_[index];
            out.compression = w->GetSettings()->mSuspensionMaxLength - w->GetSuspensionLength();
        }
        // Freno, freno de mano y rodadura: pares de Coulomb sobre la rueda (filas del LCP).
        in.stop_torque = brake + car_.tire(in.surface).rolling_resistance * in.load * r;
        states[index] = WheelDynamics{w->GetAngularVelocity(), u_[index], w_[index]};
        if (aids_.abs && foot > 0.0f && in.load > 0.0f && rmath::abs(in.vx) >= kAbsMinSpeed &&
            (front || !(controls_.handbrake > 0.0f))) {
            abs_wheel[index] = true;
            abs_foot[index] = foot;
            abs_rolling[index] = car_.tire(in.surface).rolling_resistance * in.load * r;
            abs_target[index] = in.vx * (1.0f - car_.tire(in.surface).slip_ratio_peak) / r;
            abs_sign[index] = sign_of(in.vx);
        }
    }

    // Filas fijas en el paso: Jacobianas (sim/drivetrain.h).
    DriveSystem system;
    system.jacobian[kRowClutch] = {1.0f, -ratio * shares[0], -ratio * shares[1], -ratio * shares[2],
                                   -ratio * shares[3]};
    system.jacobian[kRowViscous] = {0.0f, 0.5f, 0.5f, -0.5f, -0.5f};
    system.jacobian[kRowLsdFront] = {0.0f, 1.0f, -1.0f, 0.0f, 0.0f};
    system.jacobian[kRowLsdRear] = {0.0f, 0.0f, 0.0f, 1.0f, -1.0f};
    for (int i = 0; i < kWheelCount; ++i) {
        auto& row = system.jacobian[static_cast<std::size_t>(kRowStop + i)];
        row = {};
        row[static_cast<std::size_t>(i + 1)] = 1.0f;
    }
    const bool viscous_on = car_.viscous > 0.0f && car_.drive_front_share > 0.0f && !handbrake_open;
    system.active[kRowViscous] = viscous_on;
    system.soft[kRowViscous] = viscous_on ? 1.0f / (h * car_.viscous) : 0.0f;

    // Acelerador del motor: el del piloto o, en un cambio, el que lleva a las rpm de la marcha
    // nueva; nunca por debajo del regulador de ralentí.
    const auto engine_at = [&](float omega, float command) {
        const float throttle = rmath::max(command, idle_throttle(car_, omega));
        return engine_torque(car_, omega, throttle, drive_.boost);
    };

    std::array<TireForce, kWheelCount> sums{};
    float engine_torque_sum = 0.0f;
    float throttle_sum = 0.0f;
    float clutch_sum = 0.0f;
    float viscous_sum = 0.0f;
    std::array<float, 2> lsd_sum{};
    std::array<float, 2> axle_sum{};
    int max_iterations = 0;
    int failures = 0;
    bool clutch_open = shifting || handbrake_open || ratio == 0.0f;

    // Control de tracción (car_controller.h): motrices que cuentan en este paso y su permitido.
    std::array<bool, kWheelCount> traction_wheel{};
    std::array<float, kWheelCount> traction_allowed{};
    const float traction_dir = ratio < 0.0f ? -1.0f : 1.0f;
    bool traction_any = false;
    if (aids_.traction != TractionControl::Off && !clutch_open) {
        const float limit =
            aids_.traction == TractionControl::High ? kTractionHighLimit : kTractionLowLimit;
        for (std::size_t i = 0; i < kWheelCount; ++i) {
            if (shares[i] > 0.0f && inputs[i].load > 0.0f) {
                traction_wheel[i] = true;
                traction_any = true;
                traction_allowed[i] = rmath::max(
                    limit * car_.tire(inputs[i].surface).slip_ratio_peak * rmath::abs(inputs[i].vx),
                    kTractionMinSlip);
            }
        }
    }
    float traction_cut_sum = 0.0f;
    std::array<float, kWheelCount> abs_cut_sum{};
    std::array<bool, kWheelCount> abs_modulated{};
    drive_out_.traction_q.fill(0.0f);
    for (int s = 0; s < kWheelSubsteps; ++s) {
        // 1. Neumáticos: predicción con el giro sin cambiar.
        std::array<TirePrediction, kWheelCount> predictions{};
        float drive_speed = 0.0f;
        for (std::size_t i = 0; i < kWheelCount; ++i) {
            predictions[i] = predict_tire(car_, inputs[i], h, !explicit_wheels_, states[i]);
            drive_speed += shares[i] * states[i].omega;
        }

        // 2. Ruedas: inercia efectiva y velocidad libre (el motor va después de las capacidades:
        // el control de tracción las necesita para su resolución de prueba).
        const float omega_e = drive_.engine_omega;
        for (std::size_t i = 0; i < kWheelCount; ++i) {
            const float inv = 1.0f / wheel_effective_inertia(car_, inputs[i], h, predictions[i]);
            system.inv_mass[i + 1] = inv;
            system.velocity[i + 1] =
                states[i].omega + h * inv * (-inputs[i].radius * predictions[i].fx0);
        }

        // 3. Capacidades de las filas de Coulomb (impulso h·par).
        float capacity = 0.0f;
        if (!clutch_open) {
            capacity = clutch_capacity(car_, drive_.clutch, omega_e, controls_.throttle);
        }
        system.active[kRowClutch] = capacity > 0.0f;
        system.cap[kRowClutch] = h * capacity;
        const auto lsd_cap = [&](int axle, float preload, float drive, float coast) {
            const auto a = static_cast<std::size_t>(axle);
            const float axle_speed = 0.5f * (states[2 * a].omega + states[2 * a + 1].omega);
            const float torque = drive_.axle_torque[a];
            const float k = torque * axle_speed >= 0.0f ? drive : coast;
            return preload + k * rmath::abs(torque);
        };
        const float lsd_front =
            lsd_cap(0, car_.lsd_front_preload, car_.lsd_front_drive, car_.lsd_front_coast);
        const float lsd_rear =
            lsd_cap(1, car_.lsd_rear_preload, car_.lsd_rear_drive, car_.lsd_rear_coast);
        system.active[kRowLsdFront] = lsd_front > 0.0f;
        system.cap[kRowLsdFront] = h * lsd_front;
        system.active[kRowLsdRear] = lsd_rear > 0.0f;
        system.cap[kRowLsdRear] = h * lsd_rear;
        for (std::size_t i = 0; i < kWheelCount; ++i) {
            const auto row = static_cast<std::size_t>(kRowStop) + i;
            float stop = inputs[i].stop_torque;
            abs_modulated[i] = false;
            if (abs_wheel[i]) {
                // Velocidad libre de la rueda (sin las filas) en el sentido de v_x: con la rueda
                // girando a favor del suelo, el par que la deja en ω* en el subpaso.
                const float free = abs_sign[i] * system.velocity[i + 1];
                if (free > 0.0f) {
                    const float needed =
                        (free - abs_sign[i] * abs_target[i]) / (h * system.inv_mass[i + 1]);
                    const float applied = rmath::clamp(needed - abs_rolling[i], 0.0f, abs_foot[i]);
                    abs_cut_sum[i] += abs_foot[i] - applied;
                    stop = abs_rolling[i] + applied;
                    abs_modulated[i] = true;
                }
            }
            system.active[row] = stop > 0.0f;
            system.cap[row] = h * stop;
        }

        // 4. Motor linealizado con su acelerador: par y −∂T/∂ω (≥ 0) por diferencia centrada.
        const auto set_engine = [&](float throttle_command) {
            const float torque = engine_at(omega_e, throttle_command);
            const float slope =
                rmath::max(0.0f, -(engine_at(omega_e + kEngineSlopeDelta, throttle_command) -
                                   engine_at(omega_e - kEngineSlopeDelta, throttle_command)) /
                                     (2.0f * kEngineSlopeDelta));
            system.inv_mass[0] = 1.0f / (car_.engine_inertia + h * slope);
            system.velocity[0] = omega_e + h * system.inv_mass[0] * torque;
            return torque;
        };
        float command = shifting ? shift_throttle(car_, drive_, drive_speed) : controls_.throttle;
        if (traction_any) {
            // La motriz que más desliza respecto a su permitido, con el giro al empezar el
            // subpaso, y su q al final del subpaso con un giro dado.
            float q = -1.0e9f;
            std::size_t worst = 0;
            for (std::size_t i = 0; i < kWheelCount; ++i) {
                if (traction_wheel[i]) {
                    const float slip =
                        traction_dir * (states[i].omega * inputs[i].radius - inputs[i].vx);
                    if (slip / traction_allowed[i] > q) {
                        q = slip / traction_allowed[i];
                        worst = i;
                    }
                }
            }
            drive_out_.traction_q[static_cast<std::size_t>(s)] = q;
            // v_x del contacto es fija en el paso: al final del subpaso se le suma la
            // aceleración que dan las fuerzas previstas del neumático (si no, la aceleración del
            // coche contaría como deslizamiento que crece).
            float push = 0.0f;
            for (std::size_t i = 0; i < kWheelCount; ++i) {
                push += predictions[i].fx0;
            }
            const float vx_end = inputs[worst].vx + h * push / car_.mass;
            const auto q_at = [&](float omega) {
                return traction_dir * (omega * inputs[worst].radius - vx_end) /
                       traction_allowed[worst];
            };
            // Ritmo de q en el subpaso con el acelerador sin recortar (resolución de prueba, con
            // una copia del conjunto activo): el adelanto que amortigua el retraso del par en
            // las ruedas (embrague en arranque: el corte llega bajando las vueltas).
            std::array<RowState, kDriveRows> trial_rows = drive_.rows;
            set_engine(command);
            const float rate_open =
                (q_at(solve_drive(system, trial_rows).velocity[worst + 1]) - q) / h;
            const float lead = q + kTractionLead * rate_open - (1.0f - kTractionBand);
            float cut = 0.0f;
            if (lead > 0.0f && command > 0.0f) {
                // Con el corte entero: el ritmo baja en `gain` por unidad de corte (0 con el
                // embrague patinando). Corte que cumple c = (q + τ·ritmo(c) − (1 − b))/b.
                trial_rows = drive_.rows;
                set_engine(0.0f);
                const float rate_closed =
                    (q_at(solve_drive(system, trial_rows).velocity[worst + 1]) - q) / h;
                const float gain = rmath::max(0.0f, rate_open - rate_closed);
                cut = rmath::clamp(lead / (kTractionBand + kTractionLead * gain), 0.0f, 1.0f);
            }
            if (cut > 0.0f) {
                const float uncut = engine_at(omega_e, command);
                command *= 1.0f - cut;
                traction_cut_sum += uncut - engine_at(omega_e, command);
            }
        }
        const float t0 = set_engine(command);
        engine_torque_sum += t0;
        throttle_sum += rmath::max(command, idle_throttle(car_, omega_e));

        // 5. Tren motriz resuelto a la vez.
        const DriveSolution solution = solve_drive(system, drive_.rows);
        max_iterations = rmath::max(max_iterations, solution.iterations);
        if (!solution.converged) {
            ++failures;
        }
        drive_.engine_omega = rmath::max(0.0f, solution.velocity[0]);

        // 6. Huellas con el giro nuevo. El freno y la rodadura son pares de Coulomb, como en
        // step_tire: una rueda frenada pegada queda exactamente parada, y la que desliza nunca
        // invierte el giro (la tolerancia del conjunto activo deja un residuo de ~1e-8 rad/s).
        for (std::size_t i = 0; i < kWheelCount; ++i) {
            float omega = solution.velocity[i + 1];
            const RowState stop = drive_.rows[static_cast<std::size_t>(kRowStop) + i];
            if (stop == RowState::Stick || (stop == RowState::SlipPositive && omega > 0.0f) ||
                (stop == RowState::SlipNegative && omega < 0.0f)) {
                omega = 0.0f;
            }
            if (abs_modulated[i]) {
                const float vx = rmath::abs(inputs[i].vx);
                outputs_[i].abs_slip[static_cast<std::size_t>(s)] =
                    (vx - abs_sign[i] * omega * inputs[i].radius) /
                    (vx * car_.tire(inputs[i].surface).slip_ratio_peak);
            }
            const TireForce f = finish_tire(car_, inputs[i], h, predictions[i], omega, states[i]);
            sums[i].fx += f.fx;
            sums[i].fy += f.fy;
            sums[i].fx_pure += f.fx_pure;
            sums[i].fy_pure += f.fy_pure;
        }

        // 7. Par de entrada de cada eje (embrague y viscoacoplador), para la cota de los
        // autoblocantes del subpaso siguiente.
        const float inv_h = 1.0f / h;
        for (std::size_t a = 0; a < 2; ++a) {
            float torque = 0.0f;
            for (const std::size_t i : {2 * a, 2 * a + 1}) {
                torque += (system.jacobian[kRowClutch][i + 1] * solution.impulse[kRowClutch] +
                           system.jacobian[kRowViscous][i + 1] * solution.impulse[kRowViscous]) *
                          inv_h;
            }
            drive_.axle_torque[a] = torque;
            axle_sum[a] += torque;
        }
        clutch_sum += -solution.impulse[kRowClutch] * inv_h;
        viscous_sum += -solution.impulse[kRowViscous] * inv_h;
        lsd_sum[0] += rmath::abs(solution.impulse[kRowLsdFront]) * inv_h;
        lsd_sum[1] += rmath::abs(solution.impulse[kRowLsdRear]) * inv_h;

        // 8. Embrague: arranque → cerrado cuando la fila se pega; cerrado → arranque por debajo
        // de open_rpm (nunca cala).
        if (drive_.clutch == ClutchMode::Launch && system.active[kRowClutch] &&
            drive_.rows[kRowClutch] == RowState::Stick) {
            drive_.clutch = ClutchMode::Closed;
        } else if (drive_.clutch == ClutchMode::Closed && drive_.engine_omega < car_.open_speed) {
            drive_.clutch = ClutchMode::Launch;
        }
    }
    lcp_failures_total_ += failures;

    // Fuerzas al cuerpo (media de los subpasos) y salidas de cada rueda.
    applied_force_ = JPH::Vec3::sZero();
    constexpr float kInvSubsteps = 1.0f / kWheelSubsteps;
    for (int i = 0; i < kWheelCount; ++i) {
        const auto index = static_cast<std::size_t>(i);
        JPH::Wheel* w = mConstraint.GetWheel(static_cast<JPH::uint>(i));
        WheelOutput& out = outputs_[index];
        const TireInput& in = inputs[index];
        const WheelDynamics& state = states[index];
        const TireSurface& tire = car_.tire(in.surface);
        w->SetAngularVelocity(state.omega);
        u_[index] = state.u;
        w_[index] = state.w;
        const float fx = sums[index].fx / kWheelSubsteps;
        const float fy = sums[index].fy / kWheelSubsteps;

        out.omega = state.omega;
        out.locked = state.omega == 0.0f;
        out.u = state.u;
        out.w = state.w;
        out.kappa = state.u / tire.relaxation_long;
        out.alpha = rmath::atan(state.w / tire.relaxation_lat);
        out.slip_x = state.omega * in.radius - in.vx;
        out.slip_y = -in.vy;
        out.contact_speed = magnitude(in.vx, in.vy);
        out.fx = fx;
        out.fy = fy;
        out.fx_pure = sums[index].fx_pure / kWheelSubsteps;
        out.fy_pure = sums[index].fy_pure / kWheelSubsteps;
        out.mu = out.contact ? peak_mu(tire, in.load, car_.nominal_load) : 0.0f;
        out.abs_torque_cut = abs_cut_sum[index] * kInvSubsteps;
        if (out.contact) {
            const JPH::Vec3 force = longitudinals[index] * fx + laterals[index] * fy;
            body->AddForce(force, points[index]);
            applied_force_ += force;
        }
    }

    // Salida del tren motriz.
    DrivetrainOutput& d = drive_out_;
    d.engine_omega = drive_.engine_omega;
    d.boost = drive_.boost;
    d.gear = drive_.gear;
    d.target_gear = drive_.target_gear;
    d.shifting = shifting;
    d.clutch = drive_.clutch;
    d.clutch_open = clutch_open;
    float drive_speed = 0.0f;
    for (std::size_t i = 0; i < kWheelCount; ++i) {
        drive_speed += shares[i] * states[i].omega;
    }
    d.clutch_slip = ratio != 0.0f ? drive_.engine_omega - ratio * drive_speed : 0.0f;
    d.clutch_torque = clutch_sum * kInvSubsteps;
    d.engine_torque = engine_torque_sum * kInvSubsteps;
    d.throttle = throttle_sum * kInvSubsteps;
    d.axle_torque = {axle_sum[0] * kInvSubsteps, axle_sum[1] * kInvSubsteps};
    d.center_slip =
        0.5f * (states[0].omega + states[1].omega) - 0.5f * (states[2].omega + states[3].omega);
    d.viscous_torque = viscous_sum * kInvSubsteps;
    d.lsd_torque = {lsd_sum[0] * kInvSubsteps, lsd_sum[1] * kInvSubsteps};
    d.lcp_iterations = max_iterations;
    d.lcp_failures = failures;
    d.traction_torque_cut = traction_cut_sum * kInvSubsteps;

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
    // Dinámico: el tren motriz.
    stream.Write(drive_.engine_omega);
    stream.Write(drive_.boost);
    stream.Write(drive_.gear);
    stream.Write(drive_.target_gear);
    stream.Write(drive_.shift_steps);
    stream.Write(drive_.drift_steps);
    stream.Write(static_cast<std::uint8_t>(drive_.clutch));
    stream.Write(drive_.axle_torque[0]);
    stream.Write(drive_.axle_torque[1]);
    for (const RowState row : drive_.rows) {
        stream.Write(static_cast<std::uint8_t>(row));
    }
    // Dinámico: las ruedas.
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
    stream.Read(drive_.engine_omega);
    stream.Read(drive_.boost);
    stream.Read(drive_.gear);
    stream.Read(drive_.target_gear);
    stream.Read(drive_.shift_steps);
    stream.Read(drive_.drift_steps);
    std::uint8_t clutch = 0;
    stream.Read(clutch);
    drive_.clutch = static_cast<ClutchMode>(clutch);
    stream.Read(drive_.axle_torque[0]);
    stream.Read(drive_.axle_torque[1]);
    for (RowState& row : drive_.rows) {
        std::uint8_t value = 0;
        stream.Read(value);
        row = static_cast<RowState>(value);
    }
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
                                                     float gravity, const DriverAids& aids,
                                                     bool explicit_wheels) {
    const float max_length = kMinSuspensionLength + car.travel;
    // Dureza: la media no lee los multiplicadores (ni un ×1 pasa por el código).
    const SuspensionScale* scale = nullptr;
    if (aids.suspension == SuspensionSetting::Soft) {
        scale = &car.soft;
    } else if (aids.suspension == SuspensionSetting::Hard) {
        scale = &car.hard;
    }

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
        if (scale != nullptr) {
            // Blanda o dura: muelle y amortiguador con sus multiplicadores y el anclaje de la
            // media. Para que en equilibrio la suspensión siga midiendo max_length - sag, la
            // longitud natural del muelle (max + precarga en Jolt) se mueve en sag' - sag: con
            // la blanda (sag' > sag), precarga; con la dura, extensión máxima más corta.
            const float scaled_omega = kTwoPi * frequency * scale->frequency;
            const float scaled_stiffness = corner_mass * scaled_omega * scaled_omega;
            wheel->mSuspensionSpring.mStiffness = scaled_stiffness;
            wheel->mSuspensionSpring.mDamping = 2.0f * car.damping_ratio * scale->damping *
                                                rmath::sqrt(scaled_stiffness * corner_mass);
            const float scaled_sag = static_load / scaled_stiffness;
            if (scaled_sag > sag) {
                wheel->mSuspensionPreloadLength = scaled_sag - sag;
            } else {
                wheel->mSuspensionMaxLength = max_length - (sag - scaled_sag);
            }
        }
        settings.mWheels.push_back(wheel);
    }
    settings.mAntiRollBars.resize(2);
    settings.mAntiRollBars[0].mLeftWheel = kFrontLeft;
    settings.mAntiRollBars[0].mRightWheel = kFrontRight;
    settings.mAntiRollBars[0].mStiffness = car.antiroll_front;
    settings.mAntiRollBars[1].mLeftWheel = kRearLeft;
    settings.mAntiRollBars[1].mRightWheel = kRearRight;
    settings.mAntiRollBars[1].mStiffness = car.antiroll_rear;
    if (scale != nullptr) {
        settings.mAntiRollBars[0].mStiffness = car.antiroll_front * scale->antiroll;
        settings.mAntiRollBars[1].mStiffness = car.antiroll_rear * scale->antiroll;
    }

    auto* controller = new CarControllerSettings;
    controller->car = car;
    controller->aids = aids;
    controller->explicit_wheels = explicit_wheels;
    settings.mController = controller;

    JPH::Ref<JPH::VehicleConstraint> constraint = new JPH::VehicleConstraint(body, settings);
    constraint->SetNumStepsBetweenCollisionTestActive(1);
    constraint->SetNumStepsBetweenCollisionTestInactive(1);
    return constraint;
}

} // namespace rally::sim
