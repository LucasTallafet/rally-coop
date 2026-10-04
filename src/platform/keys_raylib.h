#pragma once

#include "platform/keys.h"

#include <cstdint>

namespace rally::platform {

// Lectura del teclado de raylib con la tabla de platform/keys. `context`: el contexto activo
// (kContextZone, kContextBlind o kContextArena); una acción que no vale en él no se dispara.

// KeyboardKey de raylib de la tecla.
int raylib_key(Key key);

// Pulsada en este fotograma (sin Alt, si la tecla de la tabla no lo lleva).
bool action_pressed(Action action, std::uint8_t context = kContextAll);
// Pulsada o repetida por el teclado al mantenerla (asiento).
bool action_pressed_repeat(Action action, std::uint8_t context = kContextAll);
// Mantenida (conducir), sin mirar Alt.
bool action_down(Action action);
// Mantenida o pulsada y soltada dentro del fotograma (botones que actúan al pulsar).
bool action_down_or_pressed(Action action);

} // namespace rally::platform
