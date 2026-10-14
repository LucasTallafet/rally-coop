// Test de muerte (ctest, sin doctest): añadir cuerpos a un mundo sellado tiene que abortar
// con el mensaje de RALLY_CHECK. ctest busca ese mensaje en la salida y falla si aparece
// NO_ABORT, es decir, si el programa sigue tras la llamada prohibida.

#include "sim/jolt_runtime.h"
#include "sim/sim_world.h"

#include <cstdio>

int main() {
    const rally::sim::JoltRuntime runtime;
    rally::sim::SimWorld world;
    world.build(rally::sim::WorldDesc{});
    world.seal();
    world.build(rally::sim::WorldDesc{});
    std::puts("NO_ABORT");
    return 0;
}
