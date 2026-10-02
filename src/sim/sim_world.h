#pragma once

#include "sim/input_smoothing.h"
#include "sim/sim_input.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rally::sim {

inline constexpr int kStepsPerSecond = 240;
inline constexpr float kStepSeconds = 1.0f / kStepsPerSecond;

enum class BodyMotion : std::uint8_t { Static, Dynamic };

// Una caja del mundo. Centro en doble precisión (origen en el centro del mundo), medidas
// en metros. Densidad de Jolt por defecto (1000 kg/m³).
struct BoxDesc {
    BodyMotion motion = BodyMotion::Static;
    double x = 0.0;
    double y = 0.0;
    double z = 0.0;
    float half_x = 0.5f;
    float half_y = 0.5f;
    float half_z = 0.5f;
};

// Descripción del mundo. Los cuerpos se crean en el orden de `bodies`: los BodyID dependen
// solo de la descripción.
struct WorldDesc {
    std::vector<BoxDesc> bodies;
    int controlled = -1; // índice del cuerpo dinámico que mueve SimInput; -1 si ninguno
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
//      solo lote (AddBodiesPrepare / AddBodiesFinalize). Una sola vez.
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

    // La entrada se aplica antes de integrar, al cuerpo controlado. Primero los mandos
    // aplicados siguen a sus objetivos con los limitadores (ControlState); con ellos,
    // empuje en su dirección adelante (-z local) proporcional a acelerador − freno (el
    // freno lo lleva hacia atrás), velocidad de giro alrededor de Y proporcional a la
    // dirección e impulso de salto al pulsar. El cuerpo controlado solo gira alrededor de Y.
    // Al pulsar recolocar, los ejes aplicados vuelven a 0 antes de aplicarlos y, tras
    // integrar, el cuerpo vuelve a su salida (la posición de la descripción), mirando al
    // frente y quieto: al volver de step() ya está ahí. Las marchas y el freno de mano solo
    // se transportan (ControlState) hasta el coche (tareas 2.4 y 2.6).
    void step(const SimInput& input);

    // Mandos aplicados y pulsaciones del último paso.
    const ControlState& controls() const;

    // Impacto del cuerpo controlado en el último paso, en m/s: el cambio de velocidad lineal
    // que no explican la gravedad, el amortiguamiento ni la entrada (empuje y salto). Lo
    // causan los contactos: apoyado son unos 0,04 m/s (la gravedad de un paso que anula el
    // suelo) y deslizando, menos; al aterrizar o chocar, varios m/s. Solo se lee: no
    // cambia la simulación. 0 si no hay cuerpo controlado.
    float last_impact() const;

    int body_count() const;
    // Índice y número de secuencia del BodyID de Jolt del cuerpo `index` de la descripción.
    std::uint32_t body_id(int index) const;
    BodyPose pose(int index) const;
    BodyVelocity velocity(int index) const;

    // SaveState completo del PhysicsSystem.
    std::string save_state() const;

    // Hash del estado completo de la simulación: FNV-1a de save_state() seguido de
    // ControlState (campo a campo, en little-endian). Es el hash final de las grabaciones y
    // la huella de los tests. Reserva memoria: no se llama dentro del paso.
    std::uint64_t state_hash() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rally::sim
