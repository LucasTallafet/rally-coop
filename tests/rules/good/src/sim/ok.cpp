// Ejemplo para cmake/CheckSimRules.cmake: nada de esto debe contar. No se compila.
// std::sin en un comentario de línea no cuenta.
/* std::cos en un comentario de bloque tampoco */
/*
   ni std::sqrt dentro de un bloque de varias líneas,
   ni #include <random>
*/
#include "sim/rmath.h"
double a = rmath::sin(x) + rmath::sqrt(y); /* comentario */ double b = rmath::cos(z);
double c = std::sin(x); // rmath-allow: comparación de referencia en una comprobación
double t = stage.time() + timer->clock();
int lo = std::min(1, 2);
double grain = operand(x);
double expected_rand = brand(x);
