#include "platform/stage_hud.h"

#include "platform/ui_text.h"
#include "sim/sim_world.h"

#include <algorithm>
#include <cinttypes>
#include <cstdio>

namespace rally::platform {

namespace {

std::string format(const char* pattern, auto... values) {
    char text[256];
    std::snprintf(text, sizeof text, pattern, values...);
    return text;
}

} // namespace

std::string clock_text(std::int64_t steps) {
    if (steps < 0) {
        return "--";
    }
    const std::int64_t centis = steps * 100 / sim::kStepsPerSecond;
    return format("%" PRId64 ":%02" PRId64 ",%02" PRId64, centis / 6000, centis / 100 % 60,
                  centis % 100);
}

std::string hud_base_text(double speed_ms, std::string_view gear, bool sequential, double rpm,
                          double boost_pa, std::string_view car_name) {
    std::string text = format("%.0f km/h   %.*s %s   %4.0f rpm   %.2f bar", speed_ms * 3.6,
                              static_cast<int>(gear.size()), gear.data(),
                              sequential ? "sec." : "auto.", rpm, boost_pa * 1.0e-5);
    if (!car_name.empty()) {
        text += "   ";
        text += car_name;
    }
    return text;
}

std::int64_t run_clock_steps(const sim::StageRunState& run) {
    if (run.start_step < 0) {
        return 0;
    }
    if (run.finish_step >= 0) {
        return run.finish_step - run.start_step;
    }
    return run.steps - run.start_step;
}

std::string short_stage_name(std::string_view name, int max_chars) {
    // El presupuesto es el ancho de `max_chars` cifras (a 10 px; los demás tamaños son múltiplos).
    const int budget = text_width(std::string(static_cast<std::size_t>(max_chars), '8'), 10);
    bool ellipsis = false;
    const std::size_t bytes = fit_text(name, 10, budget, ellipsis);
    if (!ellipsis) {
        return std::string(name);
    }
    return std::string(name.substr(0, bytes)) + "...";
}

std::string stage_indicator_text(std::string_view name, const sim::StageRunState& run) {
    if (run.finish_step >= 0) {
        return format("meta %s · recol. %d · atajos %d", clock_text(run_clock_steps(run)).c_str(),
                      run.resets, run.shortcuts);
    }
    return "tramo " + short_stage_name(name) + " · " + clock_text(run_clock_steps(run));
}

std::string stage_debug_line(const sim::StageRunState& run, double length) {
    return format("tramo: s %.1f / %.0f m, t %.1f m, %s", run.s, length, run.t,
                  run.on_road ? "en carretera" : "fuera");
}

std::string stage_debug_counts_line(const sim::StageRunState& run) {
    return format("recolocación en s = %.1f m · recol. %d · atajos %d", run.reset_s, run.resets,
                  run.shortcuts);
}

StageMessages StageEvents::observe(const sim::StageRunState& run) {
    StageMessages out;
    // Otro mundo sin reset(): los contadores bajan.
    if (run.resets < resets_ || run.shortcuts < shortcuts_ || run.split_count < splits_ ||
        (finished_ && run.finish_step < 0)) {
        reset();
    }
    const auto add = [&out](const std::string& text) {
        if (!out.events.empty()) {
            out.events += " · ";
        }
        out.events += text;
    };
    for (std::int32_t k = splits_; k < run.split_count; ++k) {
        add(format(
            "parcial %d: %s", k + 1,
            clock_text(run.split_steps[k] - std::max<std::int64_t>(run.start_step, 0)).c_str()));
    }
    const std::int32_t new_resets = run.resets - resets_;
    if (new_resets == 1) {
        add(format("recolocación en s = %.0f m", run.reset_s));
    } else if (new_resets > 1) {
        add(format("%d recolocaciones, la última en s = %.0f m", new_resets, run.reset_s));
    }
    const std::int32_t new_shortcuts = run.shortcuts - shortcuts_;
    if (new_shortcuts == 1) {
        add(format("atajo: recolocado en s = %.0f m", run.exit_s));
    } else if (new_shortcuts > 1) {
        add(format("%d atajos: recolocado en s = %.0f m", new_shortcuts, run.exit_s));
    }
    if (run.finish_step >= 0 && !finished_) {
        out.finish = format("meta: %s · recol. %d · atajos %d",
                            clock_text(run_clock_steps(run)).c_str(), run.resets, run.shortcuts);
    }
    splits_ = run.split_count;
    resets_ = run.resets;
    shortcuts_ = run.shortcuts;
    finished_ = run.finish_step >= 0;
    return out;
}

} // namespace rally::platform
