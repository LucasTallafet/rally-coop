#pragma once

#include "platform/interpolation.h"

#include <cstdint>
#include <string>

namespace rally::platform {

class FramePacer;
struct FrameSteps;

// Entrada de la demo. Se lee una vez por fotograma y se aplica igual a todos sus pasos.
struct DemoInput {
    bool reverse = false; // Espacio: gira al revés
};

// Demo temporal del bucle (fase 1, tarea 2): un cuadrado que da vueltas en círculo y gira
// sobre sí mismo, con su estado simulado a 240 Hz. Sirve para ver a ojo el paso fijo y la
// interpolación. Se quita cuando haya un cuerpo de Jolt (tarea 4).
//
// Teclas: F alterna frecuencia del monitor / 30 fps, I la interpolación, V la sincronización
// vertical, P la pausa y C guarda una captura (captura_NNN.png junto al exe).
class LoopDemo {
public:
    LoopDemo();

    void handle_keys(FramePacer& pacer);
    DemoInput read_input() const;

    // Un paso de 1/240 s.
    void step(const DemoInput& input);

    void draw(const FrameSteps& steps, std::int64_t frame_ns, const FramePacer& pacer);

private:
    void update_stats(std::int64_t frame_ns);

    double orbit_ = 0.0; // ángulo en el círculo, en radianes
    double spin_ = 0.0;  // giro del cuadrado sobre sí mismo, en radianes
    Pose previous_;
    Pose current_;
    bool interpolate_ = true;
    bool paused_ = false;            // P: congela la simulación
    bool capture_requested_ = false; // C: captura del búfer al final del fotograma
    std::string renderer_;           // GL_RENDERER; pasará al contador de rendimiento (tarea 6)

    // Contador en pantalla: media de fps y ms cada medio segundo.
    std::int64_t stats_ns_ = 0;
    int stats_frames_ = 0;
    double fps_ = 0.0;
    double average_ms_ = 0.0;
};

} // namespace rally::platform
