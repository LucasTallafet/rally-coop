#pragma once

#include "platform/audio_output.h"
#include "platform/frame_history.h"
#include "platform/gl_probe.h"
#include "platform/perf_stats.h"
#include "platform/process_memory.h"

#include <cstdint>

namespace rally::platform {

class FixedStepClock;
class FramePacer;
class Overlay;
struct FrameSteps;

// Contador de rendimiento en pantalla (F1): CPU por fotograma desglosada en simulación
// (total y por paso), render, espera de presentación y resto; GPU con consultas de tiempo;
// draw calls reales; memoria del proceso; y la gráfica de ritmo de fotogramas.
//
// Solo mide alrededor de las llamadas del bucle: no toca la simulación ni lo que recibe. Sin
// reservas por fotograma (buffers fijos y TextFormat de raylib).
//
// Orden en cada fotograma:
//   pacer.begin_frame → begin_frame → record_steps → (begin_step, step, end_step)* →
//   BeginDrawing → begin_render → dibujo → draw → end_render → EndDrawing → end_present
class PerfCounter {
public:
    // Necesita la ventana abierta (instala el contador de draw calls y crea las consultas).
    PerfCounter();

    bool visible() const { return visible_; }
    void set_visible(bool visible) { visible_ = visible; }

    // Cierra la muestra del fotograma anterior: frame_ns y pacer_wait_ns, de
    // FramePacer::begin_frame y last_wait_ns.
    void begin_frame(std::int64_t frame_ns, std::int64_t pacer_wait_ns);
    // Historial de la gráfica: duración del fotograma anterior y pasos de este (como en la 4b).
    void record_steps(std::int64_t frame_ns, const FrameSteps& steps);

    void begin_step();
    void end_step();

    void begin_render(); // justo después de BeginDrawing
    // Vacía el lote de rlgl, para que su envío cuente en el render y en la consulta de GPU.
    void end_render();
    void end_present(); // justo después de EndDrawing

    // Dibuja el panel (si está visible) en su sitio del diseño de la pantalla (capa Perf) entre
    // begin_render y end_render. render_width y render_height: resolución a la que se dibuja la
    // escena.
    void draw(const FramePacer& pacer, const FixedStepClock& clock, bool snap_enabled,
              int render_width, int render_height, Overlay& overlay) const;
    // Medidas del hilo de audio (tarea 2.7) para su línea del panel.
    void set_audio(const AudioStats& audio) { audio_ = audio; }

    // Última muestra cerrada (la del fotograma anterior) y la memoria leída por última vez.
    const FrameSample& last_sample() const { return last_; }
    const ProcessMemory& memory() const { return memory_; }
    // Lee la memoria ahora (fuera del ritmo de 0,5 s; para la medida del bosque).
    void refresh_memory();
    const char* renderer() const { return renderer_; }

private:
    bool visible_ = false;
    DrawCallCounter draw_calls_;
    GpuTimer gpu_;
    PerfStats stats_;
    FrameHistory history_;
    ProcessMemory memory_;
    const char* renderer_ = "";

    FrameSample current_; // fotograma en curso
    FrameSample last_;    // último cerrado
    bool first_frame_ = true;
    std::int64_t step_start_ns_ = 0;
    std::int64_t render_start_ns_ = 0;
    std::int64_t present_start_ns_ = 0;
    AudioStats audio_;
};

} // namespace rally::platform
