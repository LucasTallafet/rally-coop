#include "platform/telemetry_history.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace rally::platform {

namespace {

constexpr float kRadToDeg = 57.29577951308232f;
constexpr float kMsToKmh = 3.6f;

float yaw_rate_deg(const sim::CarTelemetry& t) { return t.yaw_rate * kRadToDeg; }
float speed_kmh(const sim::CarTelemetry& t) { return t.speed * kMsToKmh; }
float steer(const sim::CarTelemetry& t) { return t.steer; }
float throttle(const sim::CarTelemetry& t) { return t.throttle; }
float brake(const sim::CarTelemetry& t) { return t.brake; }
float handbrake(const sim::CarTelemetry& t) { return t.handbrake; }
template <int W>
float load_kn(const sim::CarTelemetry& t) {
    return t.wheels[W].load * 0.001f;
}
template <int W>
float compression_cm(const sim::CarTelemetry& t) {
    return t.wheels[W].compression * 100.0f;
}
// Ángulo de deriva relajado medio de un eje (ruedas A y B), con el signo de beta: α_r es < 0
// con la rueda yendo hacia su derecha y beta > 0 con la velocidad a la derecha del morro.
template <int A, int B>
float axle_slip_deg(const sim::CarTelemetry& t) {
    return -0.5f * (t.wheels[A].alpha + t.wheels[B].alpha) * kRadToDeg;
}
float rpm_thousands(const sim::CarTelemetry& t) {
    return t.engine_omega * (30.0f / 3.14159265f) * 0.001f;
}
float gear(const sim::CarTelemetry& t) { return static_cast<float>(t.gear); }
float boost_bar(const sim::CarTelemetry& t) { return t.boost * 1.0e-5f; }
template <int W>
float kappa_percent(const sim::CarTelemetry& t) {
    return t.wheels[W].kappa * 100.0f;
}

// Página 1 · Manejo. Nombres sin β: la fuente por defecto de raylib no lo tiene. En el panel de
// beta, el ángulo de deriva relajado medio del eje delantero y del trasero (tarea 2.5), con el
// signo de beta: el delantero por encima del trasero es subviraje y por debajo, sobreviraje.
constexpr std::array kBetaChannels = {
    GraphChannel{"beta", graph_beta_deg, 0xffa040ffu},
    GraphChannel{"eje del.", axle_slip_deg<0, 1>, 0x60c0ffffu},
    GraphChannel{"eje tras.", axle_slip_deg<2, 3>, 0xff5050ffu},
};
constexpr std::array kYawChannels = {GraphChannel{"guiñada", yaw_rate_deg, 0x60c0ffffu}};
constexpr std::array kSpeedChannels = {GraphChannel{"velocidad", speed_kmh, 0xf0f0f0ffu}};
constexpr std::array kInputChannels = {
    // Nombres cortos: las cuatro etiquetas con su valor caben en una línea del panel.
    GraphChannel{"dir", steer, 0x60c0ffffu},
    GraphChannel{"acel", throttle, 0x60e060ffu},
    GraphChannel{"freno", brake, 0xff5050ffu},
    GraphChannel{"f. mano", handbrake, 0xffd040ffu},
};
constexpr std::array kHandlingPanels = {
    GraphPanel{"beta", "°", -90.0f, 90.0f, kBetaChannels},
    GraphPanel{"guiñada", "°/s", -150.0f, 150.0f, kYawChannels},
    GraphPanel{"velocidad", "km/h", 0.0f, 200.0f, kSpeedChannels},
    GraphPanel{"entradas", "", -1.0f, 1.0f, kInputChannels}, // aplicadas (ControlState)
};

// Página 3 · Suspensión (tarea 2.4): carga y compresión de la suspensión por rueda (desde la
// extensión máxima). Un color por rueda, el mismo en los dos paneles.
constexpr std::uint32_t kFrontLeftColor = 0x60c0ffffu;
constexpr std::uint32_t kFrontRightColor = 0x60e060ffu;
constexpr std::uint32_t kRearLeftColor = 0xffa040ffu;
constexpr std::uint32_t kRearRightColor = 0xff5050ffu;
constexpr std::array kLoadChannels = {
    GraphChannel{"DI", load_kn<0>, kFrontLeftColor},
    GraphChannel{"DD", load_kn<1>, kFrontRightColor},
    GraphChannel{"TI", load_kn<2>, kRearLeftColor},
    GraphChannel{"TD", load_kn<3>, kRearRightColor},
};
constexpr std::array kCompressionChannels = {
    GraphChannel{"DI", compression_cm<0>, kFrontLeftColor},
    GraphChannel{"DD", compression_cm<1>, kFrontRightColor},
    GraphChannel{"TI", compression_cm<2>, kRearLeftColor},
    GraphChannel{"TD", compression_cm<3>, kRearRightColor},
};
constexpr std::array kSuspensionPanels = {
    GraphPanel{"carga", "kN", 0.0f, 8.0f, kLoadChannels},
    GraphPanel{"compresión", "cm", 0.0f, 20.0f, kCompressionChannels},
};

// Página 2 · Tren motriz: RPM (en miles) y marcha (−1 R, 0 N, 1–5), presión del turbo (tarea
// 2.6) y deslizamiento longitudinal relajado κ_r por rueda (tarea 2.5).
constexpr std::array kEngineChannels = {
    GraphChannel{"rpm (miles)", rpm_thousands, 0xf0f0f0ffu},
    GraphChannel{"marcha", gear, 0xffd040ffu},
};
constexpr std::array kBoostChannels = {GraphChannel{"presión", boost_bar, 0x60c0ffffu}};
constexpr std::array kKappaChannels = {
    GraphChannel{"DI", kappa_percent<0>, kFrontLeftColor},
    GraphChannel{"DD", kappa_percent<1>, kFrontRightColor},
    GraphChannel{"TI", kappa_percent<2>, kRearLeftColor},
    GraphChannel{"TD", kappa_percent<3>, kRearRightColor},
};
constexpr std::array kDrivetrainPanels = {
    GraphPanel{"motor", "", -1.0f, 9.0f, kEngineChannels},
    GraphPanel{"turbo", "bar", 0.0f, 2.0f, kBoostChannels},
    GraphPanel{"deslizamiento", "%", -50.0f, 50.0f, kKappaChannels},
};

constexpr std::array kPages = {
    GraphPage{"manejo", kHandlingPanels},
    GraphPage{"tren motriz", kDrivetrainPanels},
    GraphPage{"suspensión", kSuspensionPanels},
};

constexpr int count_channels() {
    int n = 0;
    for (const GraphPage& page : kPages) {
        for (const GraphPanel& panel : page.panels) {
            n += static_cast<int>(panel.channels.size());
        }
    }
    return n;
}
constexpr int kChannelCount = count_channels();
static_assert(kChannelCount <= kMaxGraphChannels, "demasiados canales en las gráficas");

constexpr std::array<TelemetryHistory::Channel, kChannelCount> kChannels = [] {
    std::array<TelemetryHistory::Channel, kChannelCount> channels{};
    std::size_t n = 0;
    for (const GraphPage& page : kPages) {
        for (const GraphPanel& panel : page.panels) {
            for (const GraphChannel& channel : panel.channels) {
                channels[n++] = channel.value;
            }
        }
    }
    return channels;
}();

} // namespace

TelemetryHistory::TelemetryHistory()
    : TelemetryHistory(default_graph_channels(), kGraphColumns, kGraphStepsPerColumn) {}

TelemetryHistory::TelemetryHistory(std::span<const Channel> channels, int columns,
                                   int steps_per_column)
    : channels_(channels.begin(), channels.end()), columns_(std::max(columns, 1)),
      steps_per_column_(std::max(steps_per_column, 1)),
      ranges_(channels.size() * static_cast<std::size_t>(columns_)), latest_(channels.size()) {}

void TelemetryHistory::clear() {
    std::fill(ranges_.begin(), ranges_.end(), Range{});
    std::fill(latest_.begin(), latest_.end(), 0.0f);
    steps_ = 0;
}

void TelemetryHistory::push(const sim::CarTelemetry& telemetry) {
    const std::int64_t column = steps_ / steps_per_column_;
    const bool starts_column = steps_ % steps_per_column_ == 0;
    const auto slot = static_cast<std::size_t>(column % columns_);
    for (std::size_t k = 0; k < channels_.size(); ++k) {
        const float v = channels_[k](telemetry);
        Range& r = ranges_[k * static_cast<std::size_t>(columns_) + slot];
        if (starts_column) {
            r = Range{v, v, true};
            if (steps_ > 0) {
                // El paso anterior también, para que la curva quede unida.
                r.min = std::min(r.min, latest_[k]);
                r.max = std::max(r.max, latest_[k]);
            }
        } else {
            r.min = std::min(r.min, v);
            r.max = std::max(r.max, v);
        }
        latest_[k] = v;
    }
    ++steps_;
}

float TelemetryHistory::latest(int channel) const {
    return latest_[static_cast<std::size_t>(channel)];
}

TelemetryHistory::Range TelemetryHistory::column(int channel, int c) const {
    if (steps_ == 0) {
        return Range{};
    }
    const std::int64_t newest = (steps_ - 1) / steps_per_column_;
    const std::int64_t absolute = newest - (columns_ - 1 - c);
    if (absolute < 0) {
        return Range{};
    }
    return ranges_[static_cast<std::size_t>(channel) * static_cast<std::size_t>(columns_) +
                   static_cast<std::size_t>(absolute % columns_)];
}

void TelemetryHistory::visible_columns(int channel, std::span<Range> out) const {
    const std::size_t count = std::min(out.size(), static_cast<std::size_t>(columns_));
    const std::int64_t newest = steps_ == 0 ? -1 : (steps_ - 1) / steps_per_column_;
    std::int64_t absolute = newest - (columns_ - 1); // columna absoluta de out[0]
    const Range* ranges = ranges_.data() + static_cast<std::size_t>(channel) * columns_;
    std::size_t c = 0;
    for (; c < count && absolute < 0; ++c, ++absolute) {
        out[c] = Range{};
    }
    if (c == count) {
        return;
    }
    auto slot = static_cast<int>(absolute % columns_);
    for (; c < count; ++c) {
        out[c] = ranges[slot];
        slot = slot + 1 == columns_ ? 0 : slot + 1;
    }
}

std::span<const GraphPage> default_graph_pages() { return kPages; }

std::span<const TelemetryHistory::Channel> default_graph_channels() { return kChannels; }

int first_graph_channel(int page) {
    int n = 0;
    for (int p = 0; p < page && p < static_cast<int>(kPages.size()); ++p) {
        for (const GraphPanel& panel : kPages[static_cast<std::size_t>(p)].panels) {
            n += static_cast<int>(panel.channels.size());
        }
    }
    return n;
}

float graph_beta_deg(const sim::CarTelemetry& t) {
    const float horizontal = std::sqrt(t.vx * t.vx + t.vz * t.vz);
    return horizontal < kGraphBetaMinSpeed ? 0.0f : t.beta * kRadToDeg;
}

void GraphPager::press(bool blind_mode, std::span<const GraphPage> pages) {
    if (blind_mode) {
        return;
    }
    for (int p = page_ + 1; p < static_cast<int>(pages.size()); ++p) {
        if (!pages[static_cast<std::size_t>(p)].panels.empty()) {
            page_ = p;
            return;
        }
    }
    page_ = -1;
}

void merge_columns(std::span<const TelemetryHistory::Range> in,
                   std::span<TelemetryHistory::Range> out) {
    const std::size_t n = in.size();
    const std::size_t m = out.size();
    for (std::size_t i = 0; i < m; ++i) {
        TelemetryHistory::Range merged;
        for (std::size_t c = i * n / m; c < (i + 1) * n / m; ++c) {
            const TelemetryHistory::Range& r = in[c];
            if (!r.valid) {
                continue;
            }
            if (!merged.valid) {
                merged = r;
            } else {
                merged.min = std::min(merged.min, r.min);
                merged.max = std::max(merged.max, r.max);
            }
        }
        out[i] = merged;
    }
}

} // namespace rally::platform
