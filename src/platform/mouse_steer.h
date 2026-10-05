#pragma once

#include <array>

namespace rally::platform {

// Barra de texto del volante para el indicador mínimo: kWheelBarChars posiciones de tope a tope,
// '-' de pista, '+' en el centro y 'O' en la posición del volante (al más cercano). Terminada en 0.
inline constexpr int kWheelBarChars = 19;
std::array<char, kWheelBarChars + 1> wheel_bar(float steer);

// Dirección con ratón, en posición (tarea 2.5b): el desplazamiento horizontal acumulado es el
// ángulo del volante, de −1 a 1. `full_lock_px` son los píxeles para el tope (la sensibilidad,
// ajustes.txt). La posición se acota al tope: pasarse y volver responde en cuanto se vuelve,
// sin recorrer los píxeles sobrantes. Sin raylib: platform/input le pasa el desplazamiento de
// cada fotograma, y la simulación lee el objetivo en cada paso como el del stick (eje analógico,
// con las velocidades de las manos).
class MouseSteer {
public:
    // Desplazamiento horizontal de un fotograma (px, derecha positivo).
    void move(float dx_px, int full_lock_px);
    // Volante al centro (tecla, botón central, al perder el foco o al capturar el cursor).
    void recenter() { position_px_ = 0.0f; }
    // Volante en [−1, 1] con la sensibilidad del último move.
    float steer() const;
    float position_px() const { return position_px_; }

private:
    float position_px_ = 0.0f;
    float full_lock_px_ = 1.0f;
};

} // namespace rally::platform
