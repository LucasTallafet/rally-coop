#pragma once

#include <cstdint>

namespace rally::platform {

// Retiene los botones de los fotogramas sin pasos de física. Sin sincronización vertical
// (más de 240 fps) hay fotogramas con 0 pasos, y una pulsación que empieza y acaba en uno
// de ellos no llegaría a la simulación. Los botones se acumulan (OR) hasta el primer
// fotograma con pasos, que los recibe todos, y después se limpian.
// Sin raylib ni SDL: lo usa platform/input y se prueba sin ventana.
class ButtonLatch {
public:
    // frame_buttons: botones pulsados en este fotograma. step_count: pasos de física del
    // fotograma. Devuelve los botones que se aplican a esos pasos (sin uso si son 0).
    std::uint16_t apply(std::uint16_t frame_buttons, int step_count);

private:
    std::uint16_t pending_ = 0;
};

} // namespace rally::platform
