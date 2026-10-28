#pragma once

#include "platform/keys.h"

#include <cstdint>

namespace rally::platform {

// Lectura del teclado de raylib con la tabla de platform/keys. `context`: el contexto activo
// (kContextZone, kContextBlind o kContextArena en el juego; kCopilotKeyContext en la ventana del
// copiloto); una acción que no vale en él no se dispara (action_valid_in). Sin valor por
// defecto: quien llama dice en qué contexto lee, y así una tecla de otra ventana no queda muda.

// KeyboardKey de raylib de la tecla.
int raylib_key(Key key);

// Pulsada en este fotograma (sin Alt, si la tecla de la tabla no lo lleva).
bool action_pressed(ActionRef action, std::uint8_t context);
// Pulsada o repetida por el teclado al mantenerla (asiento).
bool action_pressed_repeat(ActionRef action, std::uint8_t context);
// Mantenida (conducir), sin mirar Alt.
bool action_down(ActionRef action, std::uint8_t context);
// Mantenida o pulsada y soltada dentro del fotograma (botones que actúan al pulsar).
bool action_down_or_pressed(ActionRef action, std::uint8_t context);

} // namespace rally::platform
