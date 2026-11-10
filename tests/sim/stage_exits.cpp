// Medida a mano de las salidas guionizadas (tarea formato-2, lote 6; criterios 1 y 11 del
// prompt): la tabla del lote 5 (rueda sin contacto y p99 del salto de carga por bandas de
// distancia fuera del borde de la calzada) por las salidas de common/stage_exits.h, en lazadas,
// para cada suelo pedido y su referencia de malla densa. Se lanza a mano en Release
// (build/windows-release/tests/sim/Release/rally_stage_exits.exe [variantes]); sin argumentos,
// Design y DenseMesh.

#include "stage_exits.h"

#include "sim/jolt_runtime.h"

#include <algorithm>
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

// Band y BandDense: el prototipo del formato 2 de la parte A (la malla con la banda sobre la
// rejilla del formato 1, que tapa la cuneta): solo para el coste del paso, no para las cifras del
// coche fuera de la calzada (revisión del plan, cambio 9).
constexpr Named kVariants[] = {
    {"Design", StageGroundVariant::Design},
    {"DenseMesh", StageGroundVariant::DenseMesh},
    {"Band", StageGroundVariant::Band},
    {"BandDense", StageGroundVariant::BandDense},
};
constexpr int kDefaultVariants = 2;

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
    std::vector<Named> chosen;
    for (int a = 1; a < argc; ++a) {
        for (const Named& v : kVariants) {
            if (std::strcmp(argv[a], v.name) == 0) {
                chosen.push_back(v);
            }
        }
    }
    if (chosen.empty()) {
        chosen.assign(std::begin(kVariants), std::begin(kVariants) + kDefaultVariants);
    }
    for (const Named& v : chosen) {
        std::printf("\n== %s ==\n", v.name);
        std::printf("%-24s | %4s | %16s | %16s | %8s | %8s | %s\n", "salida", "km/h",
                    "0–4 m: % / p99", "4–15 m: % / p99", "más lejos", "impacto", "recolocado");
        rally::tests::ExitStats all;
        std::vector<double> step_ms;
        for (const rally::tests::ExitPlace& place : rally::tests::kExitPlaces) {
            for (const double kmh : rally::tests::kExitSpeedsKmh) {
                const rally::tests::ExitStats r =
                    rally::tests::run_exit(text, track, v.variant, place, kmh, &step_ms);
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
