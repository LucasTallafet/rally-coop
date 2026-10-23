#pragma once

namespace rally::platform {

// Ángulo en (-π, π].
double wrap_angle(double a);

// Ruedas del copiloto: a su ventana solo llega la pose del coche (net::StateBuffer::sample), no el
// giro de las ruedas. Su giro visual se integra del avance de la pose: `forward_distance` es el
// desplazamiento del coche entre dos fotogramas proyectado sobre su eje hacia delante (m,
// negativo hacia atrás) y `radius` el de la rueda. Un salto de más de kWheelSpinMaxJump (un
// recolocado o un mundo nuevo) no hace girar las ruedas. Solo presentación.
inline constexpr double kWheelSpinMaxJump = 5.0;
double wheel_spin_delta(double forward_distance, double radius);

// Longitud de la suspensión con la que se dibuja la rueda del copiloto: en equilibrio y en llano el
// centro de la rueda queda a su radio del suelo, con el origen del coche en el suelo
// (sim/car_controller.h), luego es la altura del anclaje menos el radio.
double rest_suspension_length(double attach_y, double radius);

} // namespace rally::platform
