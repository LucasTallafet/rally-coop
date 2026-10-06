#pragma once

#include "sim/car_data.h"

#include <array>
#include <cstdint>

namespace rally::sim {

// Tren motriz de la 1.0 (tarea 2.6): motor con curva de par y turbo, embrague automático,
// caja de 5 marchas (automática o secuencial, regla D4), diferencial central de reparto fijo
// con viscoacoplador y autoblocantes de rampa en cada eje. Funciones puras: el estado vive en
// DrivetrainState, que guarda el controlador (sim/car_controller.h) en su SaveState.
//
// Resolución acoplada (en cada subpaso de rueda, 960 Hz): Euler implícito del sistema
// linealizado motor + 4 ruedas, con los acoplamientos de fricción como un LCP de caja.
//   Incógnitas x = (ω_e, ω_FL, ω_FR, ω_RL, ω_RR). Inercias efectivas M = diag(I_e + h·k_e,
//   I_w + h·r·∂F_x/∂ω): la misma linealización que el neumático ya usa para el giro de la
//   rueda, extendida al motor (k_e = −∂T_e/∂ω_e ≥ 0). Predicción sin acoplamientos:
//   x* = x + h·M⁻¹·τ₀.
//   Los diferenciales abiertos son la elección de coordenadas: la caja gira a
//   ω_g = G·Σ s_i·ω_i con s = (a/2, a/2, b/2, b/2), a = parte del par al eje delantero.
//   Filas (Jacobianas J_k; la fila k pone el impulso Λ_k·J_kᵀ):
//     embrague ω_e − ω_g (Coulomb, |Λ| ≤ h·C); autoblocantes ω_L − ω_R de cada eje (Coulomb,
//     |Λ| ≤ h·(precarga + k·|par de entrada del eje|)); freno, freno de mano y rodadura ω_i
//     (Coulomb, |Λ| ≤ h·T_stop); viscoacoplador ω̄_del − ω̄_tras (amortiguador implícito,
//     Λ = −h·c·J·x').
//   Con A = J·M⁻¹·Jᵀ (más 1/(h·c) en la diagonal del viscoacoplador) y b = J·x*: las filas
//   pegadas cumplen A·Λ = −b; las que deslizan llevan Λ = ±h·cap. Conjunto activo
//   determinista (como mucho kMaxLcpIterations) con Cholesky de tamaño fijo; si no converge,
//   cada Λ se recorta a su caja y se usa así (se cuenta). x' = x* + M⁻¹·Jᵀ·Λ.
// Estabilidad: con el conjunto activo fijo es Euler implícito de un sistema lineal con
// amortiguamiento semidefinido positivo; las filas pegadas son restricciones exactas (el motor
// y lo que arrastra se resuelven como una sola inercia, por solución directa: la diferencia de
// escala motor/rueda no frena la convergencia); las que deslizan llevan un par de Coulomb que
// nunca invierte la velocidad relativa (complementariedad v·Λ ≤ 0), como el freno de la 2.4.
// El viscoacoplador solo: Δω' = Δω*/(1 + h·c·(1/I_f + 1/I_r)), factor en (0, 1] para todo
// c ≥ 0: con c rígido Δω tiende a 0 sin cambiar de signo.
//
// Referencia de estudio: el SDK de vehículos de PhysX (BSD-3) integra motor y ruedas
// acoplados por el embrague de forma implícita, como un sistema lineal N×N resuelto en cada
// subpaso (PhysX 4.1, physx/source/physxvehicle/src/PxVehicleUpdate.cpp:
// solveDrive4WInternaDynamicsEnginePlusDrivenWheels, que monta la matriz (4+1)×(4+1) con el
// par del embrague tc = K·(G·Σ α_i·ω_i − ω_eng) en t+dt y la resuelve con MatrixNN y
// MatrixNNLUSolver). De ahí sale "linealizar e integrar implícito el sistema acoplado".
// Diferencias: allí el embrague es una rigidez (PxVehicleClutchData::mStrength) y los
// diferenciales reparten con límites de velocidades (mCentreBias, mFrontBias, mRearBias en
// computeDiffTorqueRatios); aquí el embrague y los
// autoblocantes son pares de Coulomb y el central lleva un viscoacoplador.
// Sin modelar: la inercia de la caja y de los palieres (dentro de la de la rueda); los
// diferenciales no tienen masa.

// Grados de libertad del sistema y filas del LCP.
inline constexpr int kDriveDofs = 5; // motor y 4 ruedas
inline constexpr int kDriveRows = 8;
enum DriveRow : int {
    kRowClutch = 0,
    kRowViscous = 1,
    kRowLsdFront = 2,
    kRowLsdRear = 3,
    kRowStop = 4, // 4..7: freno y rodadura de cada rueda (orden de WheelIndex)
};
// Iteraciones del conjunto activo en un subpaso.
inline constexpr int kMaxLcpIterations = 8;

// Estado de una fila en el conjunto activo.
enum class RowState : std::uint8_t { Inactive = 0, Stick = 1, SlipPositive = 2, SlipNegative = 3 };

// Modo de la caja, elegido por el jugador (DriverAids; va en la grabación).
enum class GearboxMode : std::uint8_t { Automatic = 0, Sequential = 1 };

// Embrague automático. Arranque: capacidad según las rpm del motor (patina; con el motor al
// ralentí, 0: ni avance lento ni calado). Cerrado: capacidad máxima; vuelve a arranque por
// debajo de open_rpm. Además, abierto (capacidad 0) durante el cambio y con el freno de mano.
enum class ClutchMode : std::uint8_t { Launch = 0, Closed = 1 };

// Marchas: −1 = R, 0 = N, 1..5.
inline constexpr int kReverseGear = -1;
inline constexpr int kNeutralGear = 0;

// Por debajo de esta velocidad (2 km/h) el coche está parado para la caja (D4).
inline constexpr float kGearStopSpeed = 2.0f / 3.6f;

// Estado del tren motriz (SaveState del controlador). Construir y recolocar: drivetrain_reset.
struct DrivetrainState {
    float engine_omega = 0.0f;    // rad/s
    float boost = 0.0f;           // Pa sobre la atmosférica
    std::int8_t gear = 1;         // marcha engranada (−1..5)
    std::int8_t target_gear = 1;  // la del cambio en curso
    std::int16_t shift_steps = 0; // pasos que quedan del cambio (0: sin cambio)
    std::int16_t drift_steps = 0; // pasos que lleva retrasada la subida en derrape
    ClutchMode clutch = ClutchMode::Launch;
    std::array<float, 2> axle_torque{};      // N·m de entrada a cada eje en el subpaso anterior
    std::array<RowState, kDriveRows> rows{}; // conjunto activo del subpaso anterior
};

// Estado inicial (D4): 1ª, motor al ralentí, sin presión, sin cambio, embrague en arranque.
void drivetrain_reset(const CarData& car, DrivetrainState& state);

// Relación total (con el grupo) de una marcha; 0 en N, negativa en R.
float gear_ratio(const CarData& car, int gear);

// --- Motor ---------------------------------------------------------------------------------

// Par sin presión de la tabla, por interpolación lineal (constante fuera de ella), sin
// torque_scale.
float engine_curve(const CarData& car, float omega);
// Par de combustión a plena carga: torque_scale · T_atm(ω) · (1 + g·p).
float engine_full_torque(const CarData& car, float omega, float boost);
// Corte del limitador: 1 por debajo de limiter − ramp, 0 en limiter, lineal entre medias.
float limiter_factor(const CarData& car, float omega);
// Regulador de ralentí: el acelerador mínimo que sostiene idle_rpm sin carga (proporcional).
float idle_throttle(const CarData& car, float omega);
// Par neto en el cigüeñal con el acelerador efectivo `throttle` (ya con el ralentí):
// θ·T_plena·limitador − (1 − θ)·rozamiento·ω. A fondo da la tabla (la curva es la del motor
// entregando); con el acelerador cerrado, el freno motor.
float engine_torque(const CarData& car, float omega, float throttle, float boost);

// --- Turbo ---------------------------------------------------------------------------------

// Presión objetivo: boost_max · θ · rampa(ω; umbral → plena).
float boost_target(const CarData& car, float omega, float throttle);
// Un paso dt del primer orden exacto hacia el objetivo: subida con τ_sub·(ω_ref/ω) (crece a
// bajas rpm), bajada con τ_baj; nunca pasa de boost_max (válvula de descarga).
float turbo_step(const CarData& car, float boost, float omega, float throttle, float dt);

// --- Embrague ------------------------------------------------------------------------------

// Capacidad (N·m) del embrague en su modo, sin contar el cambio ni el freno de mano.
float clutch_capacity(const CarData& car, ClutchMode mode, float engine_omega, float throttle);

// --- Caja ----------------------------------------------------------------------------------

struct GearboxInput {
    GearboxMode mode = GearboxMode::Automatic;
    bool shift_up = false;
    bool shift_down = false;
    float throttle = 0.0f;
    bool stopped = false;     // |v| < kGearStopSpeed
    bool drifting = false;    // |β| sobre drift_angle (automático)
    float drive_speed = 0.0f; // rad/s: Σ s_i·ω_i, el giro a la entrada de los diferenciales
};

// Un paso de la caja (240 Hz, antes del paso de Jolt): avanza el cambio en curso, atiende los
// botones con la regla D4 y, en automático, cambia por umbrales con histéresis y espera en
// derrape.
void update_gearbox(const CarData& car, const GearboxInput& in, DrivetrainState& state);

// Acelerador del motor durante un cambio: apunta a las rpm de la marcha nueva. Al subir da 0
// (corte); al reducir, el golpe de gas (punta-tacón automático).
float shift_throttle(const CarData& car, const DrivetrainState& state, float drive_speed);

// --- LCP del tren motriz -------------------------------------------------------------------

struct DriveSystem {
    std::array<float, kDriveDofs> inv_mass{}; // 1 / inercia efectiva
    std::array<float, kDriveDofs> velocity{}; // x*: predicción sin acoplamientos
    std::array<std::array<float, kDriveDofs>, kDriveRows> jacobian{};
    std::array<float, kDriveRows> cap{};  // impulso máximo (h·par) de las filas de Coulomb
    std::array<float, kDriveRows> soft{}; // 1/(h·c) de las amortiguadas (0: de Coulomb)
    std::array<bool, kDriveRows> active{};
};

struct DriveSolution {
    std::array<float, kDriveDofs> velocity{};
    std::array<float, kDriveRows> impulse{};
    int iterations = 0;
    bool converged = true;
};

// Resuelve el LCP de caja. `rows` entra con el conjunto activo inicial (el del subpaso
// anterior) y sale con el final. Determinista: número fijo máximo de iteraciones y sin
// reservas.
DriveSolution solve_drive(const DriveSystem& system, std::array<RowState, kDriveRows>& rows);

} // namespace rally::sim
