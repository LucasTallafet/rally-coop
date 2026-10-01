// Ejemplo para cmake/CheckSimRules.cmake: cada línea marcada tiene una sola infracción.
// No se compila.
#include <cmath>                       // 1: <cmath>
double a = std::sin(x);                // 2: std::sin
double b = sqrt(x);                    // 3: sqrt sin calificar
double c = std::pow(x, 2.0); // rmath-allow:
