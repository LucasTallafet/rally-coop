#pragma once

#include "game/notes.h"
#include "platform/copilot_wheel.h"

#include <array>
#include <cstdint>

namespace rally::platform {

// Los diales del copiloto (docs/01 §5.3, lote 7): dial principal (nota de curva), secundario
// (peligros y modificadores), distancia (rueda de 8 valores o contador de metros) y borrar. Sin
// raylib ni SDL: las tres entradas (mando, ratón y teclado; platform/copilot_dial_input) se reducen
// a un DialsInput por fotograma y aquí vive la única lógica.
//
// Un dial se abre al mantener su control, se elige la posición y se confirma al soltar; con el
// vector en el centro (o sin número, con el teclado) soltar cancela. Mientras hay un dial abierto,
// los demás no se abren. El juego nunca rellena la distancia: la que sale es la que eligió el
// copiloto, y DistanceCounter no recibe ni la `s` del coche ni los cambios de dirección.

enum class DialKind : std::uint8_t { None, Main, Secondary, Distance };
enum class DistanceForm : std::uint8_t { Wheel, Counter };

// Contador de metros (de 0 a 990, de 10 en 10, parte de 50). Mantenido, repite: la primera
// pulsación cuenta al instante, tras kCounterFirstRepeatS repite kCounterSlowRate veces por segundo
// y, pasado kCounterFastAfterS, kCounterFastRate. No lee nada del coche ni del tramo.
inline constexpr int kCounterStartM = 50;
inline constexpr int kCounterStepM = 10;
inline constexpr int kCounterMaxM = 990;
inline constexpr double kCounterFirstRepeatS = 0.4;
inline constexpr double kCounterSlowRate = 8.0;   // pasos por segundo
inline constexpr double kCounterFastAfterS = 1.5; // s mantenido tras empezar a repetir
inline constexpr double kCounterFastRate = 20.0;

class DistanceCounter {
public:
    // Una vez por fotograma: `held` = −1 ([−]), +1 ([+]) o 0.
    void update(int held, double dt_s);
    int metres() const { return metres_; }
    // [cantar]: los metros elegidos; el contador vuelve a 50.
    int sing();
    void reset() { *this = DistanceCounter(); }
    // Para el test de «mantenido repite» y el dibujo.
    bool changed() const { return changed_; }

private:
    int metres_ = kCounterStartM;
    int held_ = 0;
    double hold_s_ = 0.0;   // desde que se pulsó
    double repeat_s_ = 0.0; // acumulado hacia el próximo paso
    bool changed_ = false;
};

// Borrar (una pulsación borra la última; mantenido kEraseHoldS vacía las pendientes). Nunca las
// dos.
inline constexpr double kEraseHoldS = 1.0;
enum class EraseEvent : std::uint8_t { None, EraseLast, ClearPending };
class EraseButton {
public:
    EraseEvent update(bool down, double dt_s);
    // 0..1 hacia el vaciado, para dibujar el progreso.
    double progress() const {
        return down_ ? (held_s_ < kEraseHoldS ? held_s_ / kEraseHoldS : 1.0) : 0.0;
    }

private:
    bool down_ = false;
    bool fired_ = false;
    double held_s_ = 0.0;
};

// Lo que dan las tres entradas en un fotograma.
struct DialsInput {
    // Controles que abren un dial (mantenidos).
    bool main_left = false; // hombro izquierdo / flecha izquierda / media rueda izquierda
    bool main_right = false;
    bool secondary = false;
    bool distance = false;
    // Elección analógica (stick o puntero relativo a la rueda): x a la derecha, y arriba.
    bool analog = false;
    float x = 0.0f;
    float y = 0.0f;
    // Elección directa de esta pulsación (teclado), −1 ninguna.
    int direct = -1;
    // Contador: [−] / [+] mantenido y [cantar] pulsado.
    int step_held = 0;
    bool sing_pressed = false;
    // Borrar, mantenido.
    bool erase_down = false;
    // Cambiar la forma de la distancia (pulsación).
    bool toggle_form = false;
};

// Posiciones del dial principal: 0 = «6» … 5 = «1», 6 = H. Teclado: 1–6 y H.
inline constexpr int kMainHairpinIndex = 6;
game::NoteGrade main_dial_grade(int index);
// Posiciones del secundario (docs/01 §5.3): cresta, salto, cuidado, se cierra, se abre, no cortes,
// larga y «y» (la «y» es una distancia, no una marca).
inline constexpr int kSecondaryCount = 8;
inline constexpr int kSecondaryLinkIndex = 7;
game::NoteMark secondary_mark(int index); // válida con index 0..6
// Rueda de distancias: el índice es el de game::wheel_distance (0 = «y», 1..6 = 30…200 m, 7 =
// «300+»).

constexpr int kMaxDialOrders = 4;
struct DialsOutput {
    std::array<game::NoteOrder, kMaxDialOrders> orders;
    int count = 0;
    // Una nota de curva o un peligro habría salido, pero no hay ancla (el cursor no está en ningún
    // sitio): se dice en pantalla.
    bool no_anchor = false;
    void add(const game::NoteOrder& order) {
        if (count < kMaxDialOrders) {
            orders[static_cast<std::size_t>(count++)] = order;
        }
    }
};

// Lo que dibuja la pantalla.
struct DialsView {
    DialKind open = DialKind::None;
    game::NoteSide side = game::NoteSide::None; // del principal
    int selected = -1;                          // posición elegida del dial abierto
    DistanceForm form = DistanceForm::Wheel;
    int counter_m = kCounterStartM;
    double erase_progress = 0.0;
};

class CopilotDials {
public:
    // Una vez por fotograma. `anchor_s`/`anchor_valid`: dónde está el cursor (el ancla de la nota
    // de curva o del peligro solo); solo se mira al confirmar. `attach_to_last`: hay una última
    // nota y el copiloto no ha movido el cursor a mano desde que la envió: un peligro del dial
    // secundario va entonces a esa nota (mark_order); si no, solo en el cursor (hazard_order). Los
    // modificadores van siempre a la última nota.
    DialsOutput update(const DialsInput& in, double dt_s, double anchor_s, bool anchor_valid,
                       bool attach_to_last);
    // Cierra el dial abierto sin cantar nada (la ayuda se abre, el mundo cambia).
    void cancel();

    DialsView view() const;
    bool open() const { return open_ != DialKind::None; }
    DistanceForm form() const { return form_; }
    void reset() { *this = CopilotDials(); }

private:
    void confirm(DialsOutput& out, double anchor_s, bool anchor_valid, bool attach_to_last);

    DialKind open_ = DialKind::None;
    game::NoteSide side_ = game::NoteSide::None;
    int selected_ = -1;
    bool counter_stepped_ = false;
    DistanceForm form_ = DistanceForm::Wheel;
    DistanceCounter counter_;
    EraseButton erase_;
};

} // namespace rally::platform
