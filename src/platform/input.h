#pragma once

#include "platform/button_latch.h"
#include "sim/sim_input.h"

namespace rally::platform {

// Traduce el teclado a la entrada de la simulación (enteros; nada del dispositivo pasa como
// float). Se lee una vez por fotograma y se aplica igual a todos sus pasos.
// W/flecha arriba empuja hacia -z (lejos de la cámara de la demo), S/abajo hacia +z,
// A/izquierda hacia -x y D/derecha hacia +x; Espacio salta.
class KeyboardInput {
public:
    // step_count: pasos de física de este fotograma. Los botones de los fotogramas sin
    // pasos se guardan para el siguiente que los tenga (ButtonLatch).
    sim::SimInput read(int step_count);

private:
    ButtonLatch latch_;
};

} // namespace rally::platform
