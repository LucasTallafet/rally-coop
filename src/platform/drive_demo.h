#pragma once

#include "io/recording.h"
#include "platform/demo_camera.h"
#include "platform/demo_driver_feel.h"
#include "platform/demo_measure.h"
#include "platform/demo_part.h"
#include "platform/demo_screen.h"
#include "platform/demo_world.h"
#include "platform/demo_zone_modes.h"
#include "sim/sim_input.h"

#include <array>
#include <cstdint>
#include <string>

namespace rally::platform {

class DriverInput;
class FixedStepClock;
class Gamepads;
class FramePacer;
class PerfCounter;
struct FrameSteps;

// Demo de conducción (desde la tarea 2.4): el coche del archivo .car simulado a 240 Hz en
// SimWorld, conducido con el teclado o el mando, en la zona de pruebas de la fase 2 (por
// defecto) o en el recinto de la fase 1.
//
// Desde la tarea P1a es un compositor fino: cada función de la demo es una parte (DemoPart, en
// sus archivos demo_*.{h,cpp} y con su tabla de teclas demo_*_keys) y DriveDemo reparte entre
// ellas cada paso y cada fotograma, siempre en el orden de su lista. Lo que comparten va en
// DemoContext; una parte no llama a otra. Añadir una función a la demo es añadir sus archivos,
// su miembro y su línea en la lista de partes de aquí, y su tabla en platform/key_tables.
//   - demo_world: mundo, coche, grabación de la partida, ajustes del jugador y dibujo de la escena.
//   - demo_zone_modes: zona o recinto, superficies, modo a ciegas y salida.
//   - demo_measure: bosque y medidas, capturas, registro, ritmo, pausa e interpolación.
//   - demo_driver_feel: lo que siente el conductor (cabeza, interior, vibración y sonido).
//   - demo_camera: vistas, ojo, asiento y cabeza.
//   - demo_screen: indicador, ayuda, depuración, gráficas y contador.
class DriveDemo {
public:
    // Necesita la ventana abierta (fija los planos de recorte de raylib). `car_path`: el archivo
    // del coche (--car o default_car_path()); si no se puede leer o tiene un error, aborta con
    // el motivo (RALLY_CHECK).
    explicit DriveDemo(std::string car_path);
    // --golden: el mundo y el coche de la grabación, los ajustes por defecto (sin leer ni escribir
    // ajustes.txt), sin audio y sin grabar la partida.
    DriveDemo(const GoldenFrame& golden, const io::Recording& recording);
    DriveDemo(const DriveDemo&) = delete;
    DriveDemo& operator=(const DriveDemo&) = delete;

    void handle_keys(FramePacer& pacer, PerfCounter& perf);

    // N: ajuste del dt al periodo del monitor en FixedStepClock (activo por defecto).
    bool snap_enabled() const { return ctx_.snap; }
    // Dirección con ratón (ajustes del jugador, F7) para DriverInput::set_mouse: si está elegida,
    // su sensibilidad y si se puede capturar el cursor ahora (no en pausa).
    bool mouse_steering() const { return ctx_.settings.steer_device == SteerDevice::Mouse; }
    int mouse_full_lock_px() const { return ctx_.settings.mouse_full_lock_px; }
    bool mouse_capture_allowed() const { return !ctx_.paused; }
    // Sensibilidad del stick (ajustes del jugador, teclas 4/5) para DriverInput.
    int steer_sensitivity_pct() const { return ctx_.settings.steer_sensitivity_pct; }

    // Empieza el registro de fotogramas (también con L; --frame-log al arrancar).
    void start_frame_log() { measure_.start_frame_log(); }

    // Dibuja la escena en una RenderTexture de width × height y la muestra escalada a la
    // ventana (--bench-forest: medida a 1920 × 1080 en cualquier monitor). El texto va a la
    // resolución de la ventana.
    void set_render_size(int width, int height) { measure_.set_render_size(ctx_, width, height); }
    int render_width() const;
    int render_height() const;

    ForestMode forest_mode() const { return ctx_.forest_mode; }
    void set_forest_mode(ForestMode mode) { measure_.set_forest_mode(ctx_, mode); }

    // Recinto de la fase 1 en vez de la zona (--bench-forest mide el bosque alrededor de él).
    void use_arena();
    // Semilla de la serie de pruebas a ciegas (--blind-seed).
    void set_blind_seed(std::uint64_t seed) { zone_modes_.set_blind_seed(seed); }
    // --hide-config: la pantalla no dice qué configuración se lleva (F8 sigue cambiándola;
    // bloque C del protocolo de la 2.10). En el modo a ciegas tampoco se dice.
    void set_hide_config(bool hide) { ctx_.mode.hide_config = hide; }

    // --bench-zone y --bench-road (demo_measure).
    void start_zone_bench(bool road = false);
    bool zone_bench_done() const { return measure_.zone_bench_done(); }
    void request_capture() { measure_.request_capture(); }

    // Antes de los pasos del fotograma: deja sitio en la grabación (fuera del paso).
    void prepare_steps(int count) { world_.prepare_steps(ctx_, count); }

    // Un paso de 1/240 s, que queda grabado. Devuelve el impacto del coche en ese paso (m/s; 0 en
    // pausa), para la vibración. En pausa no hay paso ni grabación.
    float step(const sim::SimInput& input);

    // Tras los pasos del fotograma: la vibración (continua y del impacto, mezcladas en
    // RumbleMixer) y la instantánea del sonido. frame_impact: el pico de sus pasos.
    void update_feedback(Gamepads& pads, float frame_impact, std::int64_t frame_ns);

    // Dibuja y presenta el fotograma, con el contador encima. input: la entrada aplicada en
    // este fotograma; frame_impact: el pico de sus pasos.
    void draw(const FrameSteps& steps, std::int64_t frame_ns, const FramePacer& pacer,
              const FixedStepClock& clock, const DriverInput& driver, const sim::SimInput& input,
              float frame_impact, PerfCounter& perf);

    // --golden: simula la grabación hasta su paso, dibuja el fotograma con alpha 1 y lo guarda.
    // false con el motivo si la grabación es más corta o no se puede guardar.
    bool draw_golden(const FramePacer& pacer, const FixedStepClock& clock,
                     const DriverInput& driver, PerfCounter& perf, std::string& error);

private:
    // Mundo nuevo, si alguna parte lo ha pedido, y aviso a todas.
    void apply_rebuild();

    DemoContext ctx_;
    const io::Recording* golden_recording_ = nullptr;
    DemoWorld world_;
    DemoZoneModes zone_modes_;
    DemoMeasure measure_;
    DemoDriverFeel driver_feel_;
    DemoCamera camera_;
    DemoScreen screen_;
    // Las partes, en el orden de todas las fases. El mundo interpola las poses y las medidas fijan
    // el recorrido de --bench-zone antes de que la cámara las use; el conductor pone la cabeza
    // antes que la cámara; en el 3D, la escena, el bosque y el interior, por este orden.
    std::array<DemoPart*, 6> parts_{&world_,       &zone_modes_, &measure_,
                                    &driver_feel_, &camera_,     &screen_};
};

} // namespace rally::platform
