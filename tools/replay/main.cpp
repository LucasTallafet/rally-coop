// rally_replay: repite una grabación (.rrec) sin ventana, comprueba el hash final y mide.
//   rally_replay info <archivo>              cabecera y pie; las ayudas de la simulación y los
//                                            ajustes de la plataforma al empezar la grabación
//   rally_replay run <archivo> [--telemetry <salida.csv>] [--bench N]
//                                            repite y dice "hash OK" o el primer problema;
//                                            con --telemetry, CSV por paso; con --bench, N
//                                            repeticiones y ms por paso, sin y con la captura
//                                            de la telemetría
//   rally_replay rewrite <archivo>           pie y formato actuales (solo tests)
//   rally_replay synth <salida.rrec> --car <archivo.car>
//                                            120 s de entradas sintéticas deterministas en la
//                                            zona (tests/data/recordings/bench_lap.rrec)
//   rally_replay zone --json <archivo> [--blind <superficie>] [--road <gravel|asphalt>]
//                                            trazado de la zona de pruebas en JSON
//   rally_replay columns --md [<archivo>]    tabla de columnas de la telemetría en Markdown
//                                            (tools/telemetry/columns.md); sin archivo, a la
//                                            salida estándar
//   rally_replay tire-curves <archivo.car> [--csv <salida.csv>]
//                                            resumen del neumático por superficie; con --csv,
//                                            las curvas (tools/telemetry/tire_curves.py)
// Códigos de salida: 0 bien, 1 el hash no cuadra, 2 error de uso o de archivo.

#include "io/build_info.h"
#include "io/recording.h"
#include "io/replay.h"
#include "io/telemetry_csv.h"
#include "sim/car_data.h"
#include "sim/demo_world.h"
#include "sim/jolt_runtime.h"
#include "sim/pcg32.h"
#include "sim/sim_version.h"
#include "sim/sim_world.h"
#include "sim/surface.h"
#include "sim/test_zone.h"
#include "sim/tire.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cinttypes>
#include <cmath>
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
               "  rally_replay info <archivo.rrec>       (ajustes de la plataforma: al empezar)\n"
               "  rally_replay run <archivo.rrec> [--telemetry <salida.csv>] [--bench N]\n"
               "  rally_replay rewrite <archivo.rrec>\n"
               "  rally_replay synth <salida.rrec> --car <archivo.car>\n"
               "  rally_replay zone --json <archivo.json> [--blind <superficie>] [--road "
               "<gravel|asphalt>]\n"
               "  rally_replay columns --md [<archivo.md>]\n"
               "  rally_replay tire-curves <archivo.car> [--csv <salida.csv>]\n",
               stderr);
    return 2;
}

// Pasada por la carretera de prueba del pie: la última completa y sus recolocaciones.
void print_road(const char* label, const rally::io::RoadResult& road) {
    if (road.last_run < 0) {
        std::printf("%s ninguna\n", label);
        return;
    }
    std::printf("%s %" PRId64 " pasos (%.3f s)%s, %u recolocaciones; %u pasadas\n", label,
                road.last_run, static_cast<double>(road.last_run) / rally::sim::kStepsPerSecond,
                road.last_resets > 0 ? " con recolocación" : "", road.last_resets, road.runs);
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
        std::printf("mundo:               %zu cuerpos, %s\n", desc.bodies.size(),
                    desc.car ? "con coche" : "sin coche");
        std::printf("ayuda de dirección:  %s%s\n", desc.aids.steer_assist ? "sí" : "no",
                    h.format_version < 4 ? " (formato anterior a la 4: no existía)" : "");
        if (desc.test_zone) {
            // La superficie real: en las pruebas a ciegas solo se ve aquí.
            std::printf("zona de pruebas:     sí, pista ciega de %s, carretera de %s\n",
                        rally::sim::surface_name(desc.blind_surface),
                        rally::sim::surface_name(desc.road_surface));
        } else {
            std::printf("zona de pruebas:     no\n");
        }
    } else {
        std::printf("mundo:               no válido (%s)\n", error.c_str());
    }
    if (h.format_version >= 2) {
        std::printf("hash de la zona:     0x%016" PRIx64 "%s\n", h.zone_hash,
                    h.zone_hash == 0 ? " (sin zona)" : "");
    }
    std::printf("coche:               %zu bytes\n", h.car_text.size());
    // Ajustes de la plataforma al empezar la grabación (no son de la simulación: F7 a mitad de
    // pasada no los cambia).
    if (h.settings.empty()) {
        std::printf("ajustes al empezar:  (ninguno)\n");
    } else {
        std::printf("ajustes al empezar:\n%s%s", h.settings.c_str(),
                    h.settings.back() == '\n' ? "" : "\n");
    }
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
    print_road("carretera:          ", f.road);
    return 0;
}

// Tiempo de un paso medido: en qué repetición y en qué paso de la grabación cayó.
struct StepTime {
    double ms = 0.0;
    int repetition = 0;
    std::uint32_t step = 0; // 0 = el primer paso
};

struct BenchStats {
    double mean_ms = 0.0;
    double p99_ms = 0.0;
    double max_ms = 0.0;
    int over_1ms = 0;
    std::vector<StepTime> slowest; // los kSlowest más lentos, de más a menos
};

constexpr std::size_t kSlowest = 5;

void summarize(std::vector<StepTime>& times, BenchStats& stats) {
    if (times.empty()) {
        return;
    }
    double total = 0.0;
    for (const StepTime& t : times) {
        total += t.ms;
        stats.over_1ms += t.ms > 1.0 ? 1 : 0;
    }
    stats.mean_ms = total / static_cast<double>(times.size());
    std::sort(times.begin(), times.end(),
              [](const StepTime& a, const StepTime& b) { return a.ms > b.ms; });
    const std::size_t p99 =
        std::min(times.size() - 1, static_cast<std::size_t>(0.01 * times.size()));
    stats.p99_ms = times[p99].ms;
    stats.max_ms = times.front().ms;
    stats.slowest.assign(times.begin(), times.begin() + std::min(kSlowest, times.size()));
}

// N repeticiones midiendo cada paso, alternando sin y con la captura de la telemetría en cada
// repetición (así el ruido del sistema reparte igual entre las dos). Los tiempos se guardan en
// memoria reservada antes.
bool bench(const Recording& recording, int repetitions, BenchStats& plain, BenchStats& captured) {
    using Clock = std::chrono::steady_clock;
    std::vector<StepTime> times[2];
    for (std::vector<StepTime>& t : times) {
        t.reserve(static_cast<std::size_t>(recording.footer.total_steps) *
                  static_cast<std::size_t>(repetitions));
    }
    for (int rep = 0; rep < repetitions; ++rep) {
        for (int telemetry = 0; telemetry < 2; ++telemetry) {
            rally::sim::SimWorld world;
            std::string error;
            if (!rally::io::build_world(recording, world, error)) {
                std::fprintf(stderr, "error: %s\n", error.c_str());
                return false;
            }
            world.set_telemetry(telemetry == 1);
            std::uint32_t step = 0;
            for (const rally::io::InputRun& run : recording.runs) {
                for (std::uint32_t i = 0; i < run.count; ++i) {
                    const Clock::time_point start = Clock::now();
                    world.step(run.input);
                    const Clock::time_point end = Clock::now();
                    times[telemetry].push_back(
                        {std::chrono::duration<double, std::milli>(end - start).count(), rep,
                         step++});
                }
            }
        }
    }
    summarize(times[0], plain);
    summarize(times[1], captured);
    return true;
}

void print_slowest(const BenchStats& stats) {
    std::printf("  pasos > 1 ms: %d; los más lentos (repetición/paso: ms):", stats.over_1ms);
    for (const StepTime& t : stats.slowest) {
        std::printf("  %d/%u: %.3f", t.repetition, t.step, t.ms);
    }
    std::puts("");
}

// Escribe la telemetría de cada paso de la repetición en el CSV.
class CsvObserver final : public rally::io::StepObserver {
public:
    explicit CsvObserver(rally::io::TelemetryCsvWriter& writer) : writer_(writer) {}
    void after_step(const rally::sim::SimWorld& world) override {
        writer_.append(world.telemetry());
    }

private:
    rally::io::TelemetryCsvWriter& writer_;
};

// Nombre del archivo sin la ruta.
std::string_view file_name(std::string_view path) {
    const std::size_t slash = path.find_last_of("/\\");
    return slash == std::string_view::npos ? path : path.substr(slash + 1);
}

int run(const char* path, int bench_repetitions, const char* telemetry_path) {
    std::string bytes;
    Recording recording;
    if (!load(path, bytes, recording)) {
        return 2;
    }
    rally::io::TelemetryCsvWriter writer;
    CsvObserver observer(writer);
    if (telemetry_path != nullptr) {
        rally::sim::WorldDesc desc;
        std::string error;
        if (!rally::io::parse_world(recording.header.world, desc, error,
                                    recording.header.format_version)) {
            std::fprintf(stderr, "error: %s\n", error.c_str());
            return 2;
        }
        const std::string header = rally::io::telemetry_csv_header(
            file_name(path), desc.test_zone, desc.blind_surface, desc.road_surface);
        if (!writer.open(telemetry_path, header, error)) {
            std::fprintf(stderr, "error: %s\n", error.c_str());
            return 2;
        }
    }
    const rally::io::ReplayResult result =
        rally::io::replay(recording, telemetry_path != nullptr ? &observer : nullptr);
    if (!result.ran) {
        std::fprintf(stderr, "error: %s\n", result.problem.c_str());
        return 2;
    }
    if (telemetry_path != nullptr) {
        std::string error;
        if (!writer.close(error)) {
            std::fprintf(stderr, "error: %s\n", error.c_str());
            return 2;
        }
        const double minutes =
            static_cast<double>(result.steps) / rally::sim::kStepsPerSecond / 60.0;
        std::printf("telemetría: %s (%zu columnas, %" PRIu64 " bytes, %.2f MB por minuto)\n",
                    telemetry_path, rally::io::telemetry_columns().size(), writer.bytes_written(),
                    minutes > 0.0 ? static_cast<double>(writer.bytes_written()) / 1e6 / minutes
                                  : 0.0);
    }
    std::printf("pasos: %" PRIu64 " (%.2f s)   hash: 0x%016" PRIx64 "\n", result.steps,
                static_cast<double>(result.steps) / rally::sim::kStepsPerSecond, result.hash);
    print_timed("vuelta:", result.timed_steps);
    print_road("carretera:", result.road);
    int code = 0;
    if (result.hash_ok) {
        std::puts("hash OK");
    } else if (result.incomplete || result.version_mismatch || result.zone_mismatch) {
        std::printf("aviso: %s\n", result.problem.c_str());
    } else {
        std::printf("FALLO: %s\n", result.problem.c_str());
        code = 1;
    }
    if (bench_repetitions > 0) {
#ifndef NDEBUG
        std::puts("aviso: build de Debug; las cifras de Release son las que cuentan");
#endif
        BenchStats plain;
        BenchStats captured;
        if (!bench(recording, bench_repetitions, plain, captured)) {
            return 2;
        }
        std::printf("bench: %d repeticiones, ms por paso: media %.4f   p99 %.4f   máximo %.4f\n",
                    bench_repetitions, plain.mean_ms, plain.p99_ms, plain.max_ms);
        print_slowest(plain);
        std::printf("bench con telemetría: media %.4f   p99 %.4f   máximo %.4f   "
                    "captura: %.2f µs por paso\n",
                    captured.mean_ms, captured.p99_ms, captured.max_ms,
                    (captured.mean_ms - plain.mean_ms) * 1000.0);
        print_slowest(captured);
        // Un máximo que cae en pasos distintos en cada repetición no es de la simulación (hace
        // el mismo trabajo en todas): es el sistema (interrupciones, otros procesos).
        std::puts("  si los pasos más lentos cambian de una repetición a otra, son ruido del "
                  "sistema, no de la simulación");
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

// Entrada del paso `step` de la grabación sintética de 120 s (bench_lap.rrec). Cambia cada
// 4 pasos, como el bucle a 60 fps:
//   0–20 s   recta a fondo 12 s y frenada a fondo 8 s;
//   20–40 s  eslalon: dirección senoidal (periodo 2 s, la mitad del giro) con medio acelerador;
//   40–56 s  giros con freno de mano: acelerador al 70 % y cada 2,5 s un giro a fondo,
//            alternando el lado, con el freno de mano 0,5 s;
//   56–60 s  frenada, R (bajar dos veces), atrás 1 s, frenada y adelante (subir dos veces);
//   60–120 s tramos al azar de 0,5 s con PCG32 (semilla fija).
// Recoloca a los 20, 40, 60, 80 y 100 s para no quedarse contra un muro.
class SynthInputs {
public:
    rally::sim::SimInput at(int step) {
        constexpr int kSecond = rally::sim::kStepsPerSecond;
        constexpr std::int16_t kMax = rally::sim::kAxisMax;
        const auto axis = [](double v) {
            return static_cast<std::int16_t>(std::clamp(v, -1.0, 1.0) * kMax);
        };
        if (step % 4 != 0) {
            return current_;
        }
        rally::sim::SimInput in;
        const double t = static_cast<double>(step) / kSecond;
        if (t < 12.0) {
            in.throttle = kMax;
        } else if (t < 20.0) {
            in.brake = kMax;
        } else if (t < 40.0) {
            in.throttle = kMax / 2;
            in.steer = axis(0.5 * std::sin(2.0 * 3.141592653589793 * (t - 20.0) / 2.0));
        } else if (t < 56.0) {
            in.throttle = axis(0.7);
            const double phase = std::fmod(t - 40.0, 2.5);
            if (phase < 0.8) {
                const bool right = static_cast<int>((t - 40.0) / 2.5) % 2 == 0;
                in.steer = right ? kMax : static_cast<std::int16_t>(-kMax);
                if (phase < 0.5) {
                    in.buttons |= rally::sim::kButtonHandbrake;
                }
            }
        } else if (t < 60.0) {
            const int s = step - 56 * kSecond;
            if (s < 2 * kSecond) {
                in.brake = kMax;
            } else if (s < 2 * kSecond + 32) {
                // Dos pulsaciones de bajar, separadas: adelante → N → R.
                in.buttons = (s / 8) % 2 == 0 ? rally::sim::kButtonShiftDown : 0;
            } else if (s < 3 * kSecond + 32) {
                in.throttle = kMax / 2; // en R, hacia atrás
            } else if (s < 3 * kSecond + 160) {
                in.brake = kMax;
            } else if (s < 3 * kSecond + 192) {
                in.buttons = (s / 8) % 2 == 0 ? rally::sim::kButtonShiftUp : 0;
            }
        } else {
            if (step % (kSecond / 2) == 0) {
                random_.steer = axis(static_cast<double>(rng_.next_float01()) * 2.0 - 1.0);
                random_.throttle = rng_.bounded(3) == 0 ? 0 : axis(rng_.next_float01());
                random_.brake = rng_.bounded(4) == 0 ? axis(rng_.next_float01()) : 0;
                random_.buttons = rng_.bounded(6) == 0 ? rally::sim::kButtonHandbrake : 0;
            }
            in = random_;
        }
        if (step > 0 && step % (20 * kSecond) == 0) {
            in.buttons |= rally::sim::kButtonReset;
        }
        current_ = in;
        return in;
    }

private:
    rally::sim::Pcg32 rng_{0xbe2c'4a90'2026'1004ull, 5u};
    rally::sim::SimInput random_;
    rally::sim::SimInput current_;
};

int synth(const char* out_path, const char* car_path) {
    std::string car_text;
    std::string error;
    if (!rally::io::read_file(car_path, car_text, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 2;
    }
    const rally::sim::CarParseResult car = rally::sim::parse_car(car_text);
    if (!car.ok) {
        std::fprintf(stderr, "error: %s: %s\n", car_path, car.error.c_str());
        return 2;
    }
    // Con la ayuda de dirección, la de los jugadores por defecto: la medida incluye su coste.
    rally::sim::WorldDesc desc = rally::sim::zone_world(rally::sim::Surface::Gravel, car.car);
    desc.aids.steer_assist = true;
    rally::sim::SimWorld world;
    world.build(desc);
    world.seal();
    rally::io::RecordingHeader header;
    header.commit = rally::io::build_commit();
    header.world = rally::io::serialize_world(desc);
    header.car_text = car_text;
    header.zone_hash = world.zone_hash();
    constexpr int kSteps = 120 * rally::sim::kStepsPerSecond;
    rally::io::Recorder recorder;
    recorder.begin(std::move(header), kSteps);
    SynthInputs inputs;
    for (int step = 0; step < kSteps; ++step) {
        const rally::sim::SimInput in = inputs.at(step);
        world.step(in);
        recorder.append(in);
    }
    const Recording recording = recorder.finish(world.state_hash(), rally::io::timed_steps(world),
                                                rally::io::road_result(world));
    if (!rally::io::write_file(out_path, rally::io::serialize(recording), error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 2;
    }
    std::printf(
        "grabación sintética escrita: %s (%" PRIu64 " pasos, %zu tramos, hash 0x%016" PRIx64 ")\n",
        out_path, recording.footer.total_steps, recording.runs.size(), recording.footer.final_hash);
    return 0;
}

// Superficie por su nombre en inglés ("gravel"...); false si no existe.
bool surface_from_key(const char* key, rally::sim::Surface& surface) {
    for (int s = 0; s < rally::sim::kSurfaceCount; ++s) {
        const auto candidate = static_cast<rally::sim::Surface>(s);
        if (std::strcmp(key, rally::sim::surface_key(candidate)) == 0) {
            surface = candidate;
            return true;
        }
    }
    return false;
}

// Trazado de la zona de pruebas en JSON. Sin --blind, la pista ciega es de grava; sin --road,
// la carretera también.
int zone_json(const char* path, const char* blind, const char* road) {
    rally::sim::Surface surface = rally::sim::Surface::Gravel;
    if (blind != nullptr && !surface_from_key(blind, surface)) {
        std::fprintf(stderr, "error: superficie desconocida: %s\n", blind);
        return 2;
    }
    rally::sim::Surface road_surface = rally::sim::Surface::Gravel;
    if (road != nullptr &&
        (!surface_from_key(road, road_surface) || (road_surface != rally::sim::Surface::Gravel &&
                                                   road_surface != rally::sim::Surface::Asphalt))) {
        std::fprintf(stderr, "error: la carretera es de gravel o de asphalt, no %s\n", road);
        return 2;
    }
    const rally::sim::TestZone zone(surface, road_surface);
    std::string error;
    if (!rally::io::write_file(path, zone.to_json(), error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 2;
    }
    std::printf("zona escrita: %s (pista ciega de %s, carretera de %s)\n", path,
                rally::sim::surface_name(surface), rally::sim::surface_name(road_surface));
    return 0;
}

// Tabla de columnas de la telemetría en Markdown, a un archivo o a la salida estándar.
int columns_md(const char* path) {
    const std::string md = rally::io::telemetry_columns_markdown();
    if (path == nullptr) {
        std::fwrite(md.data(), 1, md.size(), stdout);
        return 0;
    }
    std::string error;
    if (!rally::io::write_file(path, md, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 2;
    }
    std::printf("columnas escritas: %s (%zu)\n", path, rally::io::telemetry_columns().size());
    return 0;
}

// Curvas del neumático de un .car, con el mismo código que la simulación (sim/tire.h). Sin
// `csv_path`, el resumen por superficie en la salida estándar; con él, además, las curvas pura
// lateral (α de 0 a 60°) y longitudinal (κ de 0 a 1) a 0,5, 1 y 1,5·F_z0 en CSV
// (tools/telemetry/tire_curves.py las dibuja).
int tire_curves(const char* car_path, const char* csv_path) {
    std::string text;
    std::string error;
    if (!rally::io::read_file(car_path, text, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 2;
    }
    const rally::sim::CarParseResult parsed = rally::sim::parse_car(text);
    if (!parsed.ok) {
        std::fprintf(stderr, "error: %s: %s\n", car_path, parsed.error.c_str());
        return 2;
    }
    const rally::sim::CarData& car = parsed.car;
    const float nominal = car.nominal_load;
    constexpr float kDeg = 3.14159265358979323846f / 180.0f;
    const auto lateral = [&](const rally::sim::TireSurface& t, float alpha, float load) {
        return rally::sim::tire_force(t, 0.0f, std::tan(alpha), load, nominal).fy;
    };
    const auto longitudinal = [&](const rally::sim::TireSurface& t, float kappa, float load) {
        return rally::sim::tire_force(t, kappa, 0.0f, load, nominal).fx;
    };

    std::printf("neumático de %s, F_z0 = %.0f N (fuerza / pico a F_z0)\n", car_path,
                static_cast<double>(nominal));
    std::printf("superficie | μ pico | α pico | κ pico | C | B | E | lateral a 1,5 / 2 / 3 / 4 × "
                "α pico | longitudinal a 1,5 / 2 / 3 / 4 × κ pico | asíntota | κ = 1 | "
                "combinada (κ pico, α pico)\n");
    for (int s = 0; s < rally::sim::kSurfaceCount; ++s) {
        const auto surface = static_cast<rally::sim::Surface>(s);
        const rally::sim::TireSurface& t = car.tire(surface);
        const float d = t.mu_peak * nominal;
        const auto lat = [&](float factor) {
            return lateral(t, factor * t.slip_angle_peak, nominal) / d;
        };
        const auto lon = [&](float factor) {
            return longitudinal(t, factor * t.slip_ratio_peak, nominal) / d;
        };
        const float combined =
            rally::sim::tire_force(t, t.slip_ratio_peak, t.tan_alpha_peak, nominal, nominal).fy / d;
        std::printf(
            "%s | %.2f | %.1f° | %.3f | %.3f | %.3f | %.2f | %.3f / %.3f / %.3f / %.3f | "
            "%.3f / %.3f / %.3f / %.3f | %.2f | %.3f | %.3f\n",
            rally::sim::surface_name(surface), static_cast<double>(t.mu_peak),
            static_cast<double>(t.slip_angle_peak / kDeg), static_cast<double>(t.slip_ratio_peak),
            static_cast<double>(t.shape_c), static_cast<double>(t.shape_b),
            static_cast<double>(t.curvature_e), static_cast<double>(lat(1.5f)),
            static_cast<double>(lat(2.0f)), static_cast<double>(lat(3.0f)),
            static_cast<double>(lat(4.0f)), static_cast<double>(lon(1.5f)),
            static_cast<double>(lon(2.0f)), static_cast<double>(lon(3.0f)),
            static_cast<double>(lon(4.0f)), static_cast<double>(t.slide_ratio),
            static_cast<double>(longitudinal(t, 1.0f, nominal) / d), static_cast<double>(combined));
    }
    if (csv_path == nullptr) {
        return 0;
    }

    std::string csv = "surface,direction,load_ratio,load,slip,force,force_over_peak\n";
    char row[160];
    for (int s = 0; s < rally::sim::kSurfaceCount; ++s) {
        const auto surface = static_cast<rally::sim::Surface>(s);
        const rally::sim::TireSurface& t = car.tire(surface);
        for (const float ratio : {0.5f, 1.0f, 1.5f}) {
            const float load = ratio * nominal;
            const float d = rally::sim::peak_mu(t, load, nominal) * load;
            // Lateral: slip en grados.
            for (int i = 0; i <= 240; ++i) {
                const float alpha_deg = 0.25f * static_cast<float>(i);
                const float f = lateral(t, alpha_deg * kDeg, load);
                std::snprintf(row, sizeof(row), "%s,lateral,%.1f,%.1f,%.2f,%.2f,%.5f\n",
                              rally::sim::surface_key(surface), static_cast<double>(ratio),
                              static_cast<double>(load), static_cast<double>(alpha_deg),
                              static_cast<double>(f), static_cast<double>(f / d));
                csv += row;
            }
            // Longitudinal: slip = κ.
            for (int i = 0; i <= 200; ++i) {
                const float kappa = 0.005f * static_cast<float>(i);
                const float f = longitudinal(t, kappa, load);
                std::snprintf(row, sizeof(row), "%s,longitudinal,%.1f,%.1f,%.3f,%.2f,%.5f\n",
                              rally::sim::surface_key(surface), static_cast<double>(ratio),
                              static_cast<double>(load), static_cast<double>(kappa),
                              static_cast<double>(f), static_cast<double>(f / d));
                csv += row;
            }
        }
    }
    if (!rally::io::write_file(csv_path, csv, error)) {
        std::fprintf(stderr, "error: %s\n", error.c_str());
        return 2;
    }
    std::printf("curvas escritas: %s\n", csv_path);
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
        if (argc < 4 || argc % 2 != 0 || std::strcmp(argv[2], "--json") != 0) {
            return usage();
        }
        const char* blind = nullptr;
        const char* road = nullptr;
        for (int i = 4; i + 1 < argc; i += 2) {
            if (std::strcmp(argv[i], "--blind") == 0) {
                blind = argv[i + 1];
            } else if (std::strcmp(argv[i], "--road") == 0) {
                road = argv[i + 1];
            } else {
                return usage();
            }
        }
        return zone_json(argv[3], blind, road);
    }
    if (std::strcmp(command, "tire-curves") == 0) {
        if (argc == 3) {
            return tire_curves(argv[2], nullptr);
        }
        if (argc == 5 && std::strcmp(argv[3], "--csv") == 0) {
            return tire_curves(argv[2], argv[4]);
        }
        return usage();
    }
    if (std::strcmp(command, "columns") == 0 && std::strcmp(argv[2], "--md") == 0 && argc <= 4) {
        return columns_md(argc == 4 ? argv[3] : nullptr);
    }
    const char* path = argv[2];
    if (std::strcmp(command, "info") == 0 && argc == 3) {
        return info(path);
    }
    if (std::strcmp(command, "rewrite") == 0 && argc == 3) {
        return rewrite(path);
    }
    if (std::strcmp(command, "synth") == 0 && argc == 5 && std::strcmp(argv[3], "--car") == 0) {
        return synth(path, argv[4]);
    }
    if (std::strcmp(command, "run") == 0) {
        int repetitions = 0;
        const char* telemetry = nullptr;
        for (int i = 3; i < argc; i += 2) {
            if (i + 1 >= argc) {
                return usage();
            }
            if (std::strcmp(argv[i], "--bench") == 0 && repetitions == 0) {
                const char* text = argv[i + 1];
                const std::from_chars_result parsed =
                    std::from_chars(text, text + std::strlen(text), repetitions);
                if (parsed.ec != std::errc{} || *parsed.ptr != '\0' || repetitions < 1) {
                    return usage();
                }
            } else if (std::strcmp(argv[i], "--telemetry") == 0 && telemetry == nullptr) {
                telemetry = argv[i + 1];
            } else {
                return usage();
            }
        }
        return run(path, repetitions, telemetry);
    }
    return usage();
}
