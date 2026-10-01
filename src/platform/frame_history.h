#pragma once

#include <array>
#include <cstdint>

namespace rally::platform {

// Un fotograma en el historial de diagnóstico.
struct FrameRecord {
    std::int64_t measured_ns = 0; // duración medida
    std::int64_t used = 0;        // tiempo de juego aplicado, en unidades del reloj (ns × 240)
    int steps = 0;                // pasos de física simulados
};

// Recuentos sobre los fotogramas del historial.
struct FrameCounts {
    int frames = 0;         // fotogramas en el historial
    int expected_steps = 0; // pasos por fotograma previstos; 0 si no son enteros o no se conocen
    int off_steps = 0;      // fotogramas con un número de pasos distinto del previsto
    int dropped = 0;        // de ellos, fotogramas perdidos: duración ≥ 1,5 periodos
    int noise = 0;          // de ellos, ruido de medida: duración de ~1 periodo
};

// Últimos kCapacity fotogramas, para la gráfica del contador en pantalla. Sin reservas.
class FrameHistory {
public:
    static constexpr int kCapacity = 240;

    void push(const FrameRecord& record);

    int size() const { return size_; }
    // index 0 es el más antiguo; size() − 1, el último.
    const FrameRecord& at(int index) const;

    // frame_period: duración prevista de un fotograma en unidades del reloj (frecuencia del
    // monitor × intervalo, o 1/30 s); 0 si no se conoce (sin sincronización vertical). Solo
    // hay pasos previstos si el periodo es un número entero de pasos (4 a 60 Hz, 8 a 30 fps).
    FrameCounts count(std::int64_t frame_period) const;

    // Fotograma perdido: dura 1,5 periodos o más.
    static bool is_dropped(const FrameRecord& record, std::int64_t frame_period);

private:
    std::array<FrameRecord, kCapacity> records_{};
    int next_ = 0; // posición del próximo push
    int size_ = 0;
};

} // namespace rally::platform
