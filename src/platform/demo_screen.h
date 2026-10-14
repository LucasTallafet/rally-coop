#pragma once

#include "platform/demo_part.h"
#include "platform/telemetry_history.h"

namespace rally::platform {

// Texto en pantalla de la demo (platform/overlay, cada capa en su sitio fijo): el indicador
// mínimo y «H: ayuda» siempre; H la ayuda de teclas por páginas, F3 el panel de depuración, T las
// gráficas de la telemetría (no en el modo a ciegas; la historia se llena siempre) y F1 el
// contador de rendimiento (PerfCounter). Mientras se ve la ayuda, los paneles de arriba no se
// dibujan: siguen encendidos y vuelven al cerrarla.
class DemoScreen final : public DemoPart {
public:
    // Con --golden, la ayuda, el panel y las gráficas de la petición.
    explicit DemoScreen(const DemoContext& ctx);

    void handle_keys(DemoContext& ctx, FramePacer& pacer, PerfCounter& perf) override;
    void on_world_rebuilt(DemoContext& ctx) override;
    void after_step(DemoContext& ctx) override;
    void draw_2d(DemoContext& ctx, const DemoFrame& frame) override;

private:
    void draw_indicator(DemoContext& ctx, const DemoFrame& frame);
    void draw_help(DemoContext& ctx);
    void draw_debug(DemoContext& ctx, const DemoFrame& frame);

    int help_page_ = -1;         // H: página de la ayuda de teclas (-1 oculta)
    bool debug_visible_ = false; // F3: panel de depuración
    TelemetryHistory telemetry_history_;
    GraphPager graphs_;
};

} // namespace rally::platform
