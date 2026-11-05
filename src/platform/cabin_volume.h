#pragma once

#include "platform/interpolation.h"
#include "sim/car_data.h"

namespace rally::platform {

// Volumen seguro de la cabina (lote 5, cabina-volumen): el ojo final del conductor o del copiloto
// (asiento + cuello + F9) nunca queda a menos de kCabinMargin de la puerta, el techo, el final del
// techo por detrás ni el plano del parabrisas. Es presentación: nada llega a la simulación ni a la
// grabación. Sin raylib; se prueba sin ventana.
//
// El volumen sale de [cockpit] del archivo del coche (sin claves nuevas), en ejes del coche (x
// derecha, y arriba, −z hacia delante), con cada plano metido kCabinMargin hacia dentro. Hacia
// abajo y hacia el centro del coche no hay límite (el asiento ya lo limita).
inline constexpr double kCabinMargin = 0.08; // m

struct CabinVolume {
    // Puerta (x): el conductor tiene x_min (a su izquierda) y x_max infinito; el copiloto es el
    // reflejo en x.
    double x_min = 0.0;
    double x_max = 0.0;
    // Techo (inclinado, el que dibuja platform/cockpit): dentro cuando ry·y + rz·z ≤ rc, con (ry,
    // rz) unitario hacia arriba.
    double ry = 0.0;
    double rz = 0.0;
    double rc = 0.0;
    double z_max = 0.0; // final del techo por detrás (z crece hacia atrás)
    // Parabrisas, dentro cuando wy·y + wz·z ≤ wc. (wy, wz) es unitario y apunta hacia delante y
    // arriba.
    double wy = 0.0;
    double wz = 0.0;
    double wc = 0.0;
};

// El volumen del conductor (copilot = false) o del copiloto (reflejado en x).
CabinVolume cabin_volume(const sim::CarData& car, bool copilot);

// El punto del volumen más cercano a `eye` (ejes del coche). Si ya está dentro, `eye` sin tocar
// (bit a bit). Continuo: moverse d en la entrada mueve la salida d como mucho.
Vec3d clamp_to_cabin(const CabinVolume& volume, const Vec3d& eye);

} // namespace rally::platform
