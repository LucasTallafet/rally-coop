#pragma once

#include "game/direction_changes.h"

#include <vector>

namespace rally::platform {

// El cursor de la tablet (docs/01 §5.2, sin notas todavía): una `s` sobre el eje del tramo. Sin
// raylib ni SDL; solo lee `s` del coche (la del host) y los cambios de dirección.
//
// Por defecto está en el siguiente cambio de dirección por delante del coche y salta al siguiente
// cuando el coche lo pasa. Arriba salta al cambio siguiente desde el cursor y abajo al anterior
// (nunca por detrás del coche); mantenido más de kCursorHoldS desliza por la carretera, de
// kCursorSlideMin a kCursorSlideMax m/s en kCursorRampS. Un clic coloca el cursor. Si el coche
// pasa un cursor puesto a mano, vuelve al valor por defecto.

inline constexpr double kCursorHoldS = 0.3;      // s mantenido antes de deslizar
inline constexpr double kCursorSlideMin = 20.0;  // m/s al empezar a deslizar
inline constexpr double kCursorSlideMax = 200.0; // m/s tras kCursorRampS
inline constexpr double kCursorRampS = 2.0;      // s de la rampa (desde que empieza a deslizar)

// La dirección mantenida en un fotograma: +1 arriba (hacia delante), −1 abajo, 0 nada.
class TabletCursor {
public:
    // Una vez por fotograma. `s_car`: la del host; `length`: longitud del eje (tope del cursor);
    // `held`: la dirección mantenida (+1, −1 o 0); `dt`: segundos del fotograma.
    void update(const std::vector<game::DirectionChange>& changes, double s_car, double length,
                int held, double dt);
    // Coloca el cursor a mano (el clic del puntero). Acotado a [s_car, length].
    void place(double s, double s_car, double length);
    // Mundo nuevo.
    void reset() { *this = TabletCursor(); }

    // Hay cursor: sin cambios de dirección por delante y sin haberlo puesto, no hay.
    bool valid() const { return valid_; }
    double s() const { return s_; }
    // Puesto a mano (salto, deslizamiento o clic) y todavía por delante del coche.
    bool manual() const { return manual_; }

    // Velocidad de deslizamiento (m/s) tras `slid_s` segundos deslizando.
    static double slide_speed(double slid_s);

private:
    void jump(const std::vector<game::DirectionChange>& changes, double s_car, int dir);

    bool valid_ = false;
    bool manual_ = false;
    double s_ = 0.0;
    int held_ = 0;
    double hold_s_ = 0.0;
};

} // namespace rally::platform
