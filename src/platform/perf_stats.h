#pragma once

#include <array>
#include <cstdint>

namespace rally::platform {

// Medidas de un fotograma para el contador de rendimiento, en nanosegundos. Las partes de
// CPU no se solapan: fotograma = simulación + render + presentación + resto.
struct FrameSample {
    std::int64_t frame_ns = 0;    // de un begin_frame del pacer al siguiente
    std::int64_t sim_ns = 0;      // todos los pasos de física del fotograma
    int steps = 0;                // pasos de física del fotograma
    std::int64_t step_max_ns = 0; // el paso más largo del fotograma
    std::int64_t render_ns = 0;   // CPU de BeginDrawing al vaciado del lote antes de EndDrawing
    std::int64_t present_ns = 0;  // EndDrawing (presentación y eventos) + espera del pacer
    std::int64_t gpu_ns = -1;     // resultado de GPU llegado en este fotograma; −1 si no hay
    int draw_calls = 0;           // llamadas glDraw* reales
    int instanced_calls = 0;      // de ellas, instanciadas
    std::int64_t instances = 0;   // instancias dibujadas por las instanciadas

    // Lo que no es simulación, render ni presentación (entrada, vibración, lógica del bucle).
    std::int64_t other_ns() const { return frame_ns - sim_ns - render_ns - present_ns; }
};

// Suma, número de muestras y máximo. Sin reservas.
struct RunningTotal {
    std::int64_t sum = 0;
    std::int64_t count = 0;
    std::int64_t max = 0;

    // Un grupo de `count` muestras que suman `sum` y cuya mayor es `max`.
    void add(std::int64_t group_sum, std::int64_t group_count, std::int64_t group_max);
    void add(std::int64_t value) { add(value, 1, value); }
    // Media por muestra; 0 sin muestras.
    double mean() const;
};

// Máximo de los valores de los últimos ~2 s: 20 cubos de 100 ms con el máximo de cada uno.
// Memoria fija con cualquier número de fotogramas por segundo; la ventana real va de 1,9 a
// 2 s según en qué punto del cubo actual se pregunte.
class RollingMax {
public:
    static constexpr std::int64_t kBucketNs = 100'000'000;
    static constexpr int kBuckets = 20;

    void add(std::int64_t now_ns, std::int64_t value);
    // Máximo de los cubos de los últimos 2 s respecto a now_ns; 0 si no hay ninguno.
    std::int64_t max(std::int64_t now_ns) const;

private:
    struct Bucket {
        std::int64_t index = -1; // now_ns / kBucketNs del cubo; −1 si está vacío
        std::int64_t max = 0;
    };
    std::array<Bucket, kBuckets> buckets_{};
};

// Una magnitud del contador: media por muestra en periodos de 0,5 s y máximo de los últimos 2 s.
class PerfChannel {
public:
    void add(std::int64_t now_ns, std::int64_t group_sum, std::int64_t group_count,
             std::int64_t group_max);
    void add(std::int64_t now_ns, std::int64_t value) { add(now_ns, value, 1, value); }
    // Cierra el periodo: fija la media y el máximo que se enseñan y empieza otro periodo.
    void publish(std::int64_t now_ns);

    double average() const { return average_; } // del último periodo cerrado
    std::int64_t max() const { return max_; }   // de los 2 s anteriores al cierre
    bool has_data() const { return has_data_; } // algún periodo cerrado tenía muestras

private:
    RunningTotal period_;
    RollingMax rolling_;
    double average_ = 0.0;
    std::int64_t max_ = 0;
    bool has_data_ = false;
};

// Estadísticas del contador en pantalla. Se publican cada 0,5 s para que se puedan leer.
class PerfStats {
public:
    static constexpr std::int64_t kPeriodNs = 500'000'000;

    // now_ns: instante en que se cierra el fotograma, del mismo reloj que el resto.
    // Devuelve true si en esta llamada se ha publicado un periodo nuevo.
    bool push(std::int64_t now_ns, const FrameSample& sample);

    double fps() const { return fps_; }
    // Por fotograma, salvo step (por paso) y gpu (por resultado recibido).
    const PerfChannel& frame() const { return frame_; }
    const PerfChannel& sim() const { return sim_; }
    const PerfChannel& step() const { return step_; }
    const PerfChannel& steps() const { return steps_; }
    const PerfChannel& render() const { return render_; }
    const PerfChannel& present() const { return present_; }
    const PerfChannel& other() const { return other_; }
    const PerfChannel& gpu() const { return gpu_; }
    const PerfChannel& draw_calls() const { return draw_calls_; }
    const PerfChannel& instanced_calls() const { return instanced_calls_; }
    const PerfChannel& instances() const { return instances_; }

private:
    std::int64_t period_start_ns_ = -1;
    int period_frames_ = 0;
    double fps_ = 0.0;
    PerfChannel frame_;
    PerfChannel sim_;
    PerfChannel step_;
    PerfChannel steps_;
    PerfChannel render_;
    PerfChannel present_;
    PerfChannel other_;
    PerfChannel gpu_;
    PerfChannel draw_calls_;
    PerfChannel instanced_calls_;
    PerfChannel instances_;
};

// Totales de una medida larga (--bench-forest): media y máximo de cada magnitud sobre todos
// los fotogramas medidos.
struct PerfTotals {
    std::int64_t frames = 0;
    RunningTotal frame;
    RunningTotal sim;
    RunningTotal step; // por paso
    RunningTotal render;
    RunningTotal present;
    RunningTotal other;
    RunningTotal gpu; // por resultado recibido
    RunningTotal draw_calls;
    RunningTotal instances;

    void add(const FrameSample& sample);
};

} // namespace rally::platform
