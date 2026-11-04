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

struct PassCost {
    double mean = 0.0;
    double p99 = 0.0;
};

PassCost summarize(std::vector<double>& ms) {
    PassCost out;
    for (const double m : ms) {
        out.mean += m;
    }
    out.mean /= static_cast<double>(ms.size());
    std::sort(ms.begin(), ms.end());
    out.p99 = ms[ms.size() * 99 / 100];
    return out;
}

// Referencia en las mismas condiciones del PC: una pasada de la carretera de la 2.6b
// (tests/data/recordings/road_run.rrec). Con ella se fijó el presupuesto de §2.6 (0,048 y 0,077
// ms) y con ella se compara el tramo en la misma ejecución, alternando (respuesta del consultor,
// §4: tramo / carretera ≤ 1,25 en la media y en el p99).
PassCost road_pass() {
    static const rally::io::Recording recording = [] {
        std::string bytes;
        std::string error;
        RALLY_CHECK(
            rally::io::read_file(RALLY_TEST_DATA_DIR "/recordings/road_run.rrec", bytes, error),
            "rally_stage_measure: no se puede leer road_run.rrec");
        return rally::io::parse(bytes).recording;
    }();
    rally::sim::SimWorld world;
    std::string error;
    RALLY_CHECK(rally::io::build_world(recording, world, error),
                "rally_stage_measure: no se puede construir road_run.rrec");
    std::vector<double> ms;
    for (const rally::io::InputRun& run : recording.runs) {
        for (std::uint32_t i = 0; i < run.count; ++i) {
            const auto t0 = Clock::now();
            world.step(run.input);
            const auto t1 = Clock::now();
            ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
        }
    }
    return summarize(ms);
}
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
        std::vector<double> road_means;
        std::vector<double> road_p99s;
        for (int pass = 0; pass < 3; ++pass) {
            // Alternando: una pasada de la carretera de la 2.6b antes de cada una del tramo.
            const PassCost road = road_pass();
            road_means.push_back(road.mean);
            road_p99s.push_back(road.p99);
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
        std::sort(road_means.begin(), road_means.end());
        std::sort(road_p99s.begin(), road_p99s.end());
        std::printf("        carretera de la 2.6b alternada: media %.4f ms, p99 %.4f ms; tramo / "
                    "carretera: "
                    "media x%.2f, p99 x%.2f (límite x1,25)\n",
                    road_means[1], road_p99s[1], means[1] / road_means[1], p99s[1] / road_p99s[1]);
    }
}

} // namespace

// Coste del microrrelieve del tramo por rueda: el marco (una proyección) y las 9 muestras.
void relief_cost() {
    rally::sim::StageTrack track;
    rally::sim::StageTrack::build(rally::tests::lazadas_stage_text(), track);
    const rally::sim::TestZone noise;
    constexpr int kCalls = 200'000;
    double sum = 0.0;
    const auto t0 = Clock::now();
    for (int i = 0; i < kCalls; ++i) {
        const double s = 1100.0 * static_cast<double>(i) / kCalls;
        const rally::sim::StagePose p = track.pose(s);
        const rally::sim::StageReliefFrame f =
            rally::sim::stage_relief_frame(track, s, p.x + 0.7, p.z + 0.3);
        sum += f.t;
    }
    const auto t1 = Clock::now();
    for (int i = 0; i < kCalls; ++i) {
        const double s = 1100.0 * static_cast<double>(i) / kCalls;
        const rally::sim::StagePose p = track.pose(s);
        sum += p.x;
    }
    const auto t2 = Clock::now();
    rally::sim::StageReliefFrame f;
    f.half_left = 3.0;
    f.half_right = 3.0;
    f.surface_break = 1e18;
    for (int i = 0; i < kCalls; ++i) {
        sum += rally::sim::stage_relief_height(f, noise, 10.0, 0.5 + 1e-5 * i, 1.0 * i, 2.0);
    }
    const auto t3 = Clock::now();
    const double frame_us =
        std::chrono::duration<double, std::micro>((t1 - t0) - (t2 - t1)).count();
    std::printf("microrrelieve: marco %.3f µs, una muestra %.3f µs (%g)\n", frame_us / kCalls,
                std::chrono::duration<double, std::micro>(t3 - t2).count() / kCalls, sum);
}

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const rally::sim::JoltRuntime runtime;
    relief_cost();
    measure("lazadas.stage", rally::tests::lazadas_stage_text(), 0.0, 1150.0);
    measure("sintético 30 km", rally::tests::synthetic_stage_30km(), 1000.0, 4000.0);
    return 0;
}
