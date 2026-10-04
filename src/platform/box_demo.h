#pragma once

#include "platform/forest_scene.h"
#include "platform/frame_log.h"
#include "platform/interpolation.h"
#include "platform/recording_session.h"
#include "platform/telemetry_history.h"
#include "platform/zone_draw.h"
#include "sim/sim_input.h"
#include "sim/sim_world.h"
#include "sim/surface.h"
#include "sim/test_zone.h"

#include <array>
#include <cstdint>
#include <memory>
#include <vector>

namespace rally::platform {

class DriverInput;
class FixedStepClock;
class FramePacer;
class PerfCounter;
struct FrameSteps;

// Demo temporal hasta el coche (tarea 2.4): una caja de Jolt conducida con el teclado o el
// mando como un coche simplificado y simulada a 240 Hz en SimWorld, en la zona de pruebas de
// la fase 2 (por defecto) o en el recinto de la fase 1.
//
// Conducción en platform/input.h (W/S acelerador y freno, A/D, Espacio freno de mano, Q/E
// marchas, R recolocar, X salto temporal). Retroceso reinicia el mundo, K alterna la zona y
// el recinto, y en el recinto O lo reconstruye a 20 km del origen (o lo devuelve); todos
// cierran la grabación de la partida y empiezan otra (grabaciones/AAAAMMDD-HHMMSS.rrec junto
// al exe). En la zona, G cambia la superficie de la pista ciega y M el modo a ciegas: todas
// las superficies del mismo color, sin h y sin nombres en el overlay; ahí G pasa a la
// siguiente prueba de la serie de 15 (--blind-seed). Tab cambia la cámara (primera persona,
// fija o, en la zona, detrás y por encima, y la vista de toda la zona), J dibuja en float del
// mundo (depuración, para ver el temblor),
// F alterna frecuencia del monitor / 30 fps, I la interpolación, V la sincronización
// vertical, P la pausa, C guarda una captura (captura_NNN.png junto al exe), N activa el
// ajuste del dt al refresco del monitor, L empieza o acaba el registro de fotogramas
// (fotogramas_NNN.csv junto al exe), B cambia el modo de la escena de medida del bosque
// denso y T muestra las gráficas de la telemetría de los últimos 10 s (no en el modo a
// ciegas). El contador de rendimiento (F1) es aparte: PerfCounter.
class BoxDemo {
public:
    // Necesita la ventana abierta (fija los planos de recorte de raylib).
    BoxDemo();
    ~BoxDemo();
    BoxDemo(const BoxDemo&) = delete;
    BoxDemo& operator=(const BoxDemo&) = delete;

    void handle_keys(FramePacer& pacer);

    // N: ajuste del dt al periodo del monitor en FixedStepClock (activo por defecto).
    bool snap_enabled() const { return snap_; }

    // Empieza el registro de fotogramas (también con L; --frame-log al arrancar).
    void start_frame_log() { frame_log_.start(); }

    // Dibuja la escena en una RenderTexture de width × height y la muestra escalada a la
    // ventana (--bench-forest: medida a 1920 × 1080 en cualquier monitor). El texto va a la
    // resolución de la ventana.
    void set_render_size(int width, int height);
    int render_width() const;
    int render_height() const;

    ForestMode forest_mode() const { return forest_mode_; }
    void set_forest_mode(ForestMode mode) { forest_mode_ = mode; }

    // Recinto de la fase 1 en vez de la zona (--bench-forest mide el bosque alrededor de él).
    void use_arena();
    // Semilla de la serie de pruebas a ciegas (--blind-seed).
    void set_blind_seed(std::uint64_t seed) { blind_seed_ = seed; }

    // Medida del dibujo del suelo (--bench-zone): la cámara recorre la pista ciega a 100 km/h
    // a la altura del ojo durante kZoneBenchWarmupNs + kZoneBenchMeasureNs y escribe
    // rendimiento_zona_NNN.txt junto al exe. zone_bench_done() pasa a true al terminar.
    static constexpr std::int64_t kZoneBenchWarmupNs = 3'000'000'000;
    static constexpr std::int64_t kZoneBenchMeasureNs = 20'000'000'000;
    void start_zone_bench();
    bool zone_bench_done() const { return zone_bench_done_; }
    void request_capture() { capture_requested_ = true; }

    // Antes de los pasos del fotograma: deja sitio en la grabación (fuera del paso).
    void prepare_steps(int count);

    // Un paso de 1/240 s, que queda grabado. Devuelve el impacto de la caja controlada en
    // ese paso (m/s; 0 en pausa), para la vibración. En pausa no hay paso ni grabación.
    float step(const sim::SimInput& input);

    // Dibuja y presenta el fotograma, con el contador encima. input: la entrada aplicada en
    // este fotograma; frame_impact: el pico de sus pasos.
    void draw(const FrameSteps& steps, std::int64_t frame_ns, const FramePacer& pacer,
              const FixedStepClock& clock, const DriverInput& driver, const sim::SimInput& input,
              float frame_impact, PerfCounter& perf);

private:
    // Termina la grabación del mundo actual y construye uno nuevo con su grabación.
    void reset();
    void read_poses(std::vector<Pose>& poses) const;
    // Descripción del mundo según el modo (zona o recinto) y la superficie de la pista ciega.
    void describe_world();
    void advance_zone_bench(std::int64_t frame_ns);
    void write_zone_bench_report() const;
    void draw_zone_overlay(int y) const;

    sim::WorldDesc desc_;
    bool zone_mode_ = true;                             // K: zona de pruebas o recinto
    sim::Surface blind_surface_ = sim::Surface::Gravel; // G: superficie de la pista ciega
    bool blind_mode_ = false;                           // M: modo a ciegas
    std::uint64_t blind_seed_ = 1;                      // --blind-seed
    std::array<sim::Surface, sim::kBlindTrials> blind_series_{};
    int blind_trial_ = 0; // prueba en curso (0..14)
    int view_ = 0;        // Tab: 0 primera persona, 1 fija o detrás, 2 toda la zona
    ZoneDraw zone_draw_;
    // --bench-zone: recorrido de la cámara y tiempo transcurrido.
    bool zone_bench_ = false;
    bool zone_bench_done_ = false;
    std::int64_t zone_bench_ns_ = 0;
    bool zone_bench_measuring_ = false;
    double zone_bench_frame_ms_total_ = 0.0;
    double zone_bench_frame_ms_max_ = 0.0;
    std::int64_t zone_bench_frames_ = 0;
    // CPU del suelo por fotograma medido, con su desglose.
    struct ZoneBenchFrame {
        double ground_ms = 0.0;
        double frame_ms = 0.0;
        ZoneDraw::Breakdown breakdown;
    };
    std::vector<ZoneBenchFrame> zone_bench_ground_ms_;
    std::unique_ptr<sim::SimWorld> world_;
    std::vector<Pose> previous_; // por cuerpo, en el orden de la descripción
    std::vector<Pose> current_;
    std::vector<Pose> drawn_; // poses del fotograma (interpoladas o no)
    bool interpolate_ = true;
    bool paused_ = false;             // P: congela la simulación
    bool capture_requested_ = false;  // C: captura del búfer al final del fotograma
    bool far_from_origin_ = false;    // O: mundo a 20 km del origen en x y z
    bool world_float_ = false;        // J: dibujo en float del mundo en vez de relativo
    bool overlay_visible_ = true;     // H: texto del overlay
    bool snap_ = true;                // N: ajuste del dt al refresco
    float last_rumble_impact_ = 0.0f; // último pico que hizo vibrar, en m/s

    ForestScene forest_; // B: escena de medida
    ForestMode forest_mode_ = ForestMode::Off;
    RenderTexture2D target_{}; // id 0 si se dibuja directamente en la ventana
    FrameLog frame_log_;       // L: registro de diagnóstico
    RecordingSession recording_;
    // T: gráficas de la telemetría (no en el modo a ciegas). La historia se llena siempre.
    TelemetryHistory telemetry_history_;
    GraphPager graphs_;
    // Pulsaciones vistas por la simulación desde el último reinicio (para comprobar botones).
    int shift_up_presses_ = 0;
    int shift_down_presses_ = 0;
    int reset_presses_ = 0;
};

} // namespace rally::platform
