// Medida de la parte A de tramo-fisica (lote 4; mensajes/lote-4.md §2.6): el suelo del tramo
// del diseño (M: núcleo en malla y terreno en baldosas de 0,75 m o de 1,5 m) frente a todo en
// rejilla de 0,75 m (R), en lazadas.stage y en el tramo sintético de 30 km. Para cada uno:
// build() + seal() (mediana de 3), memoria de las formas de Jolt, triángulos, muestras y el coste
// por paso con el 205 (reference.car) y el conductor de test a 50–90 km/h. Se lanza a mano en
// Release; escribe la tabla en la salida.

#include "car_worlds.h"
#include "io/replay.h"
#include "sim/jolt_runtime.h"
#include "sim/sim_world.h"
#include "sim/stage_ground.h"
#include "stage_driver.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using rally::sim::StageGroundVariant;
using Clock = std::chrono::steady_clock;

struct Variant {
    const char* name;
    StageGroundVariant variant;
    double cell;
    rally::sim::StageGroundKind kind;
};

constexpr Variant kVariants[] = {
    {"M 0,75", StageGroundVariant::Design, 0.75, rally::sim::StageGroundKind::Design},
    {"M 1,5", StageGroundVariant::CoarseTiles, 1.5, rally::sim::StageGroundKind::Design},
    {"R 0,75", StageGroundVariant::GridOnly, 0.75, rally::sim::StageGroundKind::GridOnly},
};

rally::sim::WorldDesc stage_world(const std::string& text, StageGroundVariant variant,
                                  const rally::sim::StageTrack& track, double s, float speed) {
    rally::sim::WorldDesc desc;
    desc.stage_text = text;
    desc.stage_variant = variant;
    desc.car = true;
    desc.car_data = rally::tests::reference_car();
    desc.car_spawn = rally::sim::stage_spawn(track, s, speed);
    return desc;
}

void measure(const char* stage_name, const std::string& text, double start_s, double drive_m) {
    rally::sim::StageTrack track;
    const rally::sim::StageParseResult parsed = rally::sim::StageTrack::build(text, track);
    if (!parsed.ok) {
        std::printf("%s: el tramo no vale: %s\n", stage_name, parsed.error.c_str());
        return;
    }
    double x0 = 1e18;
    double x1 = -1e18;
    double z0 = 1e18;
    double z1 = -1e18;
    for (double s = track.s_begin(); s <= track.s_end(); s += 1.0) {
        const rally::sim::StagePose p = track.pose(s);
        x0 = std::min(x0, p.x);
        x1 = std::max(x1, p.x);
        z0 = std::min(z0, p.z);
        z1 = std::max(z1, p.z);
    }
    const rally::sim::StagePose start = track.pose(start_s);
    std::printf("\n%s: %.0f m; caja del eje %.0f × %.0f m; la conducción empieza en (%.0f, %.0f)\n",
                stage_name, track.axis().length(), x1 - x0, z1 - z0, start.x, start.z);
    std::printf("%-7s | %9s | %9s | %9s | %8s | %6s | %10s | %8s | %8s | %8s | %6s\n", "suelo",
                "carga ms", "geom. ms", "Jolt MB", "triáng.", "bald.", "muestras", "media ms",
                "p99 ms", "máx ms", "km/h");
    for (const Variant& v : kVariants) {
        // Geometría sola.
        const auto g0 = Clock::now();
        const rally::sim::StageGround ground =
            rally::sim::StageGround::build(track, v.cell, v.kind);
        const auto g1 = Clock::now();
        // build() + seal(), mediana de 3.
        std::vector<double> loads;
        std::size_t bytes = 0;
        for (int i = 0; i < 3; ++i) {
            const rally::sim::WorldDesc desc = stage_world(text, v.variant, track, start_s, 0.0f);
            rally::sim::SimWorld world;
            const auto t0 = Clock::now();
            world.build(desc);
            world.seal();
            const auto t1 = Clock::now();
            loads.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
            bytes = world.stage_ground_bytes();
        }
        std::sort(loads.begin(), loads.end());

        // Conducción: el conductor de test a 90 km/h como mucho, limitado por las curvas. Tres
        // pasadas iguales (la simulación es determinista): la mediana de la media y del p99.
        std::vector<double> means;
        std::vector<double> p99s;
        std::vector<double> maxima;
        std::size_t steps = 0;
        double speed = 0.0;
        double final_s = 0.0;
        int resets = 0;
        int shortcuts = 0;
        for (int pass = 0; pass < 3; ++pass) {
            const rally::sim::WorldDesc desc =
                stage_world(text, v.variant, track, start_s, static_cast<float>(50.0 / 3.6));
            rally::sim::SimWorld world;
            world.build(desc);
            world.seal();
            rally::tests::StageDriver driver(track, 90.0 / 3.6);
            std::vector<double> ms;
            double speed_sum = 0.0;
            while (driver.s() < start_s + drive_m && ms.size() < 600'000) {
                const rally::sim::SimInput in =
                    driver.input(world.car_pose(), world.car_velocity());
                const auto t0 = Clock::now();
                world.step(in);
                const auto t1 = Clock::now();
                ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
                const rally::sim::BodyVelocity vel = world.car_velocity();
                speed_sum += std::hypot(vel.vx, vel.vz);
            }
            steps = ms.size();
            double mean = 0.0;
            for (const double m : ms) {
                mean += m;
            }
            means.push_back(mean / static_cast<double>(steps));
            std::sort(ms.begin(), ms.end());
            p99s.push_back(ms[steps * 99 / 100]);
            maxima.push_back(ms.back());
            speed = 3.6 * speed_sum / static_cast<double>(steps);
            final_s = driver.s();
            resets = world.stage_run()->resets;
            shortcuts = world.stage_run()->shortcuts;
        }
        const std::vector<double> mean_runs = means;
        const std::vector<double> p99_runs = p99s;
        std::sort(means.begin(), means.end());
        std::sort(p99s.begin(), p99s.end());
        std::sort(maxima.begin(), maxima.end());
        std::printf("%-7s | %9.1f | %9.1f | %9.2f | %8zu | %6zu | %10zu | %8.4f | %8.4f | %8.4f | "
                    "%6.1f\n",
                    v.name, loads[1], std::chrono::duration<double, std::milli>(g1 - g0).count(),
                    static_cast<double>(bytes) / (1024.0 * 1024.0), ground.triangles().size(),
                    ground.tiles().size(), ground.sample_count(), means[1], p99s[1], maxima[1],
                    speed);
        std::printf("        %zu pasos; s final %.1f; recolocaciones %d, atajos %d; secciones %zu; "
                    "muestras sin peso %zu; origen (%.0f, %.0f, %.0f)\n",
                    steps, final_s, resets, shortcuts, ground.sections().size(),
                    ground.nearest_fallbacks(), ground.origin_x(), ground.origin_y(),
                    ground.origin_z());
        std::printf("        pasadas: media %.4f / %.4f / %.4f ms; p99 %.4f / %.4f / %.4f ms\n",
                    mean_runs[0], mean_runs[1], mean_runs[2], p99_runs[0], p99_runs[1],
                    p99_runs[2]);
    }
}

// Referencia en las mismas condiciones del PC: la pasada grabada por la carretera de la 2.6b
// (tests/data/recordings/road_run.rrec), tres veces; media y p99 de cada una. Los presupuestos de
// §2.6 se fijaron con ella (0,048 y 0,077 ms).
void road_reference() {
    std::string bytes;
    std::string error;
    if (!rally::io::read_file(RALLY_TEST_DATA_DIR "/recordings/road_run.rrec", bytes, error)) {
        std::printf("referencia: %s\n", error.c_str());
        return;
    }
    const rally::io::ParseResult parsed = rally::io::parse(bytes);
    std::printf("\ncarretera de la 2.6b (road_run.rrec):");
    for (int pass = 0; pass < 3; ++pass) {
        rally::sim::SimWorld world;
        if (!rally::io::build_world(parsed.recording, world, error)) {
            std::printf(" %s\n", error.c_str());
            return;
        }
        std::vector<double> ms;
        for (const rally::io::InputRun& run : parsed.recording.runs) {
            for (std::uint32_t i = 0; i < run.count; ++i) {
                const auto t0 = Clock::now();
                world.step(run.input);
                const auto t1 = Clock::now();
                ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
            }
        }
        double mean = 0.0;
        for (const double m : ms) {
            mean += m;
        }
        mean /= static_cast<double>(ms.size());
        std::sort(ms.begin(), ms.end());
        std::printf(" media %.4f ms, p99 %.4f ms;", mean, ms[ms.size() * 99 / 100]);
    }
    std::printf("\n");
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const rally::sim::JoltRuntime runtime;
    road_reference();
    measure("lazadas.stage", rally::tests::lazadas_stage_text(), 0.0, 1150.0);
    road_reference();
    measure("sintético 30 km", rally::tests::synthetic_stage_30km(), 1000.0, 4000.0);
    road_reference();
    return 0;
}
