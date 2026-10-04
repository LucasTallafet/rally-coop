#pragma once

#include "platform/forest_scene.h"
#include "platform/perf_stats.h"
#include "platform/process_memory.h"

#include <array>
#include <cstdint>

namespace rally::platform {

class DriveDemo;
class FramePacer;
class PerfCounter;

// Medida del bosque denso (--bench-forest): pasa por los cuatro modos (sin bosque, uno a uno,
// DrawMeshInstanced de raylib e instanciado persistente) con kWarmupNs de calentamiento y
// kMeasureNs de medida cada uno, guarda una captura al final de cada modo y escribe
// rendimiento_NNN.txt junto al exe con medias y máximos. main.cpp lo arranca sin
// sincronización vertical. El tiempo cuenta con la duración de los fotogramas medidos, no con
// el reloj.
class ForestBench {
public:
    static constexpr std::int64_t kWarmupNs = 2'000'000'000;
    static constexpr std::int64_t kMeasureNs = 10'000'000'000;
    static constexpr int kModes = 4;

    // Pone la demo en el primer modo.
    explicit ForestBench(DriveDemo& demo);

    // Una vez por fotograma, después de PerfCounter::begin_frame. Devuelve false cuando ha
    // terminado y ha escrito el informe.
    bool update(const PerfCounter& perf, const FramePacer& pacer, DriveDemo& demo);

private:
    void write_report(const PerfCounter& perf, const FramePacer& pacer, const DriveDemo& demo);

    int mode_ = 0;
    std::int64_t elapsed_ns_ = 0; // en el modo actual
    bool captured_ = false;       // captura pedida en el fotograma anterior
    std::array<PerfTotals, kModes> totals_{};
    std::array<ProcessMemory, kModes> memory_{}; // máximos durante la medida
};

} // namespace rally::platform
