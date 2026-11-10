// Tests de muerte (ctest, sin doctest): cada caso tiene que abortar con el mensaje de
// RALLY_CHECK. ctest busca ese mensaje en la salida y falla si aparece NO_ABORT, es decir, si el
// programa sigue tras la llamada prohibida. El caso va en el primer argumento:
//   build_after_seal       añadir cuerpos a un mundo sellado;
//   soft_without_settings  la suspensión blanda con un coche sin [settings] (tarea 2.8);
//   stage_with_zone        tramo y zona de pruebas a la vez (tramo-fisica);
//   bad_stage              un texto de tramo que no vale;
//   variant_without_stage  una variante de medida del suelo del tramo sin tramo.

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
    } else if (std::strcmp(argv[1], "stage_with_zone") == 0) {
        // Tramo y zona de pruebas a la vez (tramo-fisica).
        rally::sim::WorldDesc desc;
        desc.test_zone = true;
        desc.stage_text = "format = 1\n";
        world.build(desc);
    } else if (std::strcmp(argv[1], "bad_stage") == 0) {
        // Un texto de tramo que no vale: quien llama lo tenía que leer antes.
        rally::sim::WorldDesc desc;
        desc.stage_text = "format = 1\n";
        world.build(desc);
    } else if (std::strcmp(argv[1], "variant_without_stage") == 0) {
        // Una variante de medida del suelo del tramo sin tramo.
        rally::sim::WorldDesc desc;
        desc.stage_variant = rally::sim::StageGroundVariant::DenseMesh;
        world.build(desc);
    } else {
        std::puts("caso desconocido");
        return 2;
    }
    std::puts("NO_ABORT");
    return 0;
}
