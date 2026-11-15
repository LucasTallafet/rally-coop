#pragma once

#include "platform/copilot_dials.h"
#include "platform/copilot_dials_layout.h"
#include "platform/input_shaping.h"

#include <array>

namespace rally::platform {

// Las tres entradas de los diales (mando, ratón y teclado) reducidas a un DialsInput. Sin raylib ni
// SDL: la ventana del copiloto les pasa lo leído. Una sola lógica (platform/copilot_dials).

// Un gatillo se abre por encima de kDialTriggerOpen y se cierra por debajo de kDialTriggerClose (el
// Switch Pro da 0 o 1): uno que ronda el umbral no abre y cierra el dial, que mandaría una orden en
// cada cierre.
inline constexpr float kDialTriggerOpen = 0.5f;
inline constexpr float kDialTriggerClose = 0.3f;

// --- Mando ---
// R1 y L1 abren el principal por su lado; el gatillo derecho, el secundario; el izquierdo, la
// distancia (rueda: stick; contador: cruceta izquierda y derecha); el stick izquierdo elige;
// Oeste (X Xbox, cuadrado PlayStation) borra; Back cambia la forma de la distancia.
class PadDialMapper {
public:
    DialsInput map(const PadState& pad, DistanceForm form);

private:
    bool previous_back_ = false;
    bool secondary_ = false;  // gatillo derecho, con histéresis
    bool distance_ = false;   // gatillo izquierdo
    bool left_first_ = false; // los dos hombros: manda el primero que se pulsó
    bool right_first_ = false;
};

// Une dos DialsInput (mando, teclado o ratón en el mismo fotograma): los controles mantenidos y los
// botones, con O; la elección analógica, la de mayor módulo; la directa, la primera.
DialsInput merge_dials(const DialsInput& a, const DialsInput& b);

// --- Teclado ---
// ← o → mantenida abre el principal por su lado y 1–6 o H eligen la nota; S mantenida, el
// secundario con 1–8; D mantenida, la distancia (rueda: 1–8; contador: ← y → son [−] y [+]);
// Retroceso borra; F cambia la forma. Prioridad: D, S, flechas.
struct KeyDialReading {
    bool left = false;
    bool right = false;
    bool secondary = false;
    bool distance = false;
    std::array<bool, 8> number_pressed{}; // 1..8, pulsadas en este fotograma
    bool hairpin_pressed = false;         // H
    bool erase_down = false;
    bool form_pressed = false;
};
DialsInput dials_from_keys(const KeyDialReading& keys, DistanceForm form);

// --- Ratón ---
// Con el puntero fuera (Espacio mantenido). Botón izquierdo mantenido sobre la rueda principal
// (el lado según la media rueda donde se pulsa) o la secundaria; botón derecho sobre la rueda de
// distancias; con el contador, [−], [cantar] y [+]; «borrar» y la forma, con el izquierdo. Lo que
// se pulsa se queda capturado hasta soltar el botón: el vector de elección sigue al puntero aunque
// salga de la rueda.
struct MouseDialReading {
    bool enabled = false; // puntero fuera y ventana con el foco
    bool left_down = false;
    bool left_pressed = false;
    bool right_down = false;
    bool right_pressed = false;
    double x = 0.0;
    double y = 0.0;
};
class MouseDialMapper {
public:
    DialsInput map(const MouseDialReading& mouse, const DialsGeometry& g, DistanceForm form);
    // El ratón tiene algo capturado (un dial abierto con él).
    bool captured() const { return captured_ != DialKind::None; }

private:
    DialKind captured_ = DialKind::None;
    bool captured_right_button_ = false;
    game::NoteSide side_ = game::NoteSide::None;
    int step_ = 0; // −1/+1: botón del contador pulsado
    bool erase_ = false;
};

} // namespace rally::platform
