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
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <tuple>
#include <utility>
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

// --- Parte A de irregularidad (lote 7): las premisas P1–P9 y el espectro
// --------------------------

std::string hex(std::uint64_t v) {
    char text[32];
    std::snprintf(text, sizeof(text), "0x%016llx", static_cast<unsigned long long>(v));
    return text;
}

rally::sim::StageTrack must_build(const std::string& text, const char* name) {
    rally::sim::StageTrack track;
    const rally::sim::StageParseResult r = rally::sim::StageTrack::build(text, track);
    if (!r.ok) {
        std::printf("%s: NO CARGA: %s\n", name, r.error.c_str());
        std::exit(1);
    }
    return track;
}

// La salida de los tests de tramo (test_stage_world.cpp, spawn_at con t = 0): stage_spawn con el
// punto del eje del esqueleto, como hoy.
rally::sim::CarSpawn axis_spawn(const rally::sim::StageTrack& track, double s, float speed) {
    rally::sim::CarSpawn spawn = rally::sim::stage_spawn(track, s, speed);
    const rally::sim::StagePose p = track.pose(s);
    spawn.x = p.x;
    spawn.z = p.z;
    spawn.y = p.y;
    spawn.yaw = static_cast<float>(-p.heading);
    return spawn;
}

rally::sim::WorldDesc irr_desc(const std::string& text, const rally::sim::CarSpawn& spawn) {
    rally::sim::WorldDesc desc;
    desc.stage_text = text;
    desc.car = true;
    desc.car_data = rally::tests::reference_car();
    desc.car_spawn = spawn;
    return desc;
}

// P1: la pasada de reproducibilidad de test_stage_world.cpp (record_run): 1500 pasos del conductor
// a 70 km/h con un botón de recolocar en el 900, en el tramo `text`.
std::uint64_t reproducibility_hash(const std::string& text) {
    const rally::sim::StageTrack track = must_build(text, "P1");
    rally::sim::SimWorld world;
    world.build(irr_desc(text, axis_spawn(track, 0.0, 0.0f)));
    world.seal();
    rally::tests::StageDriver driver(track, 70.0 / 3.6);
    for (int i = 0; i < 1500; ++i) {
        rally::sim::SimInput in = driver.input(world.car_pose(), world.car_velocity());
        if (i == 900) {
            in.buttons = rally::sim::kButtonReset;
        }
        world.step(in);
    }
    return world.state_hash();
}

// P2: la mayor |κ| de la línea central real (por tres puntos cada 0,25 m) por (semiancho + arcén
// + 0,5) del lado de dentro, y dónde; y la del esqueleto, igual, como referencia.
void curvature_report(const char* name, const rally::sim::StageTrack& track) {
    constexpr double kStep = 0.25;
    const auto center = [&](double s, double& x, double& z) {
        const rally::sim::StagePose p = track.pose(s);
        const double c = track.road_edges(p).center;
        x = p.x + c * std::cos(p.heading);
        z = p.z + c * std::sin(p.heading);
    };
    double worst = 0.0;
    double worst_s = 0.0;
    double worst_k = 0.0;
    double skeleton = 0.0;
    double skeleton_s = 0.0;
    for (double s = track.s_begin() + kStep; s + kStep <= track.s_end(); s += kStep) {
        double x0, z0, x1, z1, x2, z2;
        center(s - kStep, x0, z0);
        center(s, x1, z1);
        center(s + kStep, x2, z2);
        const double a = std::hypot(x1 - x0, z1 - z0);
        const double b = std::hypot(x2 - x1, z2 - z1);
        const double c = std::hypot(x2 - x0, z2 - z0);
        const double cross = (x1 - x0) * (z2 - z0) - (z1 - z0) * (x2 - x0);
        // κ = 4·área / (a·b·c), con signo (+ a la derecha: x este, z sur).
        const double k = 2.0 * cross / (a * b * c);
        const rally::sim::StagePose p = track.pose(s);
        const rally::sim::StageRoadEdges e = track.road_edges(p);
        const int inner = k > 0.0 ? 1 : -1;
        const double reach =
            (inner > 0 ? e.half_right + e.shoulder_right : e.half_left + e.shoulder_left) + 0.5;
        if (std::abs(k) * reach > worst) {
            worst = std::abs(k) * reach;
            worst_s = s;
            worst_k = k;
        }
        const double reach0 =
            (p.curvature > 0.0 ? p.half_width_right : p.half_width_left) + 1.0 + 0.5;
        if (std::abs(p.curvature) * reach0 > skeleton) {
            skeleton = std::abs(p.curvature) * reach0;
            skeleton_s = s;
        }
    }
    std::printf("  P2 %s: mayor |κ efectiva|·(semiancho + arcén + 0,5) = %.4f en s %.2f (κ %.5f, R "
                "%.1f m); el esqueleto, %.4f en s %.2f\n",
                name, worst, worst_s, worst_k, 1.0 / std::abs(worst_k), skeleton, skeleton_s);
}

// P3: una pasada por una recta a `kmh` con el acelerador para esa velocidad (sin freno), la
// dirección del conductor de test, y lo medido mientras el origen del coche está en [from, to] y
// en la carretera: pasos-rueda sin contacto y el p99 del salto de carga entre pasos.
struct StraightStats {
    int wheel_steps = 0;
    int unloaded = 0;
    std::vector<double> jumps;
    double max_jump = 0.0;
    double max_jump_s = 0.0; // s del origen del coche en el mayor salto
    int max_jump_wheel = -1;
    double p99() {
        if (jumps.empty()) {
            return 0.0;
        }
        std::sort(jumps.begin(), jumps.end());
        return jumps[jumps.size() * 99 / 100];
    }
    double percent() const {
        return wheel_steps > 0 ? 100.0 * unloaded / static_cast<double>(wheel_steps) : 0.0;
    }
};

StraightStats straight_pass(const std::string& text, const rally::sim::StageTrack& track,
                            double spawn_s, double from, double to, double kmh) {
    const double speed = kmh / 3.6;
    rally::sim::SimWorld world;
    world.build(irr_desc(text, rally::sim::stage_spawn(track, spawn_s, static_cast<float>(speed))));
    world.seal();
    world.set_telemetry(true);
    rally::tests::StageDriver driver(track, speed, 100.0);
    StraightStats out;
    float previous[rally::sim::kWheelCount] = {};
    bool have_previous = false;
    for (int i = 0; i < 60 * rally::sim::kStepsPerSecond; ++i) {
        rally::sim::SimInput in = driver.input(world.car_pose(), world.car_velocity());
        const rally::sim::BodyVelocity v = world.car_velocity();
        in.throttle = static_cast<std::int16_t>(
            std::clamp(0.5 * (speed - std::hypot(v.vx, v.vz)), 0.0, 1.0) * rally::sim::kAxisMax);
        in.brake = 0;
        world.step(in);
        const double s = world.stage_run()->s;
        if (s > to) {
            break;
        }
        const rally::sim::CarTelemetry& t = world.telemetry();
        const bool inside = s >= from && world.stage_run()->on_road;
        for (int w = 0; w < rally::sim::kWheelCount; ++w) {
            const auto& wheel = t.wheels[static_cast<std::size_t>(w)];
            if (inside) {
                ++out.wheel_steps;
                out.unloaded += wheel.contact == 0 || wheel.load <= 0.0f ? 1 : 0;
                if (have_previous) {
                    const double jump = std::abs(static_cast<double>(wheel.load - previous[w]));
                    out.jumps.push_back(jump);
                    if (jump > out.max_jump) {
                        out.max_jump = jump;
                        out.max_jump_s = s;
                        out.max_jump_wheel = w;
                    }
                }
            }
            previous[w] = wheel.load;
        }
        have_previous = inside;
    }
    return out;
}

// Paso de la simulación con el conductor a 90 km/h por todo el tramo: media y p99 (ms).
PassCost drive_cost(const std::string& text, const rally::sim::StageTrack& track) {
    rally::sim::SimWorld world;
    world.build(
        irr_desc(text, rally::sim::stage_spawn(track, 0.0, static_cast<float>(50.0 / 3.6))));
    world.seal();
    rally::tests::StageDriver driver(track, 90.0 / 3.6);
    std::vector<double> ms;
    while (driver.s() < track.axis().length() && ms.size() < 200'000) {
        const rally::sim::SimInput in = driver.input(world.car_pose(), world.car_velocity());
        const auto t0 = Clock::now();
        world.step(in);
        const auto t1 = Clock::now();
        ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    return summarize(ms);
}

// Espectro de lo construido (criterio 8): cada componente medida en los vértices del núcleo de
// las secciones de la malla en [from, to] (donde la escala es 1 y nada se apaga), frente a lo
// pedido: su mayor |valor| y su longitud de onda media por los cruces por cero (2 × la distancia
// media entre cruces).
void spectrum_report(const rally::sim::StageTrack& track, const rally::sim::StageGround& g,
                     double from, double to) {
    const std::size_t per = g.vertices_per_section();
    const std::size_t n = (per - rally::sim::kStageCoreVertices - 2) / 2;
    const char* names[] = {"eje d",      "ancho izq.",    "ancho der.",  "borde izq.",
                           "borde der.", "peralte β (%)", "ondulación z"};
    std::vector<double> s_of;
    std::vector<std::array<double, 7>> values;
    for (std::size_t i = 0; i < g.sections().size(); ++i) {
        const double s = g.sections()[i];
        if (s < from || s > to) {
            continue;
        }
        const rally::sim::StagePose p = track.pose(s);
        const double nx = std::cos(p.heading);
        const double nz = std::sin(p.heading);
        double t[5];
        double y[5];
        for (std::size_t j = 0; j < 5; ++j) {
            const rally::sim::StageVertex& v = g.vertices()[i * per + n + 1 + j];
            const double x = v.x + g.origin_x();
            const double z = v.z + g.origin_z();
            t[j] = (x - p.x) * nx + (z - p.z) * nz;
            y[j] = v.y + g.origin_y();
        }
        std::array<double, 7> o{};
        o[0] = t[2];
        o[1] = (t[2] - t[1]) - p.half_width_left;
        o[2] = (t[3] - t[2]) - p.half_width_right;
        o[3] = (t[1] - t[0]) - rally::sim::kStageShoulderWidth;
        o[4] = (t[4] - t[3]) - rally::sim::kStageShoulderWidth;
        const double bank = (y[3] - y[1]) / (t[3] - t[1]);
        o[5] = 100.0 * (bank - p.bank);
        // z = altura del centro − (y del esqueleto + su peralte·d).
        o[6] = y[2] - (p.y + p.bank * t[2]);
        s_of.push_back(s);
        values.push_back(o);
    }
    std::printf("  espectro en s %.0f–%.0f (%zu secciones):\n", from, to, s_of.size());
    for (int c = 0; c < 7; ++c) {
        double peak = 0.0;
        double mean = 0.0;
        for (const auto& o : values) {
            peak = std::max(peak, std::abs(o[c]));
            mean += o[c];
        }
        mean /= static_cast<double>(values.size());
        int crossings = 0;
        double first = 0.0;
        double last = 0.0;
        for (std::size_t i = 1; i < values.size(); ++i) {
            if ((values[i - 1][c] - mean) * (values[i][c] - mean) < 0.0) {
                const double s = s_of[i];
                first = crossings == 0 ? s : first;
                last = s;
                ++crossings;
            }
        }
        const double lambda = crossings > 1 ? 2.0 * (last - first) / (crossings - 1) : 0.0;
        std::printf("    %-14s pico %8.4f, λ media %6.1f m (%d cruces)\n", names[c], peak, lambda,
                    crossings);
    }
}

// Los límites de la validación: lo medido (muestreado a λmin/16) frente a la cota analítica en todo
// el tramo y al límite.
void limits_report(const char* name, const rally::sim::StageTrack& track) {
    using rally::sim::StageChannel;
    const rally::sim::StageIrregularity& irr = track.irregularity();
    struct Row {
        StageChannel c;
        bool second;
        double limit;
        const char* what;
    };
    const Row rows[] = {
        {StageChannel::Axis, true, rally::sim::kStageAxisOffsetMaxCurvature, "|d''| (m⁻¹)"},
        {StageChannel::Height, true, rally::sim::kStageHeightMaxCurvature, "|z''| (m⁻¹)"},
        {StageChannel::Bank, false, rally::sim::kStageBankMaxRate, "|β'| (1/m)"},
        {StageChannel::EdgeLeft, true, 0.0, "|ε''| izq. (m⁻¹)"},
    };
    for (const Row& r : rows) {
        const double step = std::min(0.25, irr.wavelength_min(r.c) / 16.0);
        double worst = 0.0;
        double worst_s = 0.0;
        for (double s = track.s_begin(); s <= track.s_end(); s += step) {
            const rally::sim::StageJet j = irr.jet(r.c, s);
            const double v = std::abs(r.second ? j.d2 : j.d1);
            if (v > worst) {
                worst = v;
                worst_s = s;
            }
        }
        const rally::sim::StageChannelBound b = irr.bound(r.c, track.s_begin(), track.s_end());
        std::printf("  límites %s: %-18s medido %.5f (s %.1f), cota %.5f, límite %.5f\n", name,
                    r.what, worst, worst_s, r.second ? b.d2 : b.d1, r.limit);
    }
}

void straights_report(const std::string& plain, const rally::sim::StageTrack& lazadas,
                      const std::string& irregular, const rally::sim::StageTrack& bumpy);

void irregularity_report(bool only_straights) {
    const std::string& plain = rally::tests::lazadas_stage_text();
    const std::string& irregular = rally::tests::lazadas_irregular_stage_text();
    std::printf("== irregularidad, parte A ==\n");
    if (only_straights) {
        const rally::sim::StageTrack lazadas = must_build(plain, "lazadas");
        const rally::sim::StageTrack bumpy = must_build(irregular, "lazadas_irregular");
        limits_report("lazadas_irregular", bumpy);
        straights_report(plain, lazadas, irregular, bumpy);
        return;
    }

    // P1.
    {
        const std::string zero = rally::tests::with_zero_irregularity(plain);
        const rally::sim::StageTrack track = must_build(zero, "lazadas con la sección a cero");
        const rally::sim::StageGround g = rally::sim::StageGround::build(track);
        const std::uint64_t ground = g.hash();
        const std::uint64_t state = reproducibility_hash(zero);
        const std::uint64_t state_plain = reproducibility_hash(plain);
        std::printf("P1: lazadas en formato 2 con la sección a cero: suelo %s (esperado "
                    "0xacdcd1b800bb9d7d: %s), estado %s (lazadas.stage: %s; esperado "
                    "0x3d08dcb86b41ad6f: %s)\n",
                    hex(ground).c_str(), ground == 0xacdc'd1b8'00bb'9d7dull ? "igual" : "DISTINTO",
                    hex(state).c_str(), hex(state_plain).c_str(),
                    state == 0x3d08'dcb8'6b41'ad6full ? "igual" : "DISTINTO");
        std::printf("    hash del eje: lazadas %s, con la sección a cero %s\n",
                    hex(must_build(plain, "lazadas").axis().hash()).c_str(),
                    hex(track.axis().hash()).c_str());
    }

    const auto t0 = Clock::now();
    const rally::sim::StageTrack lazadas = must_build(plain, "lazadas");
    const auto t1 = Clock::now();
    const rally::sim::StageTrack bumpy = must_build(irregular, "lazadas_irregular");
    const auto t2 = Clock::now();
    std::printf("StageTrack::build: lazadas %.1f ms, lazadas_irregular %.1f ms\n",
                std::chrono::duration<double, std::milli>(t1 - t0).count(),
                std::chrono::duration<double, std::milli>(t2 - t1).count());
    const rally::sim::StageGround g_plain = rally::sim::StageGround::build(lazadas);
    const rally::sim::StageGround g_bumpy = rally::sim::StageGround::build(bumpy);
    std::printf("hash del suelo de lazadas_irregular: %s; sin la irregularidad "
                "(stage_text_without_irregularity): %s (lazadas.stage: %s)\n",
                hex(g_bumpy.hash()).c_str(),
                hex(rally::sim::StageGround::build(
                        must_build(rally::sim::stage_text_without_irregularity(irregular), "sin"))
                        .hash())
                    .c_str(),
                hex(g_plain.hash()).c_str());

    // P2.
    curvature_report("lazadas_irregular", bumpy);
    limits_report("lazadas_irregular", bumpy);
    for (const auto& [label, ground] :
         {std::pair<const char*, const rally::sim::StageGround*>{"lazadas", &g_plain},
          {"lazadas_irregular", &g_bumpy}}) {
        const rally::tests::BandGeometry b = rally::tests::band_geometry(*ground);
        std::printf("  P2/P4 %s: %d triángulos invertidos, %d sin área, %d de más de 80°; muro "
                    "más empinado %.2f° en s %.2f (lado %+d, frente a s %.2f)\n",
                    label, b.inverted, b.zero_area, b.over_80,
                    std::atan(b.worst.slope) * 180.0 / 3.14159265358979323846, b.worst.s,
                    b.worst.side, b.worst.other_s);
    }
    {
        const std::string text =
            rally::tests::with_irregularity_of_lazadas(rally::tests::synthetic_stage_30km());
        rally::sim::StageTrack track;
        const auto a = Clock::now();
        const rally::sim::StageParseResult r = rally::sim::StageTrack::build(text, track);
        const auto b = Clock::now();
        if (!r.ok) {
            std::printf("  P2 sintético 30 km con la sección: NO CARGA: %s\n", r.error.c_str());
        } else {
            curvature_report("sintético 30 km", track);
            limits_report("sintético 30 km", track);
            const rally::sim::StageGround g = rally::sim::StageGround::build(track);
            const auto c = Clock::now();
            const rally::tests::BandGeometry geo = rally::tests::band_geometry(g);
            std::printf("  P2/P5 sintético 30 km: %d invertidos; %zu secciones, %zu triángulos; "
                        "StageTrack::build %.1f s y StageGround::build %.1f s\n",
                        geo.inverted, g.sections().size(), g.triangles().size(),
                        std::chrono::duration<double>(b - a).count(),
                        std::chrono::duration<double>(c - b).count());
        }
    }

    // P4.
    std::printf("P4: stage_band_error de lazadas_irregular: «%s»\n",
                rally::sim::stage_band_error(bumpy).c_str());

    // P5.
    for (const auto& [label, text, track, ground] :
         {std::tuple<const char*, const std::string*, const rally::sim::StageTrack*,
                     const rally::sim::StageGround*>{"lazadas", &plain, &lazadas, &g_plain},
          {"lazadas_irregular", &irregular, &bumpy, &g_bumpy}}) {
        std::vector<double> loads;
        std::size_t bytes = 0;
        for (int i = 0; i < 3; ++i) {
            rally::sim::SimWorld world;
            const auto a = Clock::now();
            world.build(irr_desc(*text, rally::sim::stage_spawn(*track, 0.0, 0.0f)));
            world.seal();
            const auto b = Clock::now();
            loads.push_back(std::chrono::duration<double>(b - a).count());
            bytes = world.stage_ground_bytes();
        }
        std::sort(loads.begin(), loads.end());
        std::printf("P5 %s: %zu secciones, %zu triángulos, %zu baldosas, %.2f MB de Jolt; carga "
                    "(build + seal, mediana de 3) %.3f s\n",
                    label, ground->sections().size(), ground->triangles().size(),
                    ground->tiles().size(), static_cast<double>(bytes) / (1024.0 * 1024.0),
                    loads[1]);
    }

    // P6/P7: alternando, tres pasadas de cada.
    {
        std::vector<double> means[2];
        std::vector<double> p99s[2];
        for (int pass = 0; pass < 3; ++pass) {
            for (int k = 0; k < 2; ++k) {
                const PassCost c = drive_cost(k == 0 ? plain : irregular, k == 0 ? lazadas : bumpy);
                means[k].push_back(c.mean);
                p99s[k].push_back(c.p99);
            }
        }
        for (int k = 0; k < 2; ++k) {
            std::sort(means[k].begin(), means[k].end());
            std::sort(p99s[k].begin(), p99s[k].end());
        }
        std::printf("P6/P7: paso a 90 km/h (mediana de 3, alternando): lazadas media %.4f ms, p99 "
                    "%.4f ms; lazadas_irregular media %.4f ms, p99 %.4f ms (x%.2f y x%.2f)\n",
                    means[0][1], p99s[0][1], means[1][1], p99s[1][1], means[1][1] / means[0][1],
                    p99s[1][1] / p99s[0][1]);
    }

    straights_report(plain, lazadas, irregular, bumpy);

    // P9.
    std::printf("P9:\n");
    rally::tests::seam_report("lazadas_irregular", irregular, 0.040);
    {
        const rally::tests::SeamScan scan = rally::tests::seam_scan(irregular, 0.040);
        std::printf("  casos aviso: %d lados; el mayor escalón %.1f mm (s %.2f), el menor margen "
                    "%.1f mm (s %.2f)\n",
                    scan.warned_sides, 1000.0 * scan.warned_worst_step, scan.warned_worst_step_s,
                    1000.0 * scan.warned_lowest, scan.warned_lowest_s);
    }

    // Espectro.
    spectrum_report(bumpy, g_bumpy, 0.0, 640.0);
}

// P3: las rectas de lazadas a 60, 90 y 120 km/h, lazadas frente a lazadas_irregular en la misma
// ejecución.
void straights_report(const std::string& plain, const rally::sim::StageTrack& lazadas,
                      const std::string& irregular, const rally::sim::StageTrack& bumpy) {
    struct Straight {
        const char* name;
        double spawn, from, to;
    };
    const Straight straights[] = {
        // La salida, 10–40 m antes de lo que se mide y en recta: el coche ya está asentado.
        {"salida (s 0–100)", -19.0, 0.0, 100.0},
        {"recta de la cresta sin la cresta (s 260–290)", 250.0, 260.0, 290.0},
        {"cresta (s 290–350)", 250.0, 290.0, 350.0},
        {"recta de meta, mal estado ×1,5 (s 1040–1400)", 1000.0, 1040.0, 1400.0},
    };
    std::printf("P3: rectas, %% de pasos-rueda sin contacto y p99 (máximo) del salto de carga "
                "(N), lazadas | lazadas_irregular:\n");
    for (const double kmh : {60.0, 90.0, 120.0}) {
        for (const Straight& st : straights) {
            StraightStats a = straight_pass(plain, lazadas, st.spawn, st.from, st.to, kmh);
            StraightStats b = straight_pass(irregular, bumpy, st.spawn, st.from, st.to, kmh);
            std::printf("  %5.0f km/h %-46s %6.3f %% | %6.3f %%   p99 %6.1f (%6.1f en s %.1f, "
                        "rueda %d) | %6.1f (%6.1f en s %.1f)   (%d / %d pasos-rueda)\n",
                        kmh, st.name, a.percent(), b.percent(), a.p99(), a.max_jump, a.max_jump_s,
                        a.max_jump_wheel, b.p99(), b.max_jump, b.max_jump_s, a.wheel_steps,
                        b.wheel_steps);
        }
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
    if (mode == "f2trace" && argc >= 5) {
        // Premisa PB: lo que decide la altura de las muestras de la rejilla a menos de 1,1 m del
        // final del lado `side` en s (lazadas o 30km), y la malla en el final.
        const std::string which = argv[2];
        const std::string text = which == "30km" ? rally::tests::synthetic_stage_30km()
                                 : which == "hairpin"
                                     ? rally::tests::stage_file("hairpin_flat.stage")
                                     : rally::tests::lazadas_stage_text();
        const double s = std::stod(argv[3]);
        const int side = std::stoi(argv[4]);
        rally::sim::StageTrack track;
        rally::sim::StageTrack::build(text, track);
        const rally::sim::StageGround plain = rally::sim::StageGround::build(track);
        const rally::sim::StageBandSide* found = nullptr;
        for (const rally::sim::StageBandSide& b : plain.band_sides()) {
            if (b.side == side &&
                (found == nullptr || std::abs(b.s - s) < std::abs(found->s - s))) {
                found = &b;
            }
        }
        const rally::sim::StagePose p = track.pose(found->s);
        const double edge = side > 0 ? p.half_width_right : p.half_width_left;
        const double t = side * (edge + rally::sim::kStageShoulderWidth + found->end_u);
        const double fx = p.x + t * std::cos(p.heading);
        const double fz = p.z + t * std::sin(p.heading);
        std::printf("s %.3f lado %+d %s: final en t %.3f (%.3f, %.3f), y %.4f, end_u %.3f, "
                    "pendiente %.3f\n",
                    found->s, side, rally::tests::band_kind_name(found->kind), t, fx, fz,
                    found->end_y, found->end_u, found->slope);
        {
            // La malla (sus triángulos), el mundo y la rejilla sola, hacia dentro del final.
            rally::sim::WorldDesc desc;
            desc.stage_text = text;
            rally::sim::SimWorld world;
            world.build(desc);
            world.seal();
            desc.stage_variant = rally::sim::StageGroundVariant::TerrainOnly;
            rally::sim::SimWorld terrain;
            terrain.build(desc);
            terrain.seal();
            const rally::tests::MeshQuery query(plain);
            std::vector<double> hs;
            for (double depth = -0.5; depth <= 1.5001; depth += 0.125) {
                const double td = t - side * depth;
                const double x = p.x + td * std::cos(p.heading);
                const double z = p.z + td * std::sin(p.heading);
                query.heights(x, z, hs);
                std::printf("  %6.3f m dentro: mundo %9.4f rejilla %9.4f malla", depth,
                            world.probe_ground(x, z).y, terrain.probe_ground(x, z).y);
                for (const double y : hs) {
                    std::printf(" %9.4f", y);
                }
                std::printf("\n");
            }
        }
        std::vector<rally::sim::StageSampleTrace> traces;
        rally::sim::StageGround::trace(track, {fx, fz}, 1.1, traces);
        for (const rally::sim::StageSampleTrace& tr : traces) {
            const rally::sim::StageProjection q = track.acquire(tr.x, tr.z);
            std::printf("  muestra (%.3f, %.3f) s %.3f t %.3f: rejilla %.4f, N %.4f; %s%s d %.3f "
                        "junto %.4f bajo %.4f; zona %d:",
                        tr.x, tr.z, q.s, q.t, tr.height, tr.natural,
                        tr.core ? "bajo banda" : "fuera", tr.exposed ? " expuesta" : "", tr.core_d,
                        tr.core_band, tr.core_h, tr.zone_count);
            for (int k = 0; k < tr.zone_count && k < 8; ++k) {
                std::printf(" [d %.3f B %.4f]", tr.zone_d[k], tr.zone_band[k]);
            }
            std::printf("\n");
        }
        return 0;
    }
    if (mode == "irrsec") {
        // Irregularidad, parte B (premortem del §4 de la respuesta del consultor): las secciones de
        // lazadas, de lazadas_irregular y de su variante con el borde de 6–10 m, por 100 m de s, y
        // los lados de la banda por tipo.
        const std::string irregular = rally::tests::lazadas_irregular_stage_text();
        std::string wide = irregular;
        wide.replace(wide.find("edge 0.12 4 8"), 13, "edge 0.12 6 10");
        for (const auto& [name, text] : {std::pair<const char*, const std::string*>{
                                             "lazadas", &rally::tests::lazadas_stage_text()},
                                         {"lazadas_irregular", &irregular},
                                         {"lazadas_irregular, borde 6-10 m", &wide}}) {
            rally::sim::StageTrack track = must_build(*text, name);
            const rally::sim::StageGround g = rally::sim::StageGround::build(track);
            std::printf("%s: %zu secciones, %zu triángulos\n", name, g.sections().size(),
                        g.triangles().size());
            int bins[15] = {};
            for (const double s : g.sections()) {
                bins[std::clamp(static_cast<int>((s + 20.0) / 100.0), 0, 14)]++;
            }
            std::printf("   por 100 m desde s −20:");
            for (const int b : bins) {
                std::printf(" %d", b);
            }
            int kinds[5] = {};
            for (const rally::sim::StageBandSide& b : g.band_sides()) {
                kinds[static_cast<int>(b.kind)]++;
            }
            std::printf(
                "\n   lados: libres %d, enfrentados %d, rampa %d, abanico %d, recortados %d\n",
                kinds[0], kinds[1], kinds[2], kinds[3], kinds[4]);
        }
        return 0;
    }
    if (mode == "irrdiag") {
        // P4 y P5: lazadas_irregular con cada componente sola (las demás a amplitud 0; la escala,
        // la del archivo) o con todas: ¿carga?, el muro más empinado y las secciones.
        const std::string base = rally::tests::lazadas_irregular_stage_text();
        const char* keys[] = {"axis", "width", "edge", "bank", "height"};
        const std::size_t section = base.find("[irregularity]");
        for (int keep = -2; keep < 5; ++keep) {
            std::string text = base;
            for (int k = 0; k < 5; ++k) {
                if (keep == -1 || k == keep) {
                    continue;
                }
                const std::size_t at = text.find(std::string("\n") + keys[k] + " ", section);
                const std::size_t value = at + 1 + std::string(keys[k]).size() + 1;
                text.replace(value, text.find(' ', value) - value, "0");
            }
            rally::sim::StageTrack track;
            const rally::sim::StageParseResult r = rally::sim::StageTrack::build(text, track);
            std::printf("%-8s: %s",
                        keep == -2   ? "ninguna"
                        : keep == -1 ? "todas"
                                     : keys[keep],
                        r.ok ? "carga" : r.error.c_str());
            if (r.ok) {
                const rally::sim::StageGround g = rally::sim::StageGround::build(track);
                const rally::tests::BandGeometry b = rally::tests::band_geometry(g);
                std::printf("; muro más empinado %.2f° en s %.2f; %zu secciones, %zu triángulos",
                            std::atan(b.worst.slope) * 180.0 / 3.14159265358979323846, b.worst.s,
                            g.sections().size(), g.triangles().size());
            }
            std::printf("\n");
        }
        return 0;
    }
    if (mode == "irr" || mode == "irr3") {
        // Irregularidad, parte A: las premisas P1–P9 y el espectro («irr3»: solo los límites y
        // P3).
        irregularity_report(mode == "irr3");
        return 0;
    }
    if (mode == "f2seam") {
        // Invariante 7b: el escalón banda–rejilla en toda la costura, y los de más de 40 mm.
        const double limit = argc >= 3 ? std::stod(argv[2]) / 1000.0 : 0.040;
        rally::tests::seam_report("lazadas", rally::tests::lazadas_stage_text(), limit);
        rally::tests::seam_report("hairpin_flat", rally::tests::stage_file("hairpin_flat.stage"),
                                  limit);
        rally::tests::seam_report("peralte 10 %", rally::tests::max_bank_stage(), limit);
        rally::tests::seam_report("sintético 30 km", rally::tests::synthetic_stage_30km(), limit);
        return 0;
    }
    if (g_format2) {
        rally::tests::band_report();
    }
    if (mode == "f2geo") {
        return 0;
    }
    if (mode == "f2edges") {
        // Premisa PA: el ángulo entre caras vecinas, por categorías, con 18, 36, 38 y 72 tramos.
        const double active = argc >= 3 ? std::stod(argv[2]) : 5.0;
        rally::tests::edge_report(rally::tests::lazadas_stage_text(), active);
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
        const rally::sim::StageParseResult built = rally::sim::StageTrack::build(text, track);
        if (!built.ok) {
            std::printf("no carga: %s\n", built.error.c_str());
        }
        const double s0 = std::stod(argv[3]);
        const double s1 = std::stod(argv[4]);
        const rally::sim::StageCellError cell_error(track);
        for (const rally::sim::StageBandSide& b : track.band_cache()->sides) {
            if (b.s < s0 || b.s > s1) {
                continue;
            }
            const rally::sim::StagePose p = track.pose(b.s);
            std::printf(
                "s %9.3f lado %+d %-10s delta %8.4f enfr %9.3f end_u %7.3f cabeza %8.3f "
                "final %8.3f pend %6.3f kappa %8.5f e %6.1f mm (sin suavizar %6.1f) peralte %7.4f "
                "pendiente %7.4f\n",
                b.s, b.side, rally::tests::band_kind_name(b.kind), b.delta, b.other_s, b.end_u,
                b.rim_y, b.end_y, b.slope, p.curvature, 1000.0 * cell_error.at(b.s, b.side),
                1000.0 * cell_error.raw_at(b.s), p.bank, p.grade);
        }
        return 0;
    }
    relief_cost();
    measure("lazadas.stage", rally::tests::lazadas_stage_text(), 0.0, 1150.0);
    measure("sintético 30 km", rally::tests::synthetic_stage_30km(), 1000.0, 4000.0);
    return 0;
}
