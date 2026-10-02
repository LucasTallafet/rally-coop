#pragma once

#include "platform/input_shaping.h"

#include <cstdint>

namespace rally::platform {

// Mandos del conductor. raylib (backend SDL3) los abre y los cierra al conectar y
// desconectar; aquí se leen directamente con SDL3, para que ejes, botón de salto, vibración,
// nombre y tipo salgan siempre del mismo mando (GetGamepadName de raylib busca en una lista
// que, tras reconectar mandos, deja de coincidir con sus huecos).
// Botones por posición, nunca por letra: freno de mano = el de abajo de la cara derecha
// (A en Xbox, ✕ en PlayStation, B en Nintendo), recolocar = el de arriba (Y, △, X), bajar
// marcha = hombro izquierdo y subir = hombro derecho. Gatillo derecho = acelerador e
// izquierdo = freno.
class Gamepads {
public:
    // Lee los mandos conectados y devuelve el estado del activo (PadTracker).
    PadState read();

    // Vibra el mando activo (nada si no hay o si la duración es 0).
    void rumble(const Rumble& rumble);

    int connected() const { return connected_; }
    // Del mando activo: nombre (SDL_GetGamepadName) y tipo (SDL_GetGamepadStringForType);
    // nullptr si no hay mando activo.
    const char* active_name() const;
    const char* active_type() const;
    bool active_throttle_digital() const {
        return tracker_.right_trigger_digital(tracker_.active_id());
    }
    bool active_brake_digital() const {
        return tracker_.left_trigger_digital(tracker_.active_id());
    }

private:
    PadTracker tracker_;
    int connected_ = 0;
};

} // namespace rally::platform
