#pragma once

#include <cstdint>
#include <cstdio>

namespace rally::platform {

struct FrameSteps;

// Registro de diagnóstico del ritmo de fotogramas: una fila por fotograma en
// fotogramas_NNN.csv junto al exe, con el primer número libre. Cada fila se vuelca al
// momento, para que el archivo esté completo aunque el proceso se cierre a la fuerza.
//
// Columnas: fotograma, dt medido (ns), tiempo aplicado (ns), pasos, ajustado (0/1), alpha,
// deuda (ns), periodo de ajuste (ns, 0 sin ajuste), periodo previsto del fotograma (ns, 0
// si no se conoce). Los periodos y la deuda se pasan en unidades del reloj (ns × 240).
class FrameLog {
public:
    FrameLog() = default;
    ~FrameLog();
    FrameLog(const FrameLog&) = delete;
    FrameLog& operator=(const FrameLog&) = delete;

    // Abre un archivo nuevo; false si no se puede crear.
    bool start();
    void stop();
    bool active() const { return file_ != nullptr; }
    // Nombre del archivo abierto (vacío si no hay registro).
    const char* name() const { return name_; }

    void write(std::int64_t frame_ns, const FrameSteps& steps, std::int64_t debt,
               std::int64_t snap_period, std::int64_t frame_period);

private:
    std::FILE* file_ = nullptr;
    std::int64_t frame_ = 0;
    char name_[32] = {};
};

} // namespace rally::platform
