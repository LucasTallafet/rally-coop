#pragma once

#include "sim/car_controller.h"
#include "sim/car_data.h"
#include "sim/input_smoothing.h"
#include "sim/lap_timer.h"
#include "sim/sim_input.h"
#include "sim/steer_assist.h"
#include "sim/surface.h"
#include "sim/telemetry.h"
#include "sim/test_zone.h"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rally::sim {

inline constexpr int kStepsPerSecond = 240;
inline constexpr float kStepSeconds = 1.0f / kStepsPerSecond;

enum class BodyMotion : std::uint8_t { Static, Dynamic };

// Una caja del mundo. Centro en doble precisión (origen en el centro del mundo), medidas
// en metros. Densidad de Jolt por defecto (1000 kg/m³). `pitch` la gira alrededor de x
// (rampas de los mundos de test); `surface` es la superficie que nota el neumático y, con
// `micro`, la rueda nota además la banda de ruido h de esa superficie (losas de los tests).
struct BoxDesc {
    BodyMotion motion = BodyMotion::Static;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    float half_x = 0.5f;
    float half_y = 0.5f;
    float half_z = 0.5f;
    float pitch = 0.0f;
    Surface surface = Surface::Asphalt;
    bool micro = false;
};

// CarSpawn::gear por defecto: la marcha la da la velocidad de salida.
inline constexpr std::int8_t kSpawnGearBySpeed = 127;

// Salida del coche: su origen (en el suelo, entre los ejes), rumbo (giro alrededor de +y; 0
// mira a -z) y velocidad hacia delante al construir (los tests la usan; el juego sale parado).
// Recolocar fuera de la zona lo devuelve aquí, parado.
struct CarSpawn {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    float yaw = 0.0f;
    float speed = 0.0f;
    // Solo tests: marcha de salida (−1 R, 0 N, 1..5). Por defecto, kSpawnGearBySpeed: 1ª parado
    // y, con speed > 0, la que da la velocidad (CarController::set_rolling). La partida siempre
    // sale parada en 1ª (D4).
    std::int8_t gear = kSpawnGearBySpeed;
};

// Descripción del mundo. Primero el suelo y los muros de la zona de pruebas (si la hay),
// después los cuerpos en el orden de `bodies` y al final el coche: los BodyID dependen solo
// de la descripción.
struct WorldDesc {
    std::vector<BoxDesc> bodies;
    // Coche que mueve SimInput (sim/car_controller.h). Sus datos no van en la descripción
    // serializada: la grabación lleva el texto del .car aparte.
    bool car = false;
    CarSpawn car_spawn;
    CarData car_data;
    // Ayudas del jugador que cambian la simulación (sim/steer_assist.h). Van en la descripción
    // serializada desde el formato 4 de la grabación.
    DriverAids aids;
    // Diagnóstico de los tests: ruedas con integración explícita. No se graba.
    bool explicit_wheels = false;
    // Zona de pruebas de la fase 2 (sim/test_zone.h): suelo de Jolt con una superficie por
    // área, recolocación al punto más cercano y recorrido cronometrado por la pista ciega.
    bool test_zone = false;
    Surface blind_surface = Surface::Gravel; // superficie de la pista ciega
};

// Lo que hay bajo (x, z) en el suelo de la zona, según Jolt (un rayo vertical).
struct GroundProbe {
    bool hit = false;
    double y = 0.0;
    Surface surface = Surface::Dirt;
};

// Posición en doble y orientación (cuaternión x, y, z, w) de un cuerpo tras el último paso.
struct BodyPose {
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    double qx = 0.0;
    double qy = 0.0;
    double qz = 0.0;
    double qw = 1.0;
};

// Velocidad lineal (m/s) y angular (rad/s) de un cuerpo tras el último paso.
struct BodyVelocity {
    float vx = 0.0f;
    float vy = 0.0f;
    float vz = 0.0f;
    float wx = 0.0f;
    float wy = 0.0f;
    float wz = 0.0f;
};

// Mundo de Jolt de la simulación: posee el PhysicsSystem, su asignador temporal y el
// sistema de trabajos de un hilo. Ciclo de vida fijo:
//   1. build(desc): crea todos los cuerpos en el orden de la descripción y los añade en un
//      solo lote (AddBodiesPrepare / AddBodiesFinalize), y la VehicleConstraint del coche
//      como restricción y step listener. Una sola vez: cambiar de coche reconstruye el mundo.
//   2. seal(): optimiza la broad phase. Desde aquí no se añade ni se quita nada.
//   3. step(input): un paso de 1/240 s, sin reservas de memoria propias (Jolt hace 2 en
//      su broad phase; ver docs/progreso.md).
// Saltarse el orden aborta (RALLY_CHECK), también en Release.
// Además del SaveState de Jolt, la simulación guarda ControlState (mandos aplicados y
// botones del paso anterior); en el cambio de host (fase 5) viaja con el estado del juego.
// Necesita un JoltRuntime vivo durante toda su vida.
class SimWorld {
public:
    SimWorld();
    ~SimWorld();
    SimWorld(const SimWorld&) = delete;
    SimWorld& operator=(const SimWorld&) = delete;

    void build(const WorldDesc& desc);
    void seal();
    bool is_sealed() const;

    // Un paso. Primero los mandos aplicados siguen a sus objetivos con los limitadores
    // (ControlState) y pasan al controlador del coche (sim/car_controller.h), que pone la
    // dirección, el selector, el neumático, los frenos, el ancla de parada y el aire dentro del
    // paso de Jolt. Al pulsar recolocar, los ejes aplicados vuelven a 0 antes de aplicarlos y,
    // tras integrar, el coche vuelve quieto a su salida o, en la zona de pruebas, al punto de
    // recolocación más cercano, apoyado y con su orientación, con el estado del controlador a
    // cero: al volver de step() ya está ahí. En la zona, tras cada paso el cronómetro mira si
    // el coche ha cruzado la línea de meta.
    void step(const SimInput& input);

    // Mandos aplicados y pulsaciones del último paso.
    const ControlState& controls() const;
    // Contexto de la dirección del último paso (velocidad de referencia y ventana de la ayuda) y
    // objetivo de la dirección ya recortado. Salidas de diagnóstico: no son estado.
    const SteerContext& steer_context() const;
    std::int32_t steer_aided() const;

    // Captura de la telemetría del coche (sim/telemetry.h), apagada al construir. Encendida,
    // step() la rellena al final del paso; solo lee y no reserva memoria: no cambia la
    // simulación (misma huella). Se puede encender en cualquier momento: no usa pasos
    // anteriores (la aceleración sale de la velocidad al empezar el mismo paso).
    void set_telemetry(bool enabled);
    bool telemetry_enabled() const;
    // Telemetría del último paso con la captura encendida (a cero sin coche).
    const CarTelemetry& telemetry() const;

    // Impacto del coche en el último paso, en m/s: el cambio de velocidad lineal que no
    // explican la gravedad ni las fuerzas propias del controlador (neumático, ancla y aire).
    // Lo causan la suspensión y los contactos de la carrocería: apoyado son unos 0,04 m/s (la
    // gravedad de un paso que anula la suspensión); al aterrizar o chocar, varios m/s. Solo se
    // lee: no cambia la simulación. 0 sin coche.
    float last_impact() const;

    // Coche (si la descripción lo tiene).
    bool has_car() const;
    BodyPose car_pose() const;
    BodyVelocity car_velocity() const;
    float car_mass() const;
    // Posición del centro de masas del coche en el mundo.
    BodyPose car_center_of_mass() const;
    // Controlador del coche: selector, salidas por rueda y fuerzas del último paso.
    const CarController& car_controller() const;
    // Rueda `index` (sim::WheelIndex) en ejes del coche: anclaje de la suspensión, longitud de
    // la suspensión, ángulo de dirección (positivo a la izquierda, como en Jolt) y ángulo de
    // giro en [0, 2π). Para el dibujo.
    struct WheelPose {
        float attach_x = 0.0f;
        float attach_y = 0.0f;
        float attach_z = 0.0f;
        float suspension_length = 0.0f;
        float steer = 0.0f;
        float rotation = 0.0f;
        float radius = 0.0f;
        float width = 0.0f;
    };
    WheelPose car_wheel(int index) const;
    // Condición de la rueda `index`: su radio efectivo (al construir, el del archivo).
    // Recolocar no lo cambia. Hasta los daños (fase 4) solo lo usan los tests; fuera del paso.
    void set_wheel_radius(int index, float radius);
    // Solo los tests, fuera del paso: pone la velocidad lineal y angular del coche (la zona
    // muerta del giro de Jolt, sim/car_controller.cpp).
    void set_car_velocity(const BodyVelocity& velocity);

    int body_count() const;
    // Índice y número de secuencia del BodyID de Jolt del cuerpo `index` de la descripción.
    std::uint32_t body_id(int index) const;
    BodyPose pose(int index) const;
    BodyVelocity velocity(int index) const;

    // Zona de pruebas del mundo; nullptr si no tiene.
    const TestZone* zone() const;
    // Cronómetro de la pista ciega; nullptr sin zona.
    const LapState* lap() const;
    // Hash de las alturas y superficies que entraron en el suelo de Jolt (0 sin zona) y
    // memoria que ocupa el HeightFieldShape.
    std::uint64_t ground_hash() const;
    // Hash de la zona entera (0 sin zona): ground_hash seguido de las ondas de h de cada
    // superficie, los parámetros de las ondulaciones de la grava y los muros (sim::zone_hash).
    // Va en la cabecera de las grabaciones: si cambia, la repetición no compara el hash final.
    std::uint64_t zone_hash() const;
    std::size_t ground_bytes() const;
    // Rayo vertical contra el suelo de la zona. Solo lee; reserva memoria (fuera del paso).
    GroundProbe probe_ground(double x, double z) const;

    // SaveState completo del PhysicsSystem (con el estado del controlador del coche).
    std::string save_state() const;
    // Restaura un save_state() de un mundo construido con la misma descripción, y sus mandos
    // aplicados. El cronómetro de la zona no se restaura (cambio de host, fase 5).
    void restore_state(const std::string& state, const ControlState& controls);

    // Hash del estado completo de la simulación: FNV-1a de save_state() seguido de
    // ControlState y, en la zona, del cronómetro (campo a campo, en little-endian). Es el hash
    // final de las grabaciones y la huella de los tests. Reserva memoria: no se llama dentro del
    // paso.
    std::uint64_t state_hash() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rally::sim
