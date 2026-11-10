// Medida a mano de las salidas guionizadas (tarea formato-2, lote 6; criterios 1a, 1b y 11): la
// tabla del lote 5 (rueda sin contacto y p99 del salto de carga por bandas de distancia fuera del
// borde de la calzada) por las salidas de common/stage_exits.h, en lazadas, para cada suelo pedido.
// Se lanza a mano en Release (build/windows-release/tests/sim/Release/rally_stage_exits.exe
// [Design] [DenseMesh] [sin-microrrelieve]); sin suelos, Design y DenseMesh. La tabla del formato 1
// (mensajes/formato-2/salidas-formato1.txt) se midió con este mismo programa antes de la parte B.

#include "stage_exits.h"

#include "sim/jolt_runtime.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

using rally::sim::StageGroundVariant;

struct Named {
    const char* name;
    StageGroundVariant variant;
};

constexpr Named kVariants[] = {
    {"Design", StageGroundVariant::Design},
    {"DenseMesh", StageGroundVariant::DenseMesh},
};

void print_band(const char* name, const rally::tests::ExitBand& b) {
    std::printf("  %-26s | %9.1f s | %7.2f %% | %8.0f N\n", name,
                b.wheel_steps / 4.0 / rally::sim::kStepsPerSecond, b.percent(), b.p99());
}

} // namespace

int main(int argc, char** argv) {
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    const rally::sim::JoltRuntime runtime;
    const std::string& text = rally::tests::lazadas_stage_text();
    rally::sim::StageTrack track;
    const rally::sim::StageParseResult parsed = rally::sim::StageTrack::build(text, track);
    if (!parsed.ok) {
        std::printf("lazadas no vale: %s\n", parsed.error.c_str());
        return 1;
    }
    if (argc > 1 && std::strcmp(argv[1], "bins") == 0) {
        // Diagnóstico de 1b: en la recta y la curva de R 80, los pasos-rueda sin contacto por la
        // distancia de la propia rueda fuera del borde de la calzada, cada 0,5 m de 0 a 10 m.
        for (std::size_t p = 0; p < 2; ++p) {
            for (const double kmh : rally::tests::kExitSpeedsKmh) {
                const rally::tests::ExitPlace& place = rally::tests::kExitPlaces[p];
                constexpr double kPi = 3.14159265358979323846;
                const double speed = kmh / 3.6;
                const rally::sim::StagePose pose = track.pose(place.s);
                const double heading = pose.heading + place.turn_deg * kPi / 180.0;
                rally::sim::CarSpawn spawn =
                    rally::sim::stage_spawn(track, place.s, static_cast<float>(speed));
                spawn.x = pose.x + place.t * std::cos(pose.heading);
                spawn.z = pose.z + place.t * std::sin(pose.heading);
                spawn.y = pose.y + pose.bank * place.t;
                spawn.yaw = static_cast<float>(-heading);
                rally::sim::WorldDesc desc;
                desc.stage_text = text;
                desc.car = true;
                desc.car_data = rally::tests::reference_car();
                desc.car_spawn = spawn;
                rally::sim::SimWorld world;
                world.build(desc);
                world.seal();
                world.set_telemetry(true);
                int steps_in[20] = {};
                int lost_in[20] = {};
                const auto steps = static_cast<int>(place.seconds * rally::sim::kStepsPerSecond);
                for (int i = 0; i < steps; ++i) {
                    const rally::sim::BodyPose bp = world.car_pose();
                    const rally::sim::BodyVelocity v = world.car_velocity();
                    rally::sim::SimInput in;
                    in.steer = static_cast<std::int16_t>(
                        std::clamp(
                            2.5 * rally::tests::wrap_angle(heading - rally::tests::car_heading(bp)),
                            -1.0, 1.0) *
                        rally::sim::kAxisMax);
                    in.throttle = static_cast<std::int16_t>(
                        std::clamp(0.5 * (speed - std::hypot(v.vx, v.vz)), 0.0, 1.0) *
                        rally::sim::kAxisMax);
                    world.step(in);
                    if (i < rally::sim::kStepsPerSecond / 4) {
                        continue;
                    }
                    const rally::sim::BodyPose after = world.car_pose();
                    const double yaw = -rally::tests::car_heading(after);
                    for (int w = 0; w < rally::sim::kWheelCount; ++w) {
                        const rally::sim::SimWorld::WheelPose wp = world.car_wheel(w);
                        const double wx =
                            after.x + wp.attach_x * std::cos(yaw) + wp.attach_z * std::sin(yaw);
                        const double wz =
                            after.z - wp.attach_x * std::sin(yaw) + wp.attach_z * std::cos(yaw);
                        const double d = rally::tests::outside_road_edge(track, wx, wz);
                        const int bin = static_cast<int>(std::floor(d / 0.5));
                        if (bin < 0 || bin >= 20) {
                            continue;
                        }
                        ++steps_in[bin];
                        lost_in[bin] +=
                            world.telemetry().wheels[static_cast<std::size_t>(w)].contact == 0 ? 1
                                                                                               : 0;
                    }
                }
                std::printf(
                    "%s a %.0f km/h, rueda sin contacto por su distancia fuera del borde:\n",
                    place.name, kmh);
                for (int b = 0; b < 20; ++b) {
                    if (steps_in[b] > 0) {
                        std::printf("  %4.1f–%4.1f m: %5d pasos-rueda, %6.2f %%\n", 0.5 * b,
                                    0.5 * (b + 1), steps_in[b], 100.0 * lost_in[b] / steps_in[b]);
                    }
                }
            }
        }
        return 0;
    }
    std::vector<Named> chosen;
    bool micro = true;
    for (int a = 1; a < argc; ++a) {
        if (std::strcmp(argv[a], "sin-microrrelieve") == 0) {
            micro = false;
        }
        for (const Named& v : kVariants) {
            if (std::strcmp(argv[a], v.name) == 0) {
                chosen.push_back(v);
            }
        }
    }
    if (chosen.empty()) {
        chosen.assign(std::begin(kVariants), std::end(kVariants));
    }
    for (const Named& v : chosen) {
        std::printf("\n== %s, %s ==\n", v.name, micro ? "con microrrelieve" : "sin microrrelieve");
        std::printf("%-24s | %4s | %16s | %16s | %8s | %8s | %s\n", "salida", "km/h",
                    "0–4 m: % / p99", "4–15 m: % / p99", "más lejos", "impacto", "recolocado");
        rally::tests::ExitStats all;
        std::vector<double> step_ms;
        for (const rally::tests::ExitPlace& place : rally::tests::kExitPlaces) {
            for (const double kmh : rally::tests::kExitSpeedsKmh) {
                const rally::tests::ExitStats r =
                    rally::tests::run_exit(text, track, v.variant, place, kmh, &step_ms, micro);
                const rally::tests::ExitBand near = r.near_band();
                const rally::tests::ExitBand rest = r.rest_band();
                std::printf("%-24s | %4.0f | %6.2f %% %6.0f N | %6.2f %% %6.0f N | %6.1f m | %5.2f "
                            "m/s | %s\n",
                            place.name, kmh, near.percent(), near.p99(), rest.percent(), rest.p99(),
                            r.farthest, r.max_impact, r.placed ? "sí" : "no");
                all.add(r);
            }
        }
        std::printf("\nTodas las salidas juntas (%s):\n", v.name);
        std::printf("  %-26s | %11s | %9s | %10s\n", "fuera del borde", "tiempo", "sin cont.",
                    "p99 salto");
        for (int b = 0; b < rally::tests::kExitBands; ++b) {
            print_band(rally::tests::kExitBandNames[static_cast<std::size_t>(b)],
                       all.bands[static_cast<std::size_t>(b)]);
        }
        print_band("0–4 m (juntas)", all.near_band());
        print_band("4–15 m (juntas)", all.rest_band());
        double mean = 0.0;
        for (const double ms : step_ms) {
            mean += ms;
        }
        mean /= static_cast<double>(step_ms.size());
        std::sort(step_ms.begin(), step_ms.end());
        std::printf("  paso durante las salidas (%zu pasos): media %.4f ms, p99 %.4f ms, máximo "
                    "%.4f ms\n",
                    step_ms.size(), mean, step_ms[step_ms.size() * 99 / 100], step_ms.back());
    }
    return 0;
}
