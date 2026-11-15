#pragma once

#include <cstdint>

namespace rally::platform {

// Una rueda de los diales del copiloto (docs/01 §5.3): qué posición elige un vector del stick (o
// del puntero). Sin raylib ni SDL. Función pura de la forma de la rueda, del vector y de la
// posición elegida en el fotograma anterior (la histéresis).
//
// El vector es (x a la derecha, y arriba), módulo 1 a fondo. Los ángulos se miden desde arriba (0°)
// en sentido horario.
//   - Círculo de `count` sectores centrados en k · 360° / count (0 arriba): secundario y distancia.
//   - Media rueda de `count` posiciones de 0° (arriba) a 180° (abajo), simétrica: se lee con |x|,
//     así que empujar a cualquier lado da la misma posición (dial principal: 6 arriba, H abajo).

inline constexpr float kWheelDeadzone = 0.50f;     // por debajo, sin elección
inline constexpr float kWheelDeadzoneStay = 0.40f; // ya elegida: se mantiene hasta bajar de aquí
inline constexpr float kWheelHysteresisDeg = 4.0f; // pasado el límite con la vecina

enum class WheelShape : std::uint8_t { Circle, HalfCircle };

struct WheelSpec {
    WheelShape shape = WheelShape::Circle;
    int count = 8;
    // Ancho de un sector, en grados.
    float step_deg() const {
        return shape == WheelShape::Circle ? 360.0f / count : 180.0f / (count - 1);
    }
};

inline constexpr WheelSpec kMainWheel{WheelShape::HalfCircle, 7};
inline constexpr WheelSpec kRing8Wheel{WheelShape::Circle, 8};

// Ángulo del vector desde arriba en sentido horario, en [0, 360). (0, 0) da 0.
float wheel_angle_deg(float x, float y);
// Ángulo central de la posición `index`, en grados desde arriba (para dibujar y para los tests).
float wheel_center_deg(const WheelSpec& spec, int index);
// La posición elegida, o -1 (centro: cancela). `current`: la del fotograma anterior (-1 ninguna).
int wheel_pick(const WheelSpec& spec, float x, float y, int current);

} // namespace rally::platform
