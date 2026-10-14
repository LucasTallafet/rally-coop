// Ejemplo para cmake/CheckSimRules.cmake. No se compila.
#include <chrono>                                 // 8: reloj en src/gen
auto t = std::chrono::steady_clock::now();        // 9: std::chrono
long s = time(nullptr);                           // 10: time()
QueryPerformanceCounter(&counter);                // 11: contador de Windows
