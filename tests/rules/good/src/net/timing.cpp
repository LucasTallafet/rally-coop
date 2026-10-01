// Ejemplo para cmake/CheckSimRules.cmake: la red sí puede leer el reloj. No se compila.
#include <chrono>
auto now = std::chrono::steady_clock::now();
