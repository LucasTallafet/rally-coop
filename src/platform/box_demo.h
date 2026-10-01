#pragma once

#include "platform/interpolation.h"
#include "sim/sim_input.h"
#include "sim/sim_world.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rally::platform {

class FramePacer;
struct FrameSteps;

// Demo temporal de la fase 1: una caja de Jolt en un recinto, movida con el teclado y
// simulada a 240 Hz en SimWorld. Sirve para ver a ojo el paso fijo y la interpolación con
// un cuerpo real. La cámara es fija; la de primera persona llega en otra tarea.
//
// Teclas: flechas/WASD mueven la caja, Espacio salta, R reinicia el mundo, F alterna
// frecuencia del monitor / 30 fps, I la interpolación, V la sincronización vertical, P la
// pausa y C guarda una captura (captura_NNN.png junto al exe).
class BoxDemo {
public:
    BoxDemo();

    void handle_keys(FramePacer& pacer);

    // Un paso de 1/240 s.
    void step(const sim::SimInput& input);

    void draw(const FrameSteps& steps, std::int64_t frame_ns, const FramePacer& pacer);

private:
    void reset();
    void read_poses(std::vector<Pose>& poses) const;
    void update_stats(std::int64_t frame_ns);

    sim::WorldDesc desc_;
    std::unique_ptr<sim::SimWorld> world_;
    std::vector<Pose> previous_; // por cuerpo, en el orden de la descripción
    std::vector<Pose> current_;
    bool interpolate_ = true;
    bool paused_ = false;            // P: congela la simulación
    bool capture_requested_ = false; // C: captura del búfer al final del fotograma
    std::string renderer_;           // GL_RENDERER; pasará al contador de rendimiento

    // Contador en pantalla: media de fps y ms cada medio segundo.
    std::int64_t stats_ns_ = 0;
    int stats_frames_ = 0;
    double fps_ = 0.0;
    double average_ms_ = 0.0;
};

} // namespace rally::platform
