#include "platform/forest_bench.h"

#include "platform/box_demo.h"
#include "platform/forest_layout.h"
#include "platform/frame_pacer.h"
#include "platform/perf_counter.h"

#include <raylib.h>

#include <algorithm>
#include <cstdio>

namespace rally::platform {

namespace {

constexpr std::array<ForestMode, ForestBench::kModes> kOrder{
    ForestMode::Off, ForestMode::Single, ForestMode::RaylibInstanced, ForestMode::Persistent};

double ms(double ns) { return ns / 1e6; }
double mb(std::uint64_t bytes) { return static_cast<double>(bytes) / (1024.0 * 1024.0); }

} // namespace

ForestBench::ForestBench(BoxDemo& demo) { demo.set_forest_mode(kOrder[0]); }

bool ForestBench::update(const PerfCounter& perf, const FramePacer& pacer, BoxDemo& demo) {
    if (mode_ >= kModes) {
        return false;
    }
    const auto index = static_cast<std::size_t>(mode_);
    if (captured_) {
        // La captura se guardó en el fotograma anterior: siguiente modo, que empieza con su
        // calentamiento (absorbe lo que ha tardado la captura).
        captured_ = false;
        ++mode_;
        elapsed_ns_ = 0;
        if (mode_ >= kModes) {
            write_report(perf, pacer, demo);
            return false;
        }
        demo.set_forest_mode(kOrder[static_cast<std::size_t>(mode_)]);
        return true;
    }

    const FrameSample& sample = perf.last_sample();
    elapsed_ns_ += sample.frame_ns;
    if (elapsed_ns_ > kWarmupNs) {
        totals_[index].add(sample);
        ProcessMemory& memory = memory_[index];
        memory.working_set = std::max(memory.working_set, perf.memory().working_set);
        memory.private_bytes = std::max(memory.private_bytes, perf.memory().private_bytes);
    }
    if (elapsed_ns_ >= kWarmupNs + kMeasureNs) {
        demo.request_capture();
        captured_ = true;
    }
    return true;
}

void ForestBench::write_report(const PerfCounter& perf, const FramePacer& pacer,
                               const BoxDemo& demo) {
    int number = 0;
    while (FileExists(TextFormat("%s/rendimiento_%03d.txt", GetApplicationDirectory(), number))) {
        ++number;
    }
    std::FILE* file = nullptr;
    if (fopen_s(&file, TextFormat("%s/rendimiento_%03d.txt", GetApplicationDirectory(), number),
                "w") != 0 ||
        file == nullptr) {
        return;
    }
    std::fprintf(file, "# Medida del bosque denso (--bench-forest)\n\n");
    std::fprintf(file, "- Gráfica: %s\n", perf.renderer());
    std::fprintf(file, "- Render: %d×%d en RenderTexture, mostrado escalado a %d×%d\n",
                 demo.render_width(), demo.render_height(), GetScreenWidth(), GetScreenHeight());
    std::fprintf(file, "- Monitor: %.3f Hz; sincronización vertical: %s; intervalo %d\n",
                 static_cast<double>(pacer.refresh_rate()), pacer.vsync() ? "sí" : "no",
                 pacer.swap_interval());
    std::fprintf(file, "- Árboles: %d de %d triángulos, anillo de %.0f a %.0f m\n", kForestTrees,
                 kTreeTriangles, static_cast<double>(kForestInnerRadius),
                 static_cast<double>(kForestOuterRadius));
    std::fprintf(file,
                 "- Por modo: %.0f s de calentamiento y %.0f s de medida; cámara en primera "
                 "persona en el recinto; contador visible (su texto cuenta en las draw calls)\n\n",
                 static_cast<double>(kWarmupNs) / 1e9, static_cast<double>(kMeasureNs) / 1e9);
    std::fprintf(file, "Media / máximo por fotograma (GPU: por resultado recibido; paso: por paso "
                       "de física).\n\n");
    std::fprintf(file, "| Modo | Fotogramas | Fotograma ms | CPU simulación ms | CPU por paso ms "
                       "| CPU render ms | Presentación ms | Resto ms | GPU ms | Draw calls "
                       "| Instancias | Working set MB (máx.) | Privados MB (máx.) |\n");
    std::fprintf(file, "| --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- | --- "
                       "| --- |\n");
    for (std::size_t i = 0; i < kOrder.size(); ++i) {
        const PerfTotals& t = totals_[i];
        const auto pair = [](const RunningTotal& total) {
            return TextFormat("%.2f / %.2f", ms(total.mean()), ms(static_cast<double>(total.max)));
        };
        // TextFormat rota entre 4 búferes: cada fila se escribe en varias llamadas.
        std::fprintf(file, "| %s | %lld | %s | ", forest_mode_name(kOrder[i]),
                     static_cast<long long>(t.frames), pair(t.frame));
        std::fprintf(file, "%s | %s | ", pair(t.sim), pair(t.step));
        std::fprintf(file, "%s | %s | ", pair(t.render), pair(t.present));
        std::fprintf(file, "%s | %s | ", pair(t.other), pair(t.gpu));
        std::fprintf(file, "%.1f / %lld | %.0f | %.1f | %.1f |\n", t.draw_calls.mean(),
                     static_cast<long long>(t.draw_calls.max), t.instances.mean(),
                     mb(memory_[i].working_set), mb(memory_[i].private_bytes));
    }
    std::fclose(file);
}

} // namespace rally::platform
