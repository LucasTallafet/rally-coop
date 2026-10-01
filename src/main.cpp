#include "platform/fatal_error.h"
#include "platform/window.h"
#include "sim/jolt_runtime.h"

int main() {
    if (!rally::sim::jolt_abi_matches()) {
        rally::platform::show_fatal_error(
            "Jolt se compiló con definiciones distintas a las del juego.");
        return 1;
    }

    rally::platform::Window window(1280, 720, "Rally Coop");
    while (!window.should_close()) {
        window.draw_empty_frame();
    }
    return 0;
}
