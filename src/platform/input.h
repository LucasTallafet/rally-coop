#pragma once

#include "platform/button_latch.h"
#include "platform/gamepad.h"
#include "platform/mouse_steer.h"
#include "sim/sim_input.h"

namespace rally::platform {

// Entrada del conductor: teclado y mando activo, mezclados por eje (gana el de mayor
// magnitud; combine_inputs). Se lee una vez por fotograma y se aplica igual a todos sus
// pasos; solo enteros llegan a la simulación, que suaviza la dirección y el acelerador
// digital por paso (sim/input_smoothing.h).
// Teclas y botones del mando: la tabla de platform/keys (bloque Conducir).
class DriverInput {
public:
    // step_count: pasos de física de este fotograma. Los botones de pulsación de los
    // fotogramas sin pasos se guardan para el siguiente que los tenga (ButtonLatch).
    sim::SimInput read(int step_count);
    Gamepads& gamepads() { return gamepads_; }
    const Gamepads& gamepads() const { return gamepads_; }

    // Dirección con ratón (tarea 2.5b), antes de read() en cada fotograma: si el jugador la ha
    // elegido, su sensibilidad y si se puede capturar el cursor ahora (no en pausa). Con el ratón
    // elegido, la ventana con el foco y la captura permitida, el cursor queda capturado y su
    // desplazamiento horizontal mueve el volante; al soltarlo (pausa, foco perdido, teclado), el
    // volante vuelve al centro. Al capturar se descarta el primer desplazamiento (el salto del
    // cursor). X o el botón central recentran.
    void set_mouse(bool enabled, int full_lock_px, bool capture_allowed);
    bool mouse_enabled() const { return mouse_enabled_; }
    bool mouse_captured() const { return mouse_captured_; }
    const MouseSteer& mouse() const { return mouse_; }

private:
    void update_mouse();

    Gamepads gamepads_;
    ButtonLatch latch_;
    MouseSteer mouse_;
    bool mouse_enabled_ = false;
    bool mouse_capture_allowed_ = false;
    bool mouse_captured_ = false;
    bool skip_mouse_delta_ = false;
    int mouse_full_lock_px_ = 400;
};

} // namespace rally::platform
