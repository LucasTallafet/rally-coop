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

// Nombres sin β: la fuente por defecto de raylib no lo tiene.
constexpr std::array kBetaChannels = {GraphChannel{"beta", graph_beta_deg, 0xffa040ffu}};
constexpr std::array kYawChannels = {GraphChannel{"guiñada", yaw_rate_deg, 0x60c0ffffu}};
constexpr std::array kSpeedChannels = {GraphChannel{"velocidad", speed_kmh, 0xf0f0f0ffu}};
constexpr std::array kInputChannels = {
    // Nombres cortos: las cuatro etiquetas con su valor caben en una línea del panel.
    GraphChannel{"dir", steer, 0x60c0ffffu},
    GraphChannel{"acel", throttle, 0x60e060ffu},
    GraphChannel{"freno", brake, 0xff5050ffu},
    GraphChannel{"f. mano", handbrake, 0xffd040ffu},
};

constexpr std::array kPanels = {
    GraphPanel{"beta", "°", -90.0f, 90.0f, kBetaChannels},
    GraphPanel{"guiñada", "°/s", -150.0f, 150.0f, kYawChannels},
    GraphPanel{"velocidad", "km/h", 0.0f, 200.0f, kSpeedChannels},
    GraphPanel{"entradas", "", -1.0f, 1.0f, kInputChannels}, // aplicadas (ControlState)
};

} // namespace

TelemetryHistory::TelemetryHistory(int capacity)
    : samples_(static_cast<std::size_t>(std::max(capacity, 1))) {}

void TelemetryHistory::clear() {
    head_ = 0;
    size_ = 0;
}

void TelemetryHistory::push(const sim::CarTelemetry& telemetry) {
    samples_[static_cast<std::size_t>(head_)] = telemetry;
    head_ = (head_ + 1) % capacity();
    size_ = std::min(size_ + 1, capacity());
}

const sim::CarTelemetry& TelemetryHistory::at(int i) const {
    const int oldest = (head_ - size_ + capacity()) % capacity();
    return samples_[static_cast<std::size_t>((oldest + i) % capacity())];
}

void TelemetryHistory::column_ranges(std::span<const Channel> channels, int columns,
                                     std::span<Range> out) const {
    const int n = static_cast<int>(channels.size());
    const int cap = capacity();
    const int oldest = (head_ - size_ + cap) % cap;
    for (int c = 0; c < columns; ++c) {
        Range* column = out.data() + static_cast<std::size_t>(c) * static_cast<std::size_t>(n);
        // Pasos con edad en [(columns-1-c)·cap/columns, (columns-c)·cap/columns).
        const int newest_age = (columns - 1 - c) * cap / columns;
        const int oldest_age = (columns - c) * cap / columns - 1;
        const int last = size_ - 1 - newest_age;
        if (last < 0) {
            for (int k = 0; k < n; ++k) {
                column[k] = Range{};
            }
            continue;
        }
        const int first = std::max(0, size_ - 1 - oldest_age - 1);
        // Índice en el búfer sin módulo por paso: da la vuelta como mucho una vez.
        int index = (oldest + first) % cap;
        for (int i = first; i <= last; ++i) {
            const sim::CarTelemetry& sample = samples_[static_cast<std::size_t>(index)];
            for (int k = 0; k < n; ++k) {
                const float v = channels[static_cast<std::size_t>(k)](sample);
                Range& r = column[k];
                if (i == first) {
                    r = Range{v, v, true};
                } else {
                    r.min = std::min(r.min, v);
                    r.max = std::max(r.max, v);
                }
            }
            index = index + 1 == cap ? 0 : index + 1;
        }
    }
}

std::span<const GraphPanel> default_graph_panels() { return kPanels; }

float graph_beta_deg(const sim::CarTelemetry& t) {
    const float horizontal = std::sqrt(t.vx * t.vx + t.vz * t.vz);
    return horizontal < kGraphBetaMinSpeed ? 0.0f : t.beta * kRadToDeg;
}

} // namespace rally::platform
