#pragma once

#include "net/state_buffer.h"
#include "platform/cockpit.h"
#include "platform/interpolation.h"
#include "sim/car_data.h"

namespace rally::platform {

// La vista del copiloto desde el asiento derecho (lote 3, arreglo copiloto-asiento). Sin raylib.

// Ojo del copiloto: el del conductor del archivo del coche (sin los desplazamientos del asiento del
// jugador) reflejado en el eje lateral del coche: la x en ejes del coche cambia de signo.
Vec3d copilot_eye(const sim::CarData& car);

// Giro de la cabeza hacia el salpicadero mientras se mantiene la tecla: 25° a la izquierda. Desde
// el asiento derecho, la pantalla queda de 47,7° a 53,8° a la izquierda y el aro del volante de
// 51,5° a 63,8°; con 25° y el campo de ±42,8° (55° vertical, 16:9) entran en la imagen.
inline constexpr double kLookDashYaw = 25.0 * 3.14159265358979323846 / 180.0;
// Lo que tarda la cabeza en llegar (o volver): 0,18 s.
inline constexpr double kLookSeconds = 0.18;

// Avance de la mirada t en [0, 1] en dt_s segundos hacia 1 (tecla mantenida) o hacia 0.
double look_step(double t, bool held, double dt_s);
// Giro (rad, > 0 hacia la izquierda) con arranque y parada suaves.
double look_yaw(double t);

// Volante y pantalla del salpicadero con el estado de red a la hora mostrada: el ángulo del
// volante es la dirección recibida × el ángulo de la rueda × la relación de la dirección, la
// misma cuenta que la del conductor. La marcha se acota a la caja del coche (un paquete malo no
// indexa fuera de la pantalla).
struct CopilotCockpit {
    double wheel_angle = 0.0; // rad, > 0 a la derecha
    CockpitDisplay display;
};
CopilotCockpit copilot_cockpit(const net::SampledState& state, const sim::CarData& car);

} // namespace rally::platform
