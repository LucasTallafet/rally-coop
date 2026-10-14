#include "io/crash_save.h"
#include "platform/drive_demo.h"
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
#include <charconv>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <string>
#include <system_error>

// Piden la gráfica dedicada en PCs con dos gráficas (AMD PowerXpress y NVIDIA Optimus).
// Sin ellas, OpenGL puede elegir la integrada aunque el monitor esté en la dedicada.
// Los controladores solo buscan estos símbolos entre las exportaciones del .exe: en una
// biblioteca estática el enlazador los descartaría al no usarse, y en una DLL no se miran.
// El tipo es DWORD (unsigned long) sin incluir <windows.h>.
extern "C" {
__declspec(dllexport) unsigned long AmdPowerXpressRequestHighPerformance = 1;
__declspec(dllexport) unsigned long NvOptimusEnablement = 1;
}

namespace {

bool parse_number(const char* text, std::uint64_t& value) {
    const std::from_chars_result parsed = std::from_chars(text, text + std::strlen(text), value);
    return parsed.ec == std::errc{} && *parsed.ptr == '\0';
}

// --golden <grabación> <paso> <salida.png> [--view eye|chase|fixed] [--help-page N] [--debug]
// [--graphs N]. Vacío si no está; false en `ok` si está mal escrito.
std::optional<rally::platform::GoldenFrame> parse_golden(int argc, char** argv, bool& ok) {
    ok = true;
    std::optional<rally::platform::GoldenFrame> golden;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--golden") == 0) {
            if (i + 3 >= argc) {
                ok = false;
                return golden;
            }
            golden.emplace();
            golden->recording = argv[i + 1];
            ok = ok && parse_number(argv[i + 2], golden->step);
            golden->output = argv[i + 3];
            i += 3;
        }
    }
    if (!golden) {
        return golden;
    }
    for (int i = 1; i < argc; ++i) {
        const bool has_value = i + 1 < argc;
        std::uint64_t n = 0;
        if (std::strcmp(argv[i], "--view") == 0 && has_value) {
            const char* view = argv[++i];
            golden->view = std::strcmp(view, "chase") == 0   ? 1
                           : std::strcmp(view, "fixed") == 0 ? 2
                                                             : 0;
            ok = ok && (golden->view != 0 || std::strcmp(view, "eye") == 0);
        } else if (std::strcmp(argv[i], "--help-page") == 0 && has_value) {
            ok = ok && parse_number(argv[++i], n) && n >= 1;
            golden->help_page = static_cast<int>(n) - 1;
        } else if (std::strcmp(argv[i], "--graphs") == 0 && has_value) {
            ok = ok && parse_number(argv[++i], n);
            golden->graphs = static_cast<int>(n);
        } else if (std::strcmp(argv[i], "--debug") == 0) {
            golden->debug = true;
        }
    }
    return golden;
}

} // namespace

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
    // --golden: un fotograma de referencia de una grabación y fuera (tools/ci/golden.ps1).
    // Errores por stderr y código 2: sin ventana de error que pare la comprobación.
    bool golden_ok = true;
    if (const std::optional<rally::platform::GoldenFrame> golden =
            parse_golden(argc, argv, golden_ok);
        golden || !golden_ok) {
        std::string error = "uso: --golden <grabación> <paso> <salida.png> [--view "
                            "eye|chase|fixed] [--help-page N] [--debug] [--graphs N]";
        rally::io::Recording recording;
        if (golden_ok &&
            rally::platform::read_golden_recording(golden->recording, recording, error)) {
            rally::platform::DriveDemo demo(*golden, recording);
            const rally::platform::DriverInput driver;
            if (demo.draw_golden(pacer, clock, driver, perf, error)) {
                return 0;
            }
        }
        std::fprintf(stderr, "--golden: %s\n", error.c_str());
        return 2;
    }
    // --car <archivo> antes de crear la demo; por defecto, el coche del repositorio.
    std::string car_path;
    for (int i = 1; i + 1 < argc; ++i) {
        if (std::strcmp(argv[i], "--car") == 0) {
            car_path = argv[i + 1];
        }
    }
    if (car_path.empty()) {
        car_path = rally::platform::default_car_path();
    }
    rally::platform::DriveDemo demo(car_path);
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
            // El bosque se mide alrededor del recinto de la fase 1.
            window.toggle_fullscreen();
            demo.use_arena();
            demo.set_render_size(1920, 1080);
            pacer.set_vsync(false);
            perf.set_visible(true);
            bench.emplace(demo);
        } else if (std::strcmp(argv[i], "--bench-zone") == 0 && !bench) {
            // Dibujo del suelo de la zona a la frecuencia del monitor, en pantalla completa sin
            // bordes (escribe rendimiento_zona_NNN.txt y se cierra).
            window.toggle_fullscreen();
            perf.set_visible(true);
            demo.start_zone_bench();
        } else if (std::strcmp(argv[i], "--bench-road") == 0 && !bench) {
            // Lo mismo por la carretera de prueba (rendimiento_carretera_NNN.txt).
            window.toggle_fullscreen();
            perf.set_visible(true);
            demo.start_zone_bench(true);
        } else if (std::strcmp(argv[i], "--car") == 0 && i + 1 < argc) {
            ++i; // ya leído
        } else if (std::strcmp(argv[i], "--hide-config") == 0) {
            // La configuración del coche no se dice en pantalla (bloque C de la 2.10).
            demo.set_hide_config(true);
        } else if (std::strcmp(argv[i], "--blind-seed") == 0 && i + 1 < argc) {
            // Semilla de la serie de pruebas a ciegas (M en la zona).
            const char* text = argv[++i];
            std::uint64_t seed = 0;
            const std::from_chars_result parsed =
                std::from_chars(text, text + std::strlen(text), seed);
            if (parsed.ec == std::errc{} && *parsed.ptr == '\0') {
                demo.set_blind_seed(seed);
            }
        }
    }

    while (!window.should_close()) {
        const std::int64_t frame_ns = pacer.begin_frame();
        perf.begin_frame(frame_ns, pacer.last_wait_ns());
        if ((bench && !bench->update(perf, pacer, demo)) || demo.zone_bench_done()) {
            break;
        }
        // Ajuste del dt al refresco del monitor (N lo desactiva en la demo, para comparar).
        clock.set_snap_period(demo.snap_enabled() ? pacer.snap_period() : 0);
        const rally::platform::FrameSteps steps = clock.advance(frame_ns);
        perf.record_steps(frame_ns, steps);

        window.handle_keys();
        perf.handle_keys();
        demo.handle_keys(pacer);
        driver.set_mouse(demo.mouse_steering(), demo.mouse_full_lock_px(),
                         demo.mouse_capture_allowed());
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
        // Vibración (continua y por el pico de impacto de los pasos del fotograma) y sonido.
        demo.update_feedback(driver.gamepads(), frame_impact, frame_ns);
        // Presenta y recoge los eventos del próximo fotograma.
        demo.draw(steps, frame_ns, pacer, clock, driver, input, frame_impact, perf);
    }
    return 0;
}
