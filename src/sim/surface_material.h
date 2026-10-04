#pragma once

#include "sim/jolt_config.h"
#include "sim/surface.h"

#include <Jolt/Physics/Collision/PhysicsMaterial.h>

#include <cstdint>

namespace rally::sim {

class TestZone;

// De dónde sale el microrrelieve h(x, z) que nota la rueda sobre un material.
enum class MicroRelief : std::uint8_t {
    None,  // sin h (muros, cajas sueltas, el recinto)
    Zone,  // TestZone::micro_height: el suelo de la zona, mezclado entre áreas
    Noise, // TestZone::surface_noise de la superficie del material (losas de los tests)
};

// Material de todo lo que hay en el mundo: la superficie que usa el neumático y su h. Cada
// forma de la simulación lleva uno (el tester de las ruedas lo lee sin comprobar el tipo).
class SurfaceMaterial final : public JPH::PhysicsMaterial {
public:
    SurfaceMaterial(Surface surface, MicroRelief relief) : surface_(surface), relief_(relief) {}
    const char* GetDebugName() const override { return surface_key(surface_); }
    Surface surface() const { return surface_; }
    MicroRelief relief() const { return relief_; }

private:
    Surface surface_;
    MicroRelief relief_;
};

// Fuente de h para las ruedas: la zona de pruebas (si la hay) y la tabla de ondas de la que
// salen las bandas de ruido por superficie.
struct ReliefSource {
    const TestZone* zone = nullptr;
    const TestZone* noise = nullptr;

    double height(MicroRelief relief, Surface surface, double x, double z) const;
};

} // namespace rally::sim
