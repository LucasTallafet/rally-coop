#pragma once

// Coche de referencia y mundos pequeños para los tests del coche (tarea 2.4): losas de una
// superficie, la rampa del 15 % y un muro, sin la zona de pruebas entera.

#include "io/recording.h"
#include "sim/car_data.h"
#include "sim/check.h"
#include "sim/sim_world.h"

#include <cmath>
#include <string>

namespace rally::tests {

inline const std::string& reference_car_text() {
    static const std::string text = [] {
        std::string bytes;
        std::string error;
        RALLY_CHECK(rally::io::read_file(RALLY_TEST_DATA_DIR "/cars/reference.car", bytes, error),
                    "tests: no se puede leer tests/data/cars/reference.car");
        return bytes;
    }();
    return text;
}

// tests/data/cars/reference.car: el coche congelado de la huella y de los tests.
inline const sim::CarData& reference_car() {
    static const sim::CarData car = sim::load_car_or_abort(reference_car_text(), "reference.car");
    return car;
}

// Losa estática de `surface` con su cara de arriba en y = 0, de `length` m hacia -z desde
// z = 20 y 40 m de ancho; con `micro`, la rueda nota la banda de h de la superficie.
inline sim::BoxDesc slab(sim::Surface surface, bool micro, double length = 600.0) {
    sim::BoxDesc box;
    box.x = 0.0;
    box.y = -0.5;
    box.z = 20.0 - 0.5 * length;
    box.half_x = 20.0f;
    box.half_y = 0.5f;
    box.half_z = static_cast<float>(0.5 * length);
    box.surface = surface;
    box.micro = micro;
    return box;
}

// El coche de referencia en el origen, mirando a -z, a `speed` m/s, sobre una losa.
inline sim::WorldDesc flat_world(sim::Surface surface, bool micro = false, float speed = 0.0f) {
    sim::WorldDesc desc;
    desc.bodies = {slab(surface, micro)};
    desc.car = true;
    desc.car_spawn.speed = speed;
    desc.car_data = reference_car();
    return desc;
}

// Pendiente de la rampa de los tests (15 %).
inline constexpr double kRampGrade = 0.15;

// Rampa de asfalto de pendiente `grade` (por defecto el 15 %) y 40 m que sube hacia -z, con el
// coche parado en su mitad mirando cuesta arriba. La caja de la rampa gira alrededor de x; el
// coche sale apoyado en ella.
inline sim::WorldDesc ramp_world(double grade = kRampGrade) {
    const double angle = std::atan(grade);
    sim::WorldDesc desc;
    sim::BoxDesc ramp;
    ramp.half_x = 10.0f;
    ramp.half_y = 0.5f;
    ramp.half_z = 20.0f;
    // Un giro positivo alrededor de +x lleva -z hacia +y: la rampa sube hacia -z.
    ramp.pitch = static_cast<float>(angle);
    // Cara de arriba pasando por el origen.
    ramp.y = -0.5 * std::cos(angle);
    ramp.z = -0.5 * std::sin(angle);
    ramp.surface = sim::Surface::Asphalt;
    desc.bodies = {ramp};
    desc.car = true;
    desc.car_data = reference_car();
    return desc;
}

} // namespace rally::tests
