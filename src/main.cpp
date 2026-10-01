#include "platform/fatal_error.h"
#include "platform/fixed_step_clock.h"
#include "platform/frame_pacer.h"
#include "platform/loop_demo.h"
#include "platform/window.h"
#include "sim/jolt_runtime.h"

#include <cstdint>

// Piden la gráfica dedicada en PCs con dos gráficas (AMD PowerXpress y NVIDIA Optimus).
// Sin ellas, OpenGL puede elegir la integrada aunque el monitor esté en la dedicada.
// Los controladores solo buscan estos símbolos entre las exportaciones del .exe: en una
// biblioteca estática el enlazador los descartaría al no usarse, y en una DLL no se miran.
// El tipo es DWORD (unsigned long) sin incluir <windows.h>.
extern "C" {
__declspec(dllexport) unsigned long AmdPowerXpressRequestHighPerformance = 1;
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
}

int main() {
    if (!rally::sim::jolt_abi_matches()) {
        rally::platform::show_fatal_error(
            "Jolt se compiló con definiciones distintas a las del juego.");
        return 1;
    }

    rally::platform::Window window(1280, 720, "Rally Coop");
    rally::platform::FramePacer pacer;
    rally::platform::FixedStepClock clock;
    rally::platform::LoopDemo demo;

    while (!window.should_close()) {
        const std::int64_t frame_ns = pacer.begin_frame();
        const rally::platform::FrameSteps steps = clock.advance(frame_ns);

        demo.handle_keys(pacer);
        const rally::platform::DemoInput input = demo.read_input(); // una vez por fotograma
        for (int i = 0; i < steps.count; ++i) {
            demo.step(input);
        }
        demo.draw(steps, frame_ns, pacer); // presenta y recoge los eventos del próximo fotograma
    }
    return 0;
}
