#pragma once

#include "platform/demo_part.h"

namespace rally::platform {

// Cámara de la demo. Tab cambia la vista: ojo del conductor (el del archivo del coche más el
// asiento del jugador, platform/seat; con F9, la cabeza que se mueve con el coche,
// platform/head_motion), persecución y fija en el recinto o toda la zona. Re Pág / Av Pág,
// Inicio / Fin e Insert / Supr mueven el ojo 1 cm (se guarda en ajustes.txt). Render relativo a
// la cámara; J dibuja en float del mundo para ver por contraste el temblor lejos del origen.
class DemoCamera final : public DemoPart {
public:
    // Planos de recorte de raylib (los fija el compositor al empezar, con la ventana abierta).
    static constexpr double kNearPlane = 0.05;
    static constexpr double kFarPlane = 2000.0;

    void handle_keys(DemoContext& ctx, FramePacer& pacer, PerfCounter& perf) override;
    void begin_frame(DemoContext& ctx, const DemoFrame& frame) override;
    // La cámara del fotograma (ctx.frame_camera) según ctx.view, ctx.poses.drawn_car y ctx.mode.
    // La usa también la ventana del copiloto (platform/copilot_app), que no tiene partes.
    void update(DemoContext& ctx);
};

} // namespace rally::platform
