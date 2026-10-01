#pragma once

#include <cstdint>

namespace rally::platform {

// Pasos de física por segundo y tope de pasos por fotograma (100 ms). El modo de 30 fps
// necesita 8 pasos por fotograma; el tope deja margen para picos sin frenar el juego.
inline constexpr std::int64_t kStepsPerSecond = 240;
inline constexpr int kMaxStepsPerFrame = 24;

// Lo que toca hacer en un fotograma.
struct FrameSteps {
    int count = 0;          // pasos de 1/240 s que se simulan en este fotograma
    std::int64_t first = 0; // número del primer de esos pasos (el siguiente sin simular)
    double alpha = 0.0;     // fracción de paso pendiente, en [0, 1): peso del último estado
};

// Reloj de paso fijo: convierte la duración real de cada fotograma en pasos enteros de
// 1/240 s. La simulación solo ve el número de paso, nunca el tiempo del fotograma.
//
// El acumulador es entero (nanosegundos × 240), así que tras cualquier secuencia de
// fotogramas que no toque el tope se han dado exactamente floor(tiempo total / paso)
// pasos, sin error acumulado de coma flotante.
//
// Si un fotograma pide más de kMaxStepsPerFrame pasos (ventana arrastrada, pausa del
// depurador), se dan kMaxStepsPerFrame y se descartan los pasos enteros sobrantes;
// solo se conserva la fracción de paso, para que alpha no salte. No hay espiral de retraso.
class FixedStepClock {
public:
    // frame_ns: duración real del fotograma en nanosegundos. Los valores negativos
    // cuentan como 0.
    FrameSteps advance(std::int64_t frame_ns);

    // Pasos simulados desde el principio.
    std::int64_t steps_done() const { return steps_done_; }

private:
    std::int64_t accumulator_ = 0; // en ns × kStepsPerSecond; un paso son 10^9
    std::int64_t steps_done_ = 0;
};

} // namespace rally::platform
