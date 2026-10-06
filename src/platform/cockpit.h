#pragma once

#include "platform/camera_space.h"
#include "platform/interpolation.h"
#include "sim/car_data.h"

#include <cstdint>
#include <vector>

namespace rally::platform {

// Interior provisional del coche (tarea 2.7): primitivas en código, sin Blender, con las medidas
// de [cockpit] del archivo del coche. Va fijo al coche y relativo al ojo NOMINAL de [driver]: el
// asiento y la cabeza solo mueven la cámara. Triángulos de color plano en ejes del coche
// (adelante −z, derecha +x, arriba +y; origen del coche), que platform/cockpit_draw dibuja con
// raylib. Sin raylib aquí, para probar la geometría sin ventana.

struct CockpitColor {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
};

struct CockpitTri {
    Vec3f a;
    Vec3f b;
    Vec3f c;
    CockpitColor color;
};

// Parte fija: techo, salpicadero, montantes y puertas. Se construye al cargar el coche.
std::vector<CockpitTri> build_cockpit(const sim::CarData& car);

// Ángulo del volante (rad, > 0 girando a la derecha): la dirección aplicada × el ángulo de la
// rueda interior a fondo × la relación de la dirección. A fondo, 30° × 13 = 390°.
double steering_wheel_angle(float steer_applied, const sim::CarData& car);

// Volante girado `angle` rad: aro de kWheelSegments tramos, tres radios, buje y la marca de
// arriba (para ver el giro). Añade a `out` (que se reutiliza entre fotogramas).
inline constexpr int kWheelSegments = 16;
void append_steering_wheel(std::vector<CockpitTri>& out, const sim::CarData& car, double angle);

// Pantalla del salpicadero: marcha con 7 segmentos (r, n, 1–5), barra de rpm (roja desde
// red_fraction) y barra de presión. Fracciones en [0, 1].
struct CockpitDisplay {
    int gear = 0; // −1 R, 0 N, 1..5
    float rpm_fraction = 0.0f;
    float red_fraction = 1.0f;
    float boost_fraction = 0.0f;
    bool turbo = true; // sin turbo no hay barra de presión
};
void append_display(std::vector<CockpitTri>& out, const sim::CarData& car,
                    const CockpitDisplay& display);

// Punto del interior en ejes del coche a partir de coordenadas relativas al ojo nominal
// (adelante, derecha, arriba), como en [cockpit].
Vec3d cockpit_point(const sim::CarData& car, double ahead, double right, double up);

// Proyección desde el ojo nominal mirando al frente, con el campo vertical fovy (rad) y la
// relación de aspecto: x de 0 (borde izquierdo) a 1 (derecho), y de 0 (arriba) a 1 (abajo).
// Fuera de [0, 1], fuera de la imagen. Para el test de la geometría.
struct ScreenFraction {
    double x = 0.0;
    double y = 0.0;
};
ScreenFraction project_from_eye(double ahead, double right, double up, double fovy, double aspect);

} // namespace rally::platform
