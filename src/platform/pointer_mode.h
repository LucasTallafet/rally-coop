#pragma once

namespace rally::platform {

// El puntero de la tablet (lote 6): mientras el copiloto mantiene la barra espaciadora, el ratón
// deja de mirar y sirve para pulsar en el mapa. Sin raylib ni SDL; la decisión es una función pura
// de lo que pasa en el fotograma.
//
// Con la barra mantenida: se suelta el cursor del sistema (el ratón sale de la captura), la
// cabeza se congela (HeadLook::set_frozen: ni el stick ni el ratón la mueven) y un clic en la
// carretera del mapa coloca el cursor. Al soltarla, el ratón vuelve a mirar desde donde estaba la
// cabeza.
struct PointerFrame {
    bool capture = false; // CursorCapture::update(want)
    bool frozen = false;  // HeadLook::set_frozen
    bool clicks = false;  // un clic en el mapa coloca el cursor
};

// `held`: la barra mantenida. `mouse_wanted`: el ratón mira en este fotograma (con el foco, sin
// pausa: platform/input_shaping, mouse_use ≠ None).
PointerFrame pointer_frame(bool held, bool mouse_wanted);

} // namespace rally::platform
