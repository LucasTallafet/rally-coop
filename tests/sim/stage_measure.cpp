// Medida de la parte A de tramo-fisica (lote 4; mensajes/lote-4.md §2.6): el suelo del tramo
// del diseño (M: núcleo en malla y terreno en baldosas de 0,75 m o de 1,5 m) frente a todo en
// rejilla de 0,75 m (R), en lazadas.stage y en el tramo sintético de 30 km. Para cada uno:
// build() + seal() (mediana de 3), memoria de las formas de Jolt, triángulos, muestras y el coste
// por paso con el 205 (reference.car) y el conductor de test a 50–90 km/h. Se lanza a mano en
// Release; escribe la tabla en la salida.

#include "band_report.h"
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

// Desde la parte B de formato-2, un solo suelo: el del juego (malla con la banda y rejilla de 0,75
// m).
constexpr Variant kVariants[] = {
    {"F2 0,75", StageGroundVariant::Design, 0.75, rally::sim::StageGroundKind::Design},
};
constexpr Variant kFormat2Variants[] = {
    {"F2 0,75", StageGroundVariant::Design, 0.75, rally::sim::StageGroundKind::Design},
};
bool g_format2 = false;

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
    const auto v0 = Clock::now();
    const rally::sim::StageParseResult parsed = rally::sim::StageTrack::build(text, track);
    const auto v1 = Clock::now();
    if (!parsed.ok) {
        std::printf("%s: el tramo no vale: %s\n", stage_name, parsed.error.c_str());
        return;
    }
    std::printf("%s: StageTrack::build (con la banda) %.1f ms\n", stage_name,
                std::chrono::duration<double, std::milli>(v1 - v0).count());
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
    std::vector<Variant> variants(std::begin(kVariants), std::end(kVariants));
    if (g_format2) {
        variants.assign(std::begin(kFormat2Variants), std::end(kFormat2Variants));
    }
    for (const Variant& v : variants) {
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
        std::printf("        con más pies de los que caben %zu; en el centro de un arco %zu\n",
                    ground.candidate_overflows(), ground.center_overflows().size());
        for (const rally::sim::StageCenterOverflow& c : ground.center_overflows()) {
            std::printf("          (%.3f, %.3f): a %.3g m del vértice; altura %.4f, pies de %.4f "
                        "a %.4f\n",
                        c.x, c.z, c.distance, c.height, c.foot_min, c.foot_max);
        }
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

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const rally::sim::JoltRuntime runtime;
    // «f2»: la geometría de la banda y el coste; «f2geo»: solo la geometría.
    const std::string mode = argc > 1 ? std::string(argv[1]) : std::string();
    g_format2 = mode == "f2" || mode == "f2geo";
    rally::tests::g_band_debug = argc > 2 && std::string(argv[2]) == "debug";
    if (g_format2) {
        rally::tests::band_report();
    }
    if (mode == "f2geo") {
        return 0;
    }
    if (mode == "f2probe" && argc >= 4) {
        // La rejilla sola (TerrainOnly) a lo largo de la normal de lazadas en s, de t0 a t1 cada
        // 0,25 m, frente al plano del eje con el peralte: diagnóstico.
        rally::sim::StageTrack track;
        rally::sim::StageTrack::build(rally::tests::lazadas_stage_text(), track);
        rally::sim::WorldDesc desc;
        desc.stage_text = rally::tests::lazadas_stage_text();
        desc.stage_variant = rally::sim::StageGroundVariant::TerrainOnly;
        rally::sim::SimWorld world;
        world.build(desc);
        world.seal();
        rally::sim::WorldDesc mesh_desc = desc;
        mesh_desc.stage_variant = rally::sim::StageGroundVariant::Design;
        rally::sim::SimWorld mesh;
        mesh.build(mesh_desc);
        mesh.seal();
        const double s = std::stod(argv[2]);
        const double t0 = std::stod(argv[3]);
        const double t1 = argc >= 5 ? std::stod(argv[4]) : t0 + 10.0;
        const rally::sim::StagePose p = track.pose(s);
        const double step = argc >= 6 ? std::stod(argv[5]) : 0.25;
        for (double t = t0; t <= t1 + 1e-9; t += step) {
            const double x = p.x + t * std::cos(p.heading);
            const double z = p.z + t * std::sin(p.heading);
            const rally::sim::GroundProbe g = world.probe_ground(x, z);
            const rally::sim::GroundProbe m = mesh.probe_ground(x, z);
            std::printf(
                "t %7.3f rejilla %9.4f suelo %9.4f (sup %d) plano %9.4f diferencia %8.2f mm\n", t,
                g.y, m.y, static_cast<int>(m.surface), p.y + p.bank * t,
                1000.0 * (g.y - (p.y + p.bank * t)));
        }
        return 0;
    }
    if (mode == "f2sides" && argc >= 5) {
        // Los lados de la banda en [s0, s1] de un tramo (lazadas, hairpin o 30km): diagnóstico.
        const std::string which = argv[2];
        const std::string text = which == "30km" ? rally::tests::synthetic_stage_30km()
                                 : which == "hairpin"
                                     ? rally::tests::stage_file("hairpin_flat.stage")
                                 : which == "stacked3" ? rally::tests::stacked_stage(0.03)
                                                       : rally::tests::lazadas_stage_text();
        rally::sim::StageTrack track;
        rally::sim::StageTrack::build(text, track);
        const rally::sim::StageGround g =
            rally::sim::StageGround::build(track, rally::sim::kStageCell);
        const double s0 = std::stod(argv[3]);
        const double s1 = std::stod(argv[4]);
        for (const rally::sim::StageBandSide& b : g.band_sides()) {
            if (b.s < s0 || b.s > s1) {
                continue;
            }
            const rally::sim::StagePose p = track.pose(b.s);
            std::printf("s %9.3f lado %+d %-10s delta %8.4f enfr %9.3f end_u %7.3f cabeza %8.3f "
                        "final %8.3f pend %6.3f kappa %8.5f\n",
                        b.s, b.side, rally::tests::band_kind_name(b.kind), b.delta, b.other_s,
                        b.end_u, b.rim_y, b.end_y, b.slope, p.curvature);
        }
        return 0;
    }
    relief_cost();
    measure("lazadas.stage", rally::tests::lazadas_stage_text(), 0.0, 1150.0);
    measure("sintético 30 km", rally::tests::synthetic_stage_30km(), 1000.0, 4000.0);
    return 0;
}
