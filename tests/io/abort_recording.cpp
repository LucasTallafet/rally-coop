// Test de muerte (ctest, sin doctest): graba como la partida, salta un RALLY_CHECK de verdad
// y su manejador guarda la grabación incompleta, como el de la partida (src/main.cpp).
// Después, otro test de ctest la repite con rally_replay (tests/CMakeLists.txt).
//   rally_abort_recording <archivo.rrec>
// Escribe NO_ABORT si el programa sigue tras la llamada prohibida.

#include "car_worlds.h"
#include "io/crash_save.h"
#include "io/recording.h"
#include "sim/check.h"
#include "sim/demo_world.h"
#include "sim/jolt_runtime.h"
#include "sim/pcg32.h"
#include "sim/sim_world.h"

#include <cstdio>

namespace {

// Pasos grabados antes del aborto (lo comprueba la expresión del test de rally_replay).
constexpr int kStepsBeforeAbort = 600;

} // namespace

int main(int argc, char** argv) {
    if (argc != 2) {
        std::fputs("uso: rally_abort_recording <archivo.rrec>\n", stderr);
        return 2;
    }
    rally::sim::set_check_handler([](const char*) { rally::io::save_crash_recording(); });

    const rally::sim::JoltRuntime runtime;
    const rally::sim::WorldDesc desc = rally::sim::demo_arena(0.0, rally::tests::reference_car());
    rally::sim::SimWorld world;
    world.build(desc);
    world.seal();

    rally::io::RecordingHeader header;
    header.commit = "test";
    header.world = rally::io::serialize_world(desc);
    header.car_text = rally::tests::reference_car_text();
    rally::io::Recorder recorder;
    recorder.begin(header, kStepsBeforeAbort);
    rally::io::set_crash_recording(&recorder, argv[1]);

    rally::sim::Pcg32 rng(0xab07'0000'0000'0001ull, 3u);
    rally::sim::SimInput input;
    for (int i = 0; i < kStepsBeforeAbort; ++i) {
        if (i % 4 == 0) {
            input.steer = static_cast<std::int16_t>(static_cast<int>(rng.bounded(65535)) - 32767);
            input.throttle = static_cast<std::int16_t>(rng.bounded(32768));
            input.brake = static_cast<std::int16_t>(rng.bounded(4) == 0 ? rng.bounded(32768) : 0);
        }
        world.step(input);
        recorder.append(input);
    }

    // Llamada prohibida: añadir cuerpos a un mundo sellado.
    world.build(desc);
    std::puts("NO_ABORT");
    return 0;
}
