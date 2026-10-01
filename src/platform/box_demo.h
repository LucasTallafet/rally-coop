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

// Demo temporal de la fase 1: una caja de Jolt en un recinto, conducida con el teclado como
// un coche simplificado y simulada a 240 Hz en SimWorld. Sirve para ver a ojo el paso fijo,
// la interpolación y la cámara en primera persona con render relativo a la cámara.
//
// Teclas: W/S (o flechas) aceleran adelante y atrás, A/D giran, Espacio salta, R reinicia
// el mundo, O lo reconstruye a 20 km del origen (o lo devuelve), Tab alterna la primera
// persona y la cámara fija, J dibuja en float del mundo (depuración, para ver el temblor),
// F alterna frecuencia del monitor / 30 fps, I la interpolación, V la sincronización
// vertical, P la pausa y C guarda una captura (captura_NNN.png junto al exe).
class BoxDemo {
public:
    // Necesita la ventana abierta (fija los planos de recorte de raylib).
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
    std::vector<Pose> drawn_; // poses del fotograma (interpoladas o no)
    bool interpolate_ = true;
    bool paused_ = false;            // P: congela la simulación
    bool capture_requested_ = false; // C: captura del búfer al final del fotograma
    bool first_person_ = true;       // Tab: primera persona o cámara fija
    bool far_from_origin_ = false;   // O: mundo a 20 km del origen en x y z
    bool world_float_ = false;       // J: dibujo en float del mundo en vez de relativo
    std::string renderer_;           // GL_RENDERER; pasará al contador de rendimiento

    // Contador en pantalla: media de fps y ms cada medio segundo.
    std::int64_t stats_ns_ = 0;
    int stats_frames_ = 0;
    double fps_ = 0.0;
    double average_ms_ = 0.0;
};

} // namespace rally::platform
