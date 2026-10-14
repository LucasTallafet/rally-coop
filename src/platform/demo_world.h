#pragma once

#include "io/recording.h"
#include "platform/demo_part.h"
#include "platform/recording_session.h"
#include "platform/zone_draw.h"

#include <string>

namespace rally::platform {

// Lee y comprueba la grabación de --golden; false con el motivo si no vale.
bool read_golden_recording(const std::string& path, io::Recording& recording, std::string& error);

// Archivo del coche por defecto: data/cars/mid_awd_turbo.car, buscado desde la carpeta del exe
// hacia arriba (así F5 relee el del repositorio). Vacío si no está.
std::string default_car_path();

// Mundo y grabación de la demo: la descripción según el modo (zona o recinto), la construcción y
// el reinicio (cada mundo nuevo cierra la grabación de la partida y empieza otra,
// grabaciones/AAAAMMDD-HHMMSS.rrec junto al exe), el coche (F5 lo relee, F8 pasa a la
// configuración siguiente; si el archivo tiene un error, lo dice con su línea y sigue el
// anterior), las ayudas que cambian la simulación (F6, F2), la dirección con ratón (F7), los
// ajustes del jugador (ajustes.txt junto al exe, tras 0,5 s sin cambios y al salir) y el dibujo
// de la escena (suelo de la zona o recinto, cuerpos y coche).
class DemoWorld final : public DemoPart {
public:
    // Lee el coche (aborta con el motivo si no se puede) y ajustes.txt.
    DemoWorld(DemoContext& ctx, std::string car_path);
    // --golden: el coche y la descripción de la grabación; ajustes por defecto, sin grabar.
    DemoWorld(DemoContext& ctx, const io::Recording& recording);
    ~DemoWorld() override;
    DemoWorld(const DemoWorld&) = delete;
    DemoWorld& operator=(const DemoWorld&) = delete;

    // Descripción del mundo según el modo y los ajustes.
    void describe(DemoContext& ctx) const;
    // Termina la grabación del mundo actual y construye uno nuevo con su grabación.
    void reset(DemoContext& ctx);
    // Antes de los pasos del fotograma: deja sitio en la grabación (fuera del paso).
    void prepare_steps(DemoContext& ctx, int count);
    // Un paso de 1/240 s, grabado (no en pausa: lo mira el compositor).
    void step(DemoContext& ctx, const sim::SimInput& input);

    void handle_keys(DemoContext& ctx, FramePacer& pacer, PerfCounter& perf) override;
    void begin_frame(DemoContext& ctx, const DemoFrame& frame) override;
    void draw_3d(DemoContext& ctx, const DemoFrame& frame) override;

private:
    void reload_car(DemoContext& ctx);
    void next_car(DemoContext& ctx);
    bool load_car(DemoContext& ctx, const std::string& path, const char* key);
    void load_settings(DemoContext& ctx);
    void save_settings(DemoContext& ctx);
    void read_poses(DemoContext& ctx);

    DemoContext& ctx_;
    std::string car_text_;      // va a la cabecera de las grabaciones
    std::string settings_path_; // UTF-8
    ZoneDraw zone_draw_;
    RecordingSession recording_;
};

} // namespace rally::platform
