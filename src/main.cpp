#include "platform/box_demo.h"
#include "platform/fatal_error.h"
#include "platform/fixed_step_clock.h"
#include "platform/frame_pacer.h"
#include "platform/input.h"
#include "platform/window.h"
#include "sim/check.h"
#include "sim/jolt_runtime.h"

#include <cstdint>
#include <cstring>

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
    rally::sim::set_check_handler(&rally::platform::show_fatal_error);

    const rally::sim::JoltRuntime jolt;
    rally::platform::Window window(1280, 720, "Rally Coop");
    rally::platform::FramePacer pacer;
    rally::platform::FixedStepClock clock;
    rally::platform::BoxDemo demo;
    rally::platform::KeyboardInput keyboard;
    for (int i = 1; i < argc; ++i) {
        // Diagnóstico: registro de fotogramas y arranque en pantalla completa.
        if (std::strcmp(argv[i], "--frame-log") == 0) {
            demo.start_frame_log();
        } else if (std::strcmp(argv[i], "--fullscreen") == 0) {
            window.toggle_fullscreen();
        }
    }

    while (!window.should_close()) {
        const std::int64_t frame_ns = pacer.begin_frame();
        // Ajuste del dt al refresco del monitor (N lo desactiva en la demo, para comparar).
        clock.set_snap_period(demo.snap_enabled() ? pacer.snap_period() : 0);
        const rally::platform::FrameSteps steps = clock.advance(frame_ns);

        window.handle_keys();
        demo.handle_keys(pacer);
        // La entrada se lee una vez por fotograma y vale para todos sus pasos.
        const rally::sim::SimInput input = keyboard.read(steps.count);
        for (int i = 0; i < steps.count; ++i) {
            demo.step(input);
        }
        // Presenta y recoge los eventos del próximo fotograma.
        demo.draw(steps, frame_ns, pacer, clock);
    }
    return 0;
}
