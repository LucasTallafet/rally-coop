// Tests de muerte (ctest, sin doctest): cada caso tiene que abortar con el mensaje de
// RALLY_CHECK. ctest busca ese mensaje en la salida y falla si aparece NO_ABORT, es decir, si el
// programa sigue tras la llamada prohibida. El caso va en el primer argumento:
//   build_after_seal       añadir cuerpos a un mundo sellado;
//   soft_without_settings  la suspensión blanda con un coche sin [settings] (tarea 2.8).

#include "sim/jolt_runtime.h"
#include "sim/sim_world.h"

#include <cstdio>
#include <cstring>

int main(int argc, char** argv) {
    if (argc < 2) {
        std::puts("uso: rally_check_abort build_after_seal | soft_without_settings");
        return 2;
    }
    const rally::sim::JoltRuntime runtime;
    rally::sim::SimWorld world;
    if (std::strcmp(argv[1], "build_after_seal") == 0) {
        world.build(rally::sim::WorldDesc{});
        world.seal();
        world.build(rally::sim::WorldDesc{});
    } else if (std::strcmp(argv[1], "soft_without_settings") == 0) {
        rally::sim::WorldDesc desc;
        desc.car = true;
        desc.car_data.mass = 1100.0f; // con datos, pero sin [settings]
        desc.aids.suspension = rally::sim::SuspensionSetting::Soft;
        world.build(desc);
    } else {
        std::puts("caso desconocido");
        return 2;
    }
    std::puts("NO_ABORT");
    return 0;
}
