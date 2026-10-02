#pragma once

#include "platform/button_latch.h"
#include "platform/gamepad.h"
#include "sim/sim_input.h"

namespace rally::platform {

// Entrada del conductor: teclado y mando activo, mezclados por eje (gana el de mayor
// magnitud; combine_inputs). Se lee una vez por fotograma y se aplica igual a todos sus
// pasos; solo enteros llegan a la simulación, que suaviza la dirección y el acelerador
// digital por paso (sim/input_smoothing.h).
// Teclado: W/flecha arriba acelera hacia delante y S/abajo hacia atrás; A/izquierda gira a
// la izquierda y D/derecha a la derecha; Espacio salta.
// Mando: stick izquierdo (solo el eje X) gira; gatillo derecho acelera y el izquierdo frena
// o da marcha atrás; el botón de abajo de la cara derecha salta.
class DriverInput {
public:
    // step_count: pasos de física de este fotograma. Los botones de pulsación de los
    // fotogramas sin pasos se guardan para el siguiente que los tenga (ButtonLatch).
    sim::SimInput read(int step_count);

    Gamepads& gamepads() { return gamepads_; }
    const Gamepads& gamepads() const { return gamepads_; }

private:
    Gamepads gamepads_;
    ButtonLatch latch_;
};

} // namespace rally::platform
