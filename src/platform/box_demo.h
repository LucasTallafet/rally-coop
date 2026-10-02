#pragma once

#include "platform/frame_history.h"
#include "platform/frame_log.h"
#include "platform/interpolation.h"
#include "sim/sim_input.h"
#include "sim/sim_world.h"

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rally::platform {

class DriverInput;
class FixedStepClock;
class FramePacer;
struct FrameSteps;

// Demo temporal de la fase 1: una caja de Jolt en un recinto, conducida con el teclado o el
// mando como un coche simplificado y simulada a 240 Hz en SimWorld. Sirve para ver a ojo el paso
// fijo, la interpolación y la cámara en primera persona con render relativo a la cámara.
//
// Teclas: W/S (o flechas) aceleran adelante y atrás, A/D giran, Espacio salta (con mando:
// gatillo derecho e izquierdo, stick izquierdo y el botón de abajo de la cara derecha), R reinicia
// el mundo, O lo reconstruye a 20 km del origen (o lo devuelve), Tab alterna la primera
// persona y la cámara fija, J dibuja en float del mundo (depuración, para ver el temblor),
// F alterna frecuencia del monitor / 30 fps, I la interpolación, V la sincronización
// vertical, P la pausa, C guarda una captura (captura_NNN.png junto al exe), G muestra la
// gráfica de ritmo de fotogramas, N activa el ajuste del dt al refresco del monitor y L
// empieza o acaba el registro de fotogramas (fotogramas_NNN.csv junto al exe).
class BoxDemo {
public:
    // Necesita la ventana abierta (fija los planos de recorte de raylib).
    BoxDemo();

    void handle_keys(FramePacer& pacer);

    // N: ajuste del dt al periodo del monitor en FixedStepClock (activo por defecto).
    bool snap_enabled() const { return snap_; }

    // Empieza el registro de fotogramas (también con L; --frame-log al arrancar).
    void start_frame_log() { frame_log_.start(); }

    // Un paso de 1/240 s. Devuelve el impacto de la caja controlada en ese paso (m/s; 0 en
    // pausa), para la vibración.
    float step(const sim::SimInput& input);

    // input: la entrada aplicada en este fotograma; frame_impact: el pico de sus pasos.
    void draw(const FrameSteps& steps, std::int64_t frame_ns, const FramePacer& pacer,
              const FixedStepClock& clock, const DriverInput& driver, const sim::SimInput& input,
              float frame_impact);

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
    bool paused_ = false;             // P: congela la simulación
    bool capture_requested_ = false;  // C: captura del búfer al final del fotograma
    bool first_person_ = true;        // Tab: primera persona o cámara fija
    bool far_from_origin_ = false;    // O: mundo a 20 km del origen en x y z
    bool world_float_ = false;        // J: dibujo en float del mundo en vez de relativo
    bool graph_visible_ = true;       // G: gráfica de ritmo de fotogramas
    bool snap_ = true;                // N: ajuste del dt al refresco
    std::string renderer_;            // GL_RENDERER; pasará al contador de rendimiento
    float last_rumble_impact_ = 0.0f; // último pico que hizo vibrar, en m/s

    // Contador en pantalla: media de fps y ms cada medio segundo.
    std::int64_t stats_ns_ = 0;
    int stats_frames_ = 0;
    double fps_ = 0.0;
    double average_ms_ = 0.0;
    FrameHistory history_; // pasará al contador de rendimiento con la gráfica
    FrameLog frame_log_;   // L: registro de diagnóstico
};

} // namespace rally::platform
