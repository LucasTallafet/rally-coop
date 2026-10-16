#pragma once

#include "sim/car_data.h"
#include "sim/drivetrain.h"
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

// Freno de mano aplicado por encima de esta fracción: desacopla el embrague y, en 4×4, el
// central (D3).
inline constexpr float kHandbrakeDecouple = 0.1f;

// ABS (tarea 2.8): por rueda y en cada subpaso, el par del freno de pie se recorta a la cota que
// lleva la rueda al giro del objetivo, ω* = v_x·(1 − κ_pico)/r (κ_pico de la superficie que
// pisa): con la velocidad libre del subpaso x* (sin las filas de Coulomb, con el término
// semi-implícito del neumático en la inercia efectiva I), el par de parada que deja la rueda en
// ω* es sgn(v_x)·(x* − ω*)·I/h; el pie aplicado es ese par menos la rodadura, entre 0 y el pie
// pedido. Sin estado. No actúa por debajo de kAbsMinSpeed en |v_x| de la rueda (bloquea al
// final, como un ABS real, y en la rampa sujeta igual que sin él), en una trasera con el freno
// de mano tirado, sin carga, ni con la rueda girando contra el suelo. Nunca toca el freno de
// mano ni añade fuerzas.
inline constexpr float kAbsMinSpeed = 6.0f / 3.6f; // m/s

// Control de tracción (tarea 2.8): en cada subpaso, para cada motriz con carga, deslizamiento en
// el sentido del par s = d·(ω·r − v_x) (d: signo de la relación de la marcha) y permitido
// a = max(límite·κ_pico·|v_x|, kTractionMinSlip); q = s/a de la que más desliza. El acelerador
// del motor (no el del turbo ni el del embrague) se multiplica por 1 − c, con
// c = clamp((q + τ·q' − (1 − kTractionBand))/kTractionBand, 0, 1) y τ = kTractionLead: el corte
// es entero en el permitido, así que en régimen (q' = 0) el deslizamiento queda por debajo de
// él. El adelanto τ·q' amortigua el retraso entre el corte y el par en las ruedas (con el
// embrague en arranque, el corte solo llega bajando las vueltas del motor): q' es el ritmo de q
// en el subpaso, de dos resoluciones de prueba del tren motriz (sin corte y con el corte
// entero; el ritmo es lineal en el corte) y c sale de la ecuación lineal, implícita en q'.
// Sin estado entre subpasos. No actúa con el embrague abierto ni sin motrices con carga.
inline constexpr float kTractionLowLimit = 1.8f;  // × κ pico
inline constexpr float kTractionHighLimit = 1.2f; // × κ pico
inline constexpr float kTractionBand = 0.25f;     // del permitido
inline constexpr float kTractionMinSlip = 0.4f;   // m/s: el arranque parado
inline constexpr float kTractionLead = 0.02f;     // s: τ del adelanto

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
    float abs_torque_cut = 0.0f;     // N·m del freno de pie que quita el ABS, media de los subpasos
    // Diagnóstico de los tests (2.8): s_x/(|v_x|·κ_pico) al final de cada subpaso en que el ABS
    // modula esta rueda (1 en el objetivo); 0 en los demás.
    std::array<float, kWheelSubsteps> abs_slip{};
};

// Lo que hizo el tren motriz en el último paso (telemetría y dibujo; no es estado). Pares en
// N·m, medias de los subpasos salvo que se diga otra cosa.
struct DrivetrainOutput {
    float engine_omega = 0.0f; // rad/s al final del paso
    float boost = 0.0f;        // Pa
    int gear = 1;              // −1 R, 0 N, 1..5
    int target_gear = 1;       // la del cambio en curso
    bool shifting = false;     // cambio en curso (corte y embrague abierto)
    ClutchMode clutch = ClutchMode::Launch;
    bool clutch_open = false;           // abierto por el cambio o el freno de mano
    float clutch_slip = 0.0f;           // rad/s: ω_e − ω_g al final del paso (0 sin marcha)
    float clutch_torque = 0.0f;         // par que pasa al tren (> 0 empujando el coche)
    float engine_torque = 0.0f;         // par neto del motor (combustión menos rozamiento)
    float throttle = 0.0f;              // acelerador efectivo del motor (con ralentí y cambio)
    std::array<float, 2> axle_torque{}; // de entrada a cada eje (delantero, trasero)
    float center_slip = 0.0f;           // rad/s: ω̄ delantero − ω̄ trasero al final del paso
    float viscous_torque = 0.0f;        // del viscoacoplador, del delantero al trasero (+)
    std::array<float, 2> lsd_torque{};  // |par| de bloqueo de cada autoblocante
    bool drift_hold = false;            // el automático está retrasando la subida en derrape
    int lcp_iterations = 0;             // máximo de los subpasos del paso
    int lcp_failures = 0;               // subpasos del paso sin converger
    float traction_torque_cut = 0.0f;   // par motor que quita el control de tracción
    // Diagnóstico de los tests (2.8): q del control de tracción en cada subpaso (1 en el
    // permitido; 0 sin control o sin motrices que cuenten).
    std::array<float, kWheelSubsteps> traction_q{};
};

// Ajustes del controlador (los construye make_car).
class CarControllerSettings final : public JPH::VehicleControllerSettings {
public:
    CarData car;
    // Ayudas del jugador (caja, ABS, control de tracción; la suspensión ya está en los
    // WheelSettings): cambiarlas reconstruye el mundo.
    DriverAids aids;
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
//   dinámico: giro, deformación de la huella, ángulo de giro, dirección y superficie del último
//   contacto de cada rueda, y el tren motriz (DrivetrainState: motor, turbo, marcha, cambio,
//   embrague, par de entrada de los ejes y conjunto activo del LCP); recolocar lo pone en su
//   estado inicial (salvo la superficie, que es la del suelo que pisa);
//   condición, por rueda: el radio efectivo (después, daños, presión, pinchazos); persiste al
//   recolocar y viaja en el cambio de host.
class CarController final : public JPH::VehicleController {
public:
    CarController(JPH::VehicleConstraint& constraint, const CarControllerSettings& settings);

    // Mandos del paso: SimWorld los pone antes de Update.
    void set_controls(const CarControls& controls) { controls_ = controls; }

    // Recolocar: el estado dinámico a su valor inicial (1ª, motor al ralentí, sin presión); la
    // condición no cambia.
    void reset();

    // Arranque de los tests con velocidad: giro de las ruedas a juego con `speed` (m/s) y, con
    // speed > 0, la marcha más baja cuyas rpm no pasen de las de subir a fondo, con el embrague
    // cerrado y el motor a las rpm de la caja. La partida siempre sale parada en 1ª (D4).
    void set_rolling(float speed);
    // Solo tests (CarSpawn::gear), tras set_rolling: sale en la marcha `gear` (−1..5), con el
    // motor a las rpm de la caja (al menos el ralentí; en N, al ralentí) y el embrague cerrado
    // si el motor pasa de open_rpm.
    void set_gear(int gear);

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

    int gear() const { return drive_.gear; }
    GearboxMode gearbox() const { return aids_.gearbox; }
    const DriverAids& aids() const { return aids_; }
    const DrivetrainOutput& drivetrain() const { return drive_out_; }
    // Subpasos sin converger del LCP desde que se construyó el coche (diagnóstico de los tests).
    std::int64_t lcp_failures_total() const { return lcp_failures_total_; }
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
    DriverAids aids_;
    bool explicit_wheels_ = false;
    CarControls controls_;

    // Estado dinámico (SaveState; recolocar lo reinicia). El giro está en las ruedas de Jolt.
    DrivetrainState drive_;
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
    DrivetrainOutput drive_out_;
    std::int64_t lcp_failures_total_ = 0;
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
// Dureza (aids.suspension, tarea 2.8): la media es la del archivo. La blanda y la dura
// multiplican la frecuencia, la fracción del crítico y las barras ([settings]) con el anclaje
// de la media: misma altura, actitud y recorrido libre hasta el tope en parado. La blanda, con
// una precarga del muelle de sag − sag_media; la dura (con precarga negativa, que Jolt no
// admite), con la extensión máxima más corta en sag_media − sag. Con blanda o dura, el coche
// tiene que tener [settings] (SimWorld::build lo comprueba).
JPH::Ref<JPH::VehicleConstraint> make_car_constraint(JPH::Body& body, const CarData& car,
                                                     float gravity, const DriverAids& aids,
                                                     bool explicit_wheels);

} // namespace rally::sim
