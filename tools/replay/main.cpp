// rally_replay: repite una grabación (.rrec) sin ventana, comprueba el hash final y mide.
//   rally_replay info <archivo>              cabecera y pie
//   rally_replay run <archivo> [--bench N]   repite y dice "hash OK" o el primer problema;
//                                            con --bench, N repeticiones y ms por paso
//   rally_replay rewrite <archivo>           pie con la versión actual (solo tests)
//   rally_replay zone --json <archivo> [--blind <superficie>]
//                                            trazado de la zona de pruebas en JSON
// Códigos de salida: 0 bien, 1 el hash no cuadra, 2 error de uso o de archivo.

#include "io/build_info.h"
#include "io/recording.h"
#include "io/replay.h"
#include "sim/jolt_runtime.h"
#include "sim/sim_version.h"
#include "sim/sim_world.h"
#include "sim/surface.h"
#include "sim/test_zone.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

namespace {

using rally::io::Recording;

int usage() {
    std::fputs("uso:\n"
               "  rally_replay info <archivo.rrec>\n"
               "  rally_replay run <archivo.rrec> [--bench N]\n"
               "  rally_replay rewrite <archivo.rrec>\n"
               "  rally_replay zone --json <archivo.json> [--blind <superficie>]\n",
               stderr);
    return 2;
}

void print_timed(const char* label, std::int64_t steps) {
    if (steps < 0) {
        std::printf("%s ninguno\n", label);
    } else {
        std::printf("%s %" PRId64 " pasos (%.3f s)\n", label, steps,
                    static_cast<double>(steps) / rally::sim::kStepsPerSecond);
    }
}

bool load(const char* path, std::string& bytes, Recording& recording) {
    std::string error;
    if (!rally::io::read_file(path, bytes, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return false;
    }
    rally::io::ParseResult parsed = rally::io::parse(bytes);
    if (!parsed.ok) {
        std::fprintf(stderr, "error: %s: %s\n", path, parsed.error.c_str());
        return false;
    }
    recording = std::move(parsed.recording);
    return true;
}

int info(const char* path) {
    std::string bytes;
    Recording recording;
    if (!load(path, bytes, recording)) {
        return 2;
    }
    const rally::io::RecordingHeader& h = recording.header;
    const rally::io::RecordingFooter& f = recording.footer;
    rally::sim::WorldDesc desc;
    std::string error;
    const bool world_ok = rally::io::parse_world(h.world, desc, error, h.format_version);
    const double raw_bytes = static_cast<double>(f.total_steps) * sizeof(rally::sim::SimInput);
    std::printf("archivo:             %s (%zu bytes)\n", path, bytes.size());
    std::printf("formato:             %u\n", h.format_version);
    std::printf("versión simulación:  %u (esta: %u)%s\n", h.sim_version, rally::sim::kSimVersion,
                h.sim_version != rally::sim::kSimVersion ? "  DISTINTA" : "");
    std::printf("commit:              %s (este: %s)\n", h.commit.c_str(),
                rally::io::build_commit());
    if (world_ok) {
        std::printf("mundo:               %zu cuerpos, controlado %d\n", desc.bodies.size(),
                    desc.controlled);
        if (desc.test_zone) {
            // La superficie real: en las pruebas a ciegas solo se ve aquí.
            std::printf("zona de pruebas:     sí, pista ciega de %s\n",
                        rally::sim::surface_name(desc.blind_surface));
        } else {
            std::printf("zona de pruebas:     no\n");
        }
    } else {
        std::printf("mundo:               no válido (%s)\n", error.c_str());
    }
    std::printf("coche:               %zu bytes\n", h.car_text.size());
    std::printf("ajustes:             %zu bytes\n", h.settings.size());
    std::printf("tramos:              %zu\n", recording.runs.size());
    std::printf("pasos:               %" PRIu64 " (%.2f s)\n", f.total_steps,
                static_cast<double>(f.total_steps) / rally::sim::kStepsPerSecond);
    std::printf("compresión:          %.2fx frente a %zu B por paso\n",
                bytes.empty() ? 0.0 : raw_bytes / static_cast<double>(bytes.size()),
                sizeof(rally::sim::SimInput));
    if (f.complete) {
        std::printf("hash final:          0x%016" PRIx64 "\n", f.final_hash);
    } else {
        std::printf("hash final:          ninguno (grabación incompleta: se guardó al abortar)\n");
    }
    print_timed("tiempo cronometrado:", f.timed_steps);
    return 0;
}

struct BenchStats {
    double mean_ms = 0.0;
    double p99_ms = 0.0;
    double max_ms = 0.0;
};

// N repeticiones midiendo cada paso. Los tiempos se guardan en memoria reservada antes.
bool bench(const Recording& recording, int repetitions, BenchStats& stats) {
    using Clock = std::chrono::steady_clock;
    std::vector<double> step_ms;
    step_ms.reserve(static_cast<std::size_t>(recording.footer.total_steps) *
                    static_cast<std::size_t>(repetitions));
    for (int rep = 0; rep < repetitions; ++rep) {
        rally::sim::SimWorld world;
        std::string error;
        if (!rally::io::build_world(recording, world, error)) {
            std::fprintf(stderr, "error: %s\n", error.c_str());
            return false;
        }
        for (const rally::io::InputRun& run : recording.runs) {
            for (std::uint32_t i = 0; i < run.count; ++i) {
                const Clock::time_point start = Clock::now();
                world.step(run.input);
                const Clock::time_point end = Clock::now();
                step_ms.push_back(std::chrono::duration<double, std::milli>(end - start).count());
            }
        }
    }
    if (step_ms.empty()) {
        return true;
    }
    double total = 0.0;
    for (const double ms : step_ms) {
        total += ms;
    }
    stats.mean_ms = total / static_cast<double>(step_ms.size());
    std::sort(step_ms.begin(), step_ms.end());
    const std::size_t p99 =
        std::min(step_ms.size() - 1, static_cast<std::size_t>(0.99 * step_ms.size()));
    stats.p99_ms = step_ms[p99];
    stats.max_ms = step_ms.back();
    return true;
}

int run(const char* path, int bench_repetitions) {
    std::string bytes;
    Recording recording;
    if (!load(path, bytes, recording)) {
        return 2;
    }
    const rally::io::ReplayResult result = rally::io::replay(recording);
    if (!result.ran) {
        std::fprintf(stderr, "error: %s\n", result.problem.c_str());
        return 2;
    }
    std::printf("pasos: %" PRIu64 " (%.2f s)   hash: 0x%016" PRIx64 "\n", result.steps,
                static_cast<double>(result.steps) / rally::sim::kStepsPerSecond, result.hash);
    print_timed("vuelta:", result.timed_steps);
    int code = 0;
    if (result.hash_ok) {
        std::puts("hash OK");
    } else if (result.incomplete || result.version_mismatch) {
        std::printf("aviso: %s\n", result.problem.c_str());
    } else {
        std::printf("FALLO: %s\n", result.problem.c_str());
        code = 1;
    }
    if (bench_repetitions > 0) {
#ifndef NDEBUG
        std::puts("aviso: build de Debug; las cifras de Release son las que cuentan");
#endif
        BenchStats stats;
        if (!bench(recording, bench_repetitions, stats)) {
            return 2;
        }
        std::printf("bench: %d repeticiones, ms por paso: media %.4f   p99 %.4f   máximo %.4f\n",
                    bench_repetitions, stats.mean_ms, stats.p99_ms, stats.max_ms);
    }
    return code;
}

int rewrite(const char* path) {
    std::string bytes;
    Recording recording;
    if (!load(path, bytes, recording)) {
        return 2;
    }
    std::puts("aviso: rewrite es solo para grabaciones sintéticas de tests; una grabación "
              "jugada de otra versión no tiene por qué dar el mismo recorrido");
    const rally::io::ReplayResult result = rally::io::rewrite_footer(recording);
    if (!result.hash_ok) {
        std::fprintf(stderr, "error: %s\n", result.problem.c_str());
        return 2;
    }
    std::string error;
    if (!rally::io::write_file(path, rally::io::serialize(recording), error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 2;
    }
    std::printf("reescrito: versión %u, %" PRIu64 " pasos, hash 0x%016" PRIx64 "\n",
                recording.header.sim_version, recording.footer.total_steps,
                recording.footer.final_hash);
    return 0;
}

// Trazado de la zona de pruebas en JSON. Sin --blind, la pista ciega es de grava.
int zone_json(const char* path, const char* blind) {
    rally::sim::Surface surface = rally::sim::Surface::Gravel;
    if (blind != nullptr) {
        bool found = false;
        for (int s = 0; s < rally::sim::kSurfaceCount; ++s) {
            const auto candidate = static_cast<rally::sim::Surface>(s);
            if (std::strcmp(blind, rally::sim::surface_key(candidate)) == 0) {
                surface = candidate;
                found = true;
            }
        }
        if (!found) {
            std::fprintf(stderr, "error: superficie desconocida: %s\n", blind);
            return 2;
        }
    }
    const rally::sim::TestZone zone(surface);
    std::string error;
    if (!rally::io::write_file(path, zone.to_json(), error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 2;
    }
    std::printf("zona escrita: %s (pista ciega de %s)\n", path, rally::sim::surface_name(surface));
    return 0;
}

} // namespace

int main(int argc, char** argv) {
    // Los mensajes van en UTF-8 (tildes): la consola de Windows los muestra así.
    SetConsoleOutputCP(CP_UTF8);
    if (!rally::sim::jolt_abi_matches()) {
        std::fputs("error: Jolt se compiló con definiciones distintas a las del juego\n", stderr);
        return 2;
    }
    if (argc < 3) {
        return usage();
    }
    const rally::sim::JoltRuntime jolt;
    const char* command = argv[1];
    if (std::strcmp(command, "zone") == 0) {
        if (argc == 4 && std::strcmp(argv[2], "--json") == 0) {
            return zone_json(argv[3], nullptr);
        }
        if (argc == 6 && std::strcmp(argv[2], "--json") == 0 &&
            std::strcmp(argv[4], "--blind") == 0) {
            return zone_json(argv[3], argv[5]);
        }
        return usage();
    }
    const char* path = argv[2];
    if (std::strcmp(command, "info") == 0 && argc == 3) {
        return info(path);
    }
    if (std::strcmp(command, "rewrite") == 0 && argc == 3) {
        return rewrite(path);
    }
    if (std::strcmp(command, "run") == 0) {
        int repetitions = 0;
        if (argc == 5 && std::strcmp(argv[3], "--bench") == 0) {
            const char* text = argv[4];
            const std::from_chars_result parsed =
                std::from_chars(text, text + std::strlen(text), repetitions);
            if (parsed.ec != std::errc{} || *parsed.ptr != '\0' || repetitions < 1) {
                return usage();
            }
        } else if (argc != 3) {
            return usage();
        }
        return run(path, repetitions);
    }
    return usage();
}
