#pragma once

#include "sim/car_data.h"
#include "sim/jolt_config.h"
#include "sim/steer_assist.h"
#include "sim/surface.h"
#include "sim/surface_material.h"
#include "sim/tire.h"

#include <Jolt/Physics/Body/BodyCreationSettings.h>
#include <Jolt/Physics/Vehicle/VehicleCollisionTester.h>
#include <Jolt/Physics/Vehicle/VehicleConstraint.h>
#include <Jolt/Physics/Vehicle/VehicleController.h>

#include <array>
#include <cstdint>

namespace rally::sim {

// Ruedas en este orden en la VehicleConstraint, en el controlador y en la telemetría.
inline constexpr int kWheelCount = 4;
enum WheelIndex : int { kFrontLeft = 0, kFrontRight = 1, kRearLeft = 2, kRearRight = 3 };

// Subpasos del giro de las ruedas en cada paso de 1/240 s (960 Hz).
inline constexpr int kWheelSubsteps = 4;

// Longitud de la suspensión (anclaje al centro de la rueda) con la rueda arriba del todo.
inline constexpr float kMinSuspensionLength = 0.05f;

// Densidad del aire para la resistencia aerodinámica (ISA, nivel del mar).
inline constexpr float kAirDensity = 1.225f;

// Selector provisional de la tracción (D4): R – N – adelante. Lo sustituye la caja de la 2.6.
enum class Gear : std::int8_t { Reverse = -1, Neutral = 0, Forward = 1 };

// Por debajo de esta velocidad (2 km/h) el coche está parado para el selector.
inline constexpr float kGearStopSpeed = 2.0f / 3.6f;

// Mandos aplicados de un paso, normalizados (dirección en [-1, 1], positivo a la derecha; el
// resto en [0, 1]) y las pulsaciones de marcha.
struct CarControls {
    float steer = 0.0f;
    float throttle = 0.0f;
    float brake = 0.0f;
    float handbrake = 0.0f;
    bool shift_up = false;
    bool shift_down = false;
};

// Lo que hizo cada rueda en el último paso. Salida de diagnóstico (telemetría y dibujo): no
// es estado de la simulación y se recalcula en cada paso.
struct WheelOutput {
    bool contact = false;
    Surface surface = Surface::Dirt; // la del material del contacto
    float load = 0.0f;               // N: carga normal (suspensión del paso anterior)
    float compression = 0.0f;        // m desde la extensión máxima
    float kappa = 0.0f;              // deslizamiento relajado κ_r = u/σ_x (σ de la superficie)
    float alpha = 0.0f;              // rad: α_r = atan(w/σ_y), < 0 con la rueda yendo a la derecha
    float slip_x = 0.0f;             // m/s: s_x = ω·r − v_x al final del paso
    float slip_y = 0.0f;             // m/s: s_y = −v_y
    float contact_speed = 0.0f;      // m/s: |v| del cuerpo en el contacto, en su plano
    float fx = 0.0f;                 // N, media de los subpasos (adelante +)
    float fy = 0.0f;                 // N, media de los subpasos (derecha +)
    float fx_pure = 0.0f;            // N: la que daría κ_r solo, media de los subpasos
    float fy_pure = 0.0f;            // N: la que daría α_r solo, media de los subpasos
    float mu = 0.0f;                 // μ(F_z): pico con la sensibilidad a la carga
    float omega = 0.0f;              // rad/s al final del paso (adelante +)
    bool locked = false;             // rueda parada por fricción estática al final del paso
    float u = 0.0f;                  // m: deformación longitudinal de la huella
    float w = 0.0f;                  // m: deformación lateral de la huella
    float brake_torque = 0.0f;       // N·m pedido (freno y freno de mano)
    float h = 0.0f;                  // m: media de h sobre la huella
};

// Ajustes del controlador (los construye make_car).
class CarControllerSettings final : public JPH::VehicleControllerSettings {
public:
    CarData car;
    // Diagnóstico de los tests: integración explícita del giro de las ruedas (sin el término
    // implícito de dFx/dω). No se graba: el juego y las grabaciones siempre usan la
    // semi-implícita.
    bool explicit_wheels = false;

    void SaveBinaryState(JPH::StreamOut&) const override {}
    void RestoreBinaryState(JPH::StreamIn&) override {}
    JPH::VehicleController* ConstructController(JPH::VehicleConstraint& constraint) const override;
};

// Controlador propio: la VehicleConstraint de Jolt solo pone la suspensión y el contacto; las
// fuerzas del neumático y la resistencia del aire son fuerzas explícitas sobre el cuerpo,
// aplicadas en PostCollide (Jolt las integra en el mismo paso). Su estado, todo en SaveState
// (el giro y el ángulo de las ruedas los guarda la restricción), tiene dos partes:
//   dinámico: giro, deformación de la huella, ángulo de giro, dirección, selector y superficie
//   del último contacto de cada rueda; recolocar lo pone a cero (salvo la superficie, que es la
//   del suelo que pisa);
//   condición, por rueda: el radio efectivo (después, daños, presión, pinchazos); persiste al
//   recolocar y viaja en el cambio de host.
class CarController final : public JPH::VehicleController {
public:
    CarController(JPH::VehicleConstraint& constraint, const CarControllerSettings& settings);

    // Mandos del paso: SimWorld los pone antes de Update.
    void set_controls(const CarControls& controls) { controls_ = controls; }

    // Recolocar: el estado dinámico a cero (selector adelante); la condición no cambia.
    void reset();

    // Arranque de los tests con velocidad: giro de las ruedas a juego con `speed` (m/s).
    void set_rolling(float speed);

    // Condición: radio efectivo de la rueda (al construir, el del archivo). Lo leen el
    // neumático y el tester. Hoy solo lo cambian los tests (los daños llegan en la fase 4).
    float wheel_radius(int index) const { return radius_[static_cast<std::size_t>(index)]; }
    void set_wheel_radius(int index, float radius) {
        radius_[static_cast<std::size_t>(index)] = radius;
    }

    // Estado que usa la ayuda de dirección, leído al principio del paso (antes de Update): solo
    // lee el cuerpo, el contacto y la carga de las ruedas (estado de Jolt) y la superficie del
    // último contacto (SaveState).
    SteerAssistInput steer_assist_input() const;

    Gear gear() const { return gear_; }
    const WheelOutput& wheel(int index) const { return outputs_[static_cast<std::size_t>(index)]; }
    // Suma de las fuerzas propias del último paso (neumático y aire), en el mundo.
    JPH::Vec3 applied_force() const { return applied_force_; }
    // Si en el último paso el neumático usó la velocidad lineal del cuerpo porque Jolt no lo iba a
    // girar (zona muerta del giro, car_controller.cpp). Diagnóstico de los tests.
    bool rotation_skipped() const { return rotation_skipped_; }
    const CarData& car() const { return car_; }

    // Lo escribe el tester de las ruedas al encontrar el suelo.
    void set_contact_relief(int index, float h, Surface surface);

    JPH::Ref<JPH::VehicleControllerSettings> GetSettings() const override;

protected:
    JPH::Wheel* ConstructWheel(const JPH::WheelSettings& wheel) const override;
    bool AllowSleep() const override { return false; }
    void PreCollide(float dt, JPH::PhysicsSystem& system) override;
    void PostCollide(float dt, JPH::PhysicsSystem& system) override;
    bool SolveLongitudinalAndLateralConstraints(float) override { return false; }
    void SaveState(JPH::StateRecorder& stream) const override;
    void RestoreState(JPH::StateRecorder& stream) override;
#ifdef JPH_DEBUG_RENDERER
    void Draw(JPH::DebugRenderer*) const override {}
#endif

private:
    CarData car_;
    bool explicit_wheels_ = false;
    CarControls controls_;

    // Estado dinámico (SaveState; recolocar lo pone a cero). El giro está en las ruedas de Jolt.
    Gear gear_ = Gear::Forward;
    std::array<float, kWheelCount> u_{};     // m: deformación longitudinal de la huella
    std::array<float, kWheelCount> w_{};     // m: deformación lateral de la huella
    std::array<float, kWheelCount> steer_{}; // ángulo de Jolt (positivo a la izquierda)
    // Condición (SaveState; recolocar no la toca).
    std::array<float, kWheelCount> radius_{}; // m: radio efectivo

    // Salidas del último paso.
    std::array<WheelOutput, kWheelCount> outputs_{};
    std::array<float, kWheelCount> contact_h_{};
    std::array<Surface, kWheelCount> contact_surface_{};
    JPH::Vec3 applied_force_ = JPH::Vec3::sZero();
    bool rotation_skipped_ = false;
};

// Tester de las ruedas: un cilindro como el de VehicleCollisionTesterCastCylinder de Jolt, pero
// con el radio efectivo de la condición, y sobre él la media de h sobre la huella (largo ×
// ancho del neumático, 3 × 3 muestras) con el h del material del contacto. La rueda sube esa
// media: se acorta la suspensión y se sube el punto de contacto, que queda bajo el centro de
// la rueda. La normal es la de la geometría.
class CarWheelTester final : public JPH::VehicleCollisionTester {
public:
    CarWheelTester(JPH::ObjectLayer layer, const ReliefSource& relief, CarController& controller);

    bool Collide(JPH::PhysicsSystem& system, const JPH::VehicleConstraint& constraint,
                 JPH::uint wheel_index, JPH::RVec3Arg origin, JPH::Vec3Arg direction,
                 const JPH::BodyID& vehicle_body, JPH::Body*& out_body,
                 JPH::SubShapeID& out_sub_shape, JPH::RVec3& out_position, JPH::Vec3& out_normal,
                 float& out_suspension_length) const override;
    void PredictContactProperties(JPH::PhysicsSystem& system,
                                  const JPH::VehicleConstraint& constraint, JPH::uint wheel_index,
                                  JPH::RVec3Arg origin, JPH::Vec3Arg direction,
                                  const JPH::BodyID& vehicle_body, JPH::Body*& io_body,
                                  JPH::SubShapeID& io_sub_shape, JPH::RVec3& io_position,
                                  JPH::Vec3& io_normal, float& io_suspension_length) const override;

private:
    JPH::ObjectLayer layer_;
    ReliefSource relief_;
    CarController& controller_;
};

// Ajustes del cuerpo del coche: caja de la carrocería con el centro de masas desplazado, masa
// e inercias del archivo, sin dormirse y sin amortiguamiento de Jolt (el aire lo pone el
// controlador). `position` es el origen del coche (en el suelo, en equilibrio).
JPH::BodyCreationSettings car_body_settings(const CarData& car, JPH::RVec3Arg position,
                                            JPH::QuatArg rotation, float speed);

// Crea la VehicleConstraint del coche sobre su cuerpo ya creado (sin añadirla al sistema).
// Muelles con la masa suspendida de cada esquina (la frecuencia del archivo es la del coche).
// Coloca los anclajes de la suspensión para que, en equilibrio en llano con el reparto del
// archivo, el centro de las ruedas quede a su radio del suelo y la caja horizontal.
JPH::Ref<JPH::VehicleConstraint> make_car_constraint(JPH::Body& body, const CarData& car,
                                                     float gravity, bool explicit_wheels);

} // namespace rally::sim
