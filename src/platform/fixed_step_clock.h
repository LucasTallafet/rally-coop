#pragma once

#include <cstdint>

namespace rally::platform {

// Pasos de física por segundo y tope de pasos por fotograma (100 ms). El modo de 30 fps
// necesita 8 pasos por fotograma; el tope deja margen para picos sin frenar el juego.
inline constexpr std::int64_t kStepsPerSecond = 240;
inline constexpr int kMaxStepsPerFrame = 24;

// Unidad de tiempo del reloj: ns × kStepsPerSecond. Un paso son exactamente 10^9 unidades
// y 1/60 s, 4 × 10^9, sin redondeo.
inline constexpr std::int64_t kStepUnits = 1'000'000'000;

// Ajuste del dt a la sincronización vertical (ver FixedStepClock).
inline constexpr std::int64_t kSnapWindowUnits = 1'000'000 * kStepsPerSecond; // 1 ms
inline constexpr std::int64_t kRepayDivisor = 16;
inline constexpr std::int64_t kMaxRepayUnits = 100'000 * kStepsPerSecond;  // 0,1 ms
inline constexpr std::int64_t kMaxDebtUnits = 4'000'000 * kStepsPerSecond; // 4 ms

// Periodo de un refresco de numerator/denominator Hz, en unidades del reloj (redondeado a la
// unidad: el error de redondeo va a la deuda y se devuelve). 0 si numerator <= 0.
std::int64_t refresh_period_units(std::int64_t numerator, std::int64_t denominator);

// Lo que toca hacer en un fotograma.
struct FrameSteps {
    int count = 0;          // pasos de 1/240 s que se simulan en este fotograma
    std::int64_t first = 0; // número del primer de esos pasos (el siguiente sin simular)
    double alpha = 0.0;     // fracción de paso pendiente, en [0, 1): peso del último estado
    std::int64_t used = 0;  // tiempo de juego que avanza el fotograma, en unidades del reloj
    bool snapped = false;   // el dt se ajustó a un múltiplo del periodo del monitor
};

// Reloj de paso fijo: convierte la duración real de cada fotograma en pasos enteros de
// 1/240 s. La simulación solo ve el número de paso, nunca el tiempo del fotograma.
//
// El acumulador es entero (unidades de ns × 240). Sin ajuste, tras cualquier secuencia de
// fotogramas que no toque el tope se han dado exactamente floor(tiempo total / paso) pasos.
//
// Ajuste a la sincronización vertical (set_snap_period): la medida del dt tiene ruido
// (se toma al volver de la presentación, no en el refresco). Si el dt medido está a menos
// de 1 ms de un múltiplo k ≥ 1 del periodo, se usa k × periodo exacto. La diferencia va a
// una deuda que se devuelve poco a poco (deuda/16, como mucho 0,1 ms por fotograma); si
// pasa de 4 ms, se paga entera en ese fotograma. Así el tiempo de juego no se separa del
// real más de 4 ms más un desplazamiento fijo de origen:
//   tiempo real − deuda − tiempo de juego = constante
// Al empezar a ajustar (primer fotograma ajustado tras uno sin ajustar), la fracción del
// acumulador se coloca en medio paso: con fotogramas de un número entero de pasos, alpha
// queda lejos del borde entre pasos y el ruido no da pares de 3 y 5 pasos. Ese cambio de
// fase es un desplazamiento del origen del tiempo de juego (< medio paso), no deuda.
//
// Si un fotograma pide más de kMaxStepsPerFrame pasos (ventana arrastrada, pausa del
// depurador), se dan kMaxStepsPerFrame y se descartan los pasos enteros sobrantes y la
// deuda; solo se conserva la fracción de paso, para que alpha no salte. No hay espiral
// de retraso.
class FixedStepClock {
public:
    // frame_ns: duración real del fotograma en nanosegundos. Los valores negativos
    // cuentan como 0.
    FrameSteps advance(std::int64_t frame_ns);

    // Periodo del monitor en unidades del reloj (refresh_period_units); 0 desactiva el
    // ajuste. La deuda pendiente se sigue devolviendo con el ajuste desactivado.
    void set_snap_period(std::int64_t period_units);
    std::int64_t snap_period() const { return snap_period_; }

    // Pasos simulados desde el principio.
    std::int64_t steps_done() const { return steps_done_; }

    // Tiempo medido aún no aplicado al juego, en unidades del reloj (negativo si el juego
    // va por delante).
    std::int64_t debt() const { return debt_; }

private:
    std::int64_t accumulator_ = 0; // en unidades del reloj; un paso son kStepUnits
    std::int64_t steps_done_ = 0;
    std::int64_t snap_period_ = 0;
    std::int64_t debt_ = 0;
    bool snapping_ = false; // el último fotograma se ajustó
};

} // namespace rally::platform
