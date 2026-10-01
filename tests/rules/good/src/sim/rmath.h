// Ejemplo para cmake/CheckSimRules.cmake: src/sim/rmath.h está excluido. No se compila.
#include <cmath>
inline double sin(double x) { return std::sin(x); }
