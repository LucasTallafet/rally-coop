#pragma once

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

// Mundo de Jolt de la simulación: posee el PhysicsSystem, su asignador temporal y el
// sistema de trabajos de un hilo. Ciclo de vida fijo:
//   1. build(desc): crea todos los cuerpos en el orden de la descripción y los añade en un
//      solo lote (AddBodiesPrepare / AddBodiesFinalize). Una sola vez.
//   2. seal(): optimiza la broad phase. Desde aquí no se añade ni se quita nada.
//   3. step(input): un paso de 1/240 s, sin reservas de memoria propias (Jolt hace 2 en
//      su broad phase; ver docs/progreso.md).
// Saltarse el orden aborta (RALLY_CHECK), también en Release.
// Además del SaveState de Jolt, la simulación guarda los botones del paso anterior
// (detección de pulsaciones); en el cambio de host (fase 5) viajan con el estado del juego.
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

    // La entrada se aplica antes de integrar: empuje horizontal del cuerpo controlado
    // (aceleración constante; en diagonal no suma más) e impulso de salto al pulsar.
    void step(const SimInput& input);

    int body_count() const;
    // Índice y número de secuencia del BodyID de Jolt del cuerpo `index` de la descripción.
    std::uint32_t body_id(int index) const;
    BodyPose pose(int index) const;

    // SaveState completo del PhysicsSystem.
    std::string save_state() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rally::sim
