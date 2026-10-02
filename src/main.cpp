#include "io/crash_save.h"
#include "platform/box_demo.h"
#include "platform/fatal_error.h"
#include "platform/fixed_step_clock.h"
#include "platform/forest_bench.h"
#include "platform/frame_pacer.h"
#include "platform/input.h"
#include "platform/input_shaping.h"
#include "platform/perf_counter.h"
#include "platform/window.h"
#include "sim/check.h"
#include "sim/jolt_runtime.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <optional>

// Piden la gráfica dedicada en PCs con dos gráficas (AMD PowerXpress y NVIDIA Optimus).
// Sin ellas, OpenGL puede elegir la integrada aunque el monitor esté en la dedicada.
// Los controladores solo buscan estos símbolos entre las exportaciones del .exe: en una
// biblioteca estática el enlazador los descartaría al no usarse, y en una DLL no se miran.
// El tipo es DWORD (unsigned long) sin incluir <windows.h>.
extern "C" {
__declspec(dllexport) unsigned long AmdPowerXpressRequestHighPerformance = 1;
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
}

int main(int argc, char** argv) {
    if (!rally::sim::jolt_abi_matches()) {
        rally::platform::show_fatal_error(
            "Jolt se compiló con definiciones distintas a las del juego.");
        return 1;
    }
    // Si salta un RALLY_CHECK, primero se guarda la grabación en curso (incompleta) y después
    // se muestra el error.
    rally::sim::set_check_handler([](const char* message) {
        rally::io::save_crash_recording();
        rally::platform::show_fatal_error(message);
    });

    const rally::sim::JoltRuntime jolt;
    rally::platform::Window window(1280, 720, "Rally Coop");
    rally::platform::FramePacer pacer;
    rally::platform::FixedStepClock clock;
    rally::platform::PerfCounter perf;
    rally::platform::BoxDemo demo;
    rally::platform::DriverInput driver;
    std::optional<rally::platform::ForestBench> bench;
    for (int i = 1; i < argc; ++i) {
        // Diagnóstico: registro de fotogramas, arranque en pantalla completa y medida del
        // bosque denso a 1920 × 1080 en pantalla completa sin bordes y sin sincronización
        // vertical, que deja a la GPU en un reloj bajo (se cierra al acabar).
        if (std::strcmp(argv[i], "--frame-log") == 0) {
            demo.start_frame_log();
        } else if (std::strcmp(argv[i], "--fullscreen") == 0) {
            window.toggle_fullscreen();
        } else if (std::strcmp(argv[i], "--bench-forest") == 0 && !bench) {
            window.toggle_fullscreen();
            demo.set_render_size(1920, 1080);
            pacer.set_vsync(false);
            perf.set_visible(true);
            bench.emplace(demo);
        }
    }

    while (!window.should_close()) {
        const std::int64_t frame_ns = pacer.begin_frame();
        perf.begin_frame(frame_ns, pacer.last_wait_ns());
        if (bench && !bench->update(perf, pacer, demo)) {
            break;
        }
        // Ajuste del dt al refresco del monitor (N lo desactiva en la demo, para comparar).
        clock.set_snap_period(demo.snap_enabled() ? pacer.snap_period() : 0);
        const rally::platform::FrameSteps steps = clock.advance(frame_ns);
        perf.record_steps(frame_ns, steps);

        window.handle_keys();
        perf.handle_keys();
        demo.handle_keys(pacer);
        // La entrada se lee una vez por fotograma y vale para todos sus pasos.
        const rally::sim::SimInput input = driver.read(steps.count);
        float frame_impact = 0.0f;
        demo.prepare_steps(steps.count);
        for (int i = 0; i < steps.count; ++i) {
            // El contador solo mide alrededor del paso: no toca la simulación.
            perf.begin_step();
            frame_impact = std::max(frame_impact, demo.step(input));
            perf.end_step();
        }
        // Vibración por el pico de impacto de los pasos del fotograma.
        driver.gamepads().rumble(rally::platform::rumble_for_impact(frame_impact));
        // Presenta y recoge los eventos del próximo fotograma.
        demo.draw(steps, frame_ns, pacer, clock, driver, input, frame_impact, perf);
    }
    return 0;
}
