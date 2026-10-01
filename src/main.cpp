#include "platform/window.h"
#include "sim/jolt_runtime.h"

#include <cstdio>

int main() {
    if (!rally::sim::jolt_abi_matches()) {
        std::fprintf(stderr, "Jolt se compiló con definiciones distintas a las del juego.\n");
        return 1;
    }

    rally::platform::Window window(1280, 720, "Rally Coop");
    while (!window.should_close()) {
        window.draw_empty_frame();
    }
    return 0;
}
