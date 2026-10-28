#pragma once

#include "net/state_buffer.h"
#include "platform/cockpit.h"
#include "platform/interpolation.h"
#include "sim/car_data.h"

namespace rally::platform {

// La vista del copiloto desde el asiento derecho (lote 3, arreglo copiloto-asiento). Sin raylib.
// La mirada (stick o ratón) es la de platform/head_look, igual que la del conductor.

// Ojo del copiloto: el del conductor del archivo del coche (sin los desplazamientos del asiento del
// jugador) reflejado en el eje lateral del coche: la x en ejes del coche cambia de signo.
Vec3d copilot_eye(const sim::CarData& car);

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
