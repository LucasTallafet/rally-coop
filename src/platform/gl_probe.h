#pragma once

#include <array>
#include <cstdint>

namespace rally::platform {

// Medidas de OpenGL para el contador de rendimiento. Necesitan la ventana de raylib abierta.

// Llamadas de dibujo reales desde la última toma.
struct DrawCallCounts {
    int calls = 0;              // glDrawArrays, glDrawElements y sus versiones instanciadas
    int instanced_calls = 0;    // de ellas, instanciadas
    std::int64_t instances = 0; // instancias pedidas en las instanciadas
};

// Cuenta las llamadas glDraw* que llegan a OpenGL. En OpenGL 3.3, rlgl llama a GL a través
// de los punteros globales de glad (glad_glDrawArrays...), que están en raylib.lib: se
// sustituyen por envoltorios que cuentan y llaman al original. Así se cuentan los lotes de
// rlgl (una llamada por cambio de modo o de textura al vaciar el lote), DrawMesh y
// DrawMeshInstanced, también el texto del propio contador. Depende de la glad interna de
// raylib 6.0: si deja de cuadrar, el contador lo enseña como no disponible.
class DrawCallCounter {
public:
    // Instala los envoltorios (una sola vez por proceso; los siguientes no hacen nada).
    DrawCallCounter();

    // Comprueba que los envoltorios siguen puestos (los vuelve a poner si no) y devuelve las
    // llamadas desde la toma anterior.
    DrawCallCounts take();
    bool available() const { return available_; }

private:
    bool available_ = false;
};

// Tiempo de GPU con consultas GL_TIME_ELAPSED. Anillo de kQueries consultas: cada
// fotograma abre una; el resultado se lee varios fotogramas después y solo si ya está
// disponible, sin bloquear. Si el anillo da la vuelta a una consulta sin resultado, esa
// muestra se pierde.
class GpuTimer {
public:
    static constexpr int kQueries = 4;

    GpuTimer();
    ~GpuTimer();
    GpuTimer(const GpuTimer&) = delete;
    GpuTimer& operator=(const GpuTimer&) = delete;

    bool available() const { return available_; }
    void begin();
    void end();
    // Resultado más reciente llegado desde la llamada anterior, en ns; −1 si no ha llegado
    // ninguno.
    std::int64_t take_result();
    int lost() const { return lost_; } // muestras perdidas desde el arranque

private:
    bool available_ = false;
    bool active_ = false;
    std::array<unsigned int, kQueries> queries_{};
    std::array<bool, kQueries> pending_{};
    int next_ = 0;   // próxima consulta que se abre
    int oldest_ = 0; // consulta pendiente más antigua
    int lost_ = 0;
};

// GL_RENDERER: nombre de la gráfica.
const char* gl_renderer();

} // namespace rally::platform
