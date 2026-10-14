#pragma once

#include "platform/camera_space.h"
#include "platform/forest_scene.h"
#include "platform/head_motion.h"
#include "platform/interpolation.h"
#include "platform/overlay.h"
#include "platform/player_settings.h"
#include "sim/car_data.h"
#include "sim/sim_input.h"
#include "sim/sim_world.h"
#include "sim/surface.h"

#include <raylib.h>

#include <array>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace rally::platform {

class AudioOutput;
class DriverInput;
class FixedStepClock;
class FrameLog;
class FramePacer;
class Gamepads;
class PerfCounter;
class RecordingSession;
class ZoneDraw;
struct FrameSteps;

// --golden (tarea P1a): un fotograma de referencia sacado de una grabación, para comprobar que
// una reestructuración no cambia el dibujo (tools/ci/golden.ps1, tests/platform/golden/).
struct GoldenFrame {
    std::string recording;  // .rrec (UTF-8)
    std::uint64_t step = 0; // pasos simulados antes del fotograma
    std::string output;     // .png (UTF-8)
    int view = 0;           // Tab: 0 ojo del conductor, 1 persecución, 2 fija o toda la zona
    int help_page = -1;     // H: página de la ayuda (-1 oculta)
    bool debug = false;     // F3
    int graphs = 0;         // T: veces pulsada (0: ocultas)
};

// Qué mundo se describe. Lo escriben los modos de la zona (y las medidas y la línea de órdenes);
// lo leen el mundo (la descripción), la cámara, el conductor y la pantalla.
struct WorldMode {
    bool zone = true;                                        // K: zona de pruebas o recinto
    bool blind = false;                                      // M: modo a ciegas
    int blind_trial = 0;                                     // prueba en curso (0..14)
    sim::Surface blind_trial_surface = sim::Surface::Gravel; // la de esa prueba
    sim::Surface blind_surface = sim::Surface::Gravel;       // G: superficie de la pista ciega
    sim::Surface road_surface = sim::Surface::Gravel;        // U: calzada de la carretera
    bool road_start = false;      // Y: el coche sale en la carretera de prueba, no en la recta
    bool far_from_origin = false; // O: recinto a 20 km del origen en x y z
    bool hide_config = false;     // --hide-config
    // La configuración no se dice en pantalla: a ciegas o con --hide-config.
    bool config_hidden() const { return hide_config || blind; }
};

// Reconstrucción pedida por una parte: el compositor la hace una vez, tras las teclas.
enum class Rebuild : std::uint8_t {
    None,
    Reset,    // mundo nuevo con la descripción actual
    Describe, // describir otra vez (modo, superficies, ayudas) y mundo nuevo
};

// Poses por cuerpo (en el orden de la descripción), del coche y de sus ruedas: en el paso
// anterior, en el último y en el fotograma. Las escribe el mundo; las leen la cámara, el
// conductor y la pantalla.
struct ScenePoses {
    std::vector<Pose> previous;
    std::vector<Pose> current;
    std::vector<Pose> drawn;
    Pose previous_car;
    Pose current_car;
    Pose drawn_car;
    std::array<sim::SimWorld::WheelPose, sim::kWheelCount> previous_wheels{};
    std::array<sim::SimWorld::WheelPose, sim::kWheelCount> current_wheels{};
    std::array<sim::SimWorld::WheelPose, sim::kWheelCount> drawn_wheels{};
};

// La cámara del fotograma (la escribe la cámara; la leen el dibujo y la pantalla).
struct FrameCamera {
    Vec3d eye;
    Vec3d forward;
    Vec3d up{0.0, 1.0, 0.0};
    Vec3d origin; // render relativo: el ojo (o 0 con J)
    Camera3D camera{};
};

// Lo que comparten dos o más partes de la demo. Lo de una sola parte se queda en ella. Cada
// grupo dice quién lo escribe.
struct DemoContext {
    explicit DemoContext(const GoldenFrame* golden_frame = nullptr) : golden(golden_frame) {}

    // Mundo y coche (demo_world).
    std::unique_ptr<sim::SimWorld> world;
    sim::WorldDesc desc;
    sim::CarData car;
    std::string car_path; // archivo del coche (o la grabación, con --golden)
    const RecordingSession* recording = nullptr;
    ZoneDraw* zone_draw = nullptr; // el suelo de la zona (la pantalla y las medidas leen su CPU)
    ScenePoses poses;
    // Pulsaciones vistas por la simulación desde el último reinicio (para comprobar botones).
    int shift_up_presses = 0;
    int shift_down_presses = 0;
    int reset_presses = 0;

    // Qué mundo y reconstrucción pedida (cualquier parte).
    WorldMode mode;
    Rebuild rebuild = Rebuild::None;

    // Ajustes del jugador (los lee y guarda demo_world; los cambian el mundo, la cámara y el
    // conductor) y mensajes en pantalla (todas; los dibuja la pantalla).
    PlayerSettings settings;
    SettingsSaver settings_saver;
    Overlay overlay;

    // Medidas (demo_measure).
    bool paused = false;     // P: congela la simulación
    bool interpolate = true; // I
    bool snap = true;        // N: ajuste del dt al refresco
    bool zone_bench = false; // --bench-zone y --bench-road
    ForestMode forest_mode = ForestMode::Off;
    const FrameLog* frame_log = nullptr;
    RenderTexture2D target{}; // id 0 si se dibuja directamente en la ventana
    // --bench-zone: el ojo y la mirada del recorrido, en vez de los de la vista.
    std::optional<std::pair<Vec3d, Vec3d>> camera_override;

    // Cámara (demo_camera; K vuelve al ojo).
    int view = 0;             // Tab: 0 ojo del conductor, 1 persecución, 2 fija o toda la zona
    bool world_float = false; // J: dibujo en float del mundo en vez de relativo
    FrameCamera frame_camera;

    // Fotograma (demo_world): se interpola si no hay pausa y la interpolación está activa.
    bool blend = false;

    // Conductor en el coche (demo_driver_feel).
    std::optional<HeadOffset> head;  // la cabeza del fotograma (con F9)
    float last_rumble_impact = 0.0f; // último pico que hizo vibrar, en m/s
    const AudioOutput* audio = nullptr;

    // --golden (null en el juego).
    const GoldenFrame* golden = nullptr;

    // Contexto de las teclas (zona, zona a ciegas o recinto; platform/keys) al empezar las
    // teclas del fotograma: lo fija el compositor, así una tecla que cambia el modo no cambia el
    // contexto de las demás del mismo fotograma.
    std::uint8_t frame_key_context = 0;
    std::uint8_t key_context() const;
};

// Lo que llega del bucle en cada fotograma.
struct DemoFrame {
    const FrameSteps& steps;
    std::int64_t frame_ns;
    const FramePacer& pacer;
    const FixedStepClock& clock;
    const DriverInput& driver;
    const sim::SimInput& input;
    float frame_impact; // el pico de impacto de los pasos del fotograma
    PerfCounter& perf;
};

// Una parte de la demo. El compositor (platform/drive_demo) llama a cada fase de todas las
// partes en el orden de su lista. Una parte no llama a otra: lo que comparten va en el contexto,
// y una reconstrucción se pide en DemoContext::rebuild.
class DemoPart {
public:
    virtual ~DemoPart() = default;
    virtual void handle_keys(DemoContext&, FramePacer&, PerfCounter&) {}
    // Tras construir un mundo nuevo (al empezar y en cada reconstrucción).
    virtual void on_world_rebuilt(DemoContext&) {}
    // Tras cada paso simulado (no en pausa).
    virtual void after_step(DemoContext&) {}
    // Tras los pasos del fotograma: vibración y sonido.
    virtual void after_steps(DemoContext&, Gamepads&, float /*frame_impact*/,
                             std::int64_t /*frame_ns*/) {}
    // Antes de dibujar: poses, cámara, medidas.
    virtual void begin_frame(DemoContext&, const DemoFrame&) {}
    // Dentro de BeginMode3D, con la cámara del fotograma.
    virtual void draw_3d(DemoContext&, const DemoFrame&) {}
    // Texto en pantalla, tras el 3D.
    virtual void draw_2d(DemoContext&, const DemoFrame&) {}
    // Tras los mensajes, antes de presentar (capturas).
    virtual void end_frame(DemoContext&, const DemoFrame&) {}
};

} // namespace rally::platform
