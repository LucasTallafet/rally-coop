#include "sim/stage_irregularity.h"

#include "sim/pcg32.h"
#include "sim/rmath.h"
#include "sim/stage_axis.h"

#include <algorithm>

namespace rally::sim {

namespace {

constexpr double kTwoPi = 6.283185307179586;

// Uniforme en [0, 1) con los 32 bits de Pcg32.
double unit(Pcg32& rng) { return static_cast<double>(rng.next_u32()) / 4294967296.0; }

// Paso suave 3u² − 2u³ con sus dos derivadas respecto a u. Aquí hace falta el valor y las dos
// derivadas (la validación, las secciones y e(s)): las tres de stage_ground.cpp, test_zone.cpp y
// drivetrain.cpp dan solo el valor.
StageJet smooth(double u) {
    return StageJet{u * u * (3.0 - 2.0 * u), 6.0 * u * (1.0 - u), 6.0 - 12.0 * u};
}

// Las ondas de un canal: λ log-uniforme por partes iguales de [log λmin, log λmax], φ uniforme y
// a ∝ λ con Σ a = amplitud.
std::vector<StageWave> make_waves(const StageIrregularityComponent& c, std::uint64_t seed,
                                  int channel) {
    Pcg32 rng(seed, static_cast<std::uint64_t>(channel));
    std::vector<StageWave> waves;
    const double lo = rmath::log(c.wavelength_min);
    const double span = rmath::log(c.wavelength_max) - lo;
    double sum = 0.0;
    double lambdas[kStageIrregularityWaves];
    for (int i = 0; i < kStageIrregularityWaves; ++i) {
        const double lambda =
            rmath::exp(lo + span * (static_cast<double>(i) + unit(rng)) / kStageIrregularityWaves);
        lambdas[i] = lambda;
        sum += lambda;
        StageWave w;
        w.k = kTwoPi / lambda;
        w.phase = kTwoPi * unit(rng);
        waves.push_back(w);
    }
    for (int i = 0; i < kStageIrregularityWaves; ++i) {
        waves[static_cast<std::size_t>(i)].amplitude = c.amplitude * lambdas[i] / sum;
    }
    return waves;
}

// Los intervalos de [0, L] (u desde el inicio de la primitiva) donde κ0 + (κ1 − κ0)·u/L > g.
void above(double s0, double length, double k0, double k1, double g,
           std::vector<std::array<double, 2>>& out) {
    if (k0 == k1) {
        if (k0 > g) {
            out.push_back({s0, s0 + length});
        }
        return;
    }
    const double u = (g - k0) * length / (k1 - k0);
    if (k1 > k0) {
        if (k1 > g) {
            out.push_back({s0 + rmath::max(u, 0.0), s0 + length});
        }
    } else if (k0 > g) {
        out.push_back({s0, s0 + rmath::min(u, length)});
    }
}

} // namespace

StageIrregularity StageIrregularity::build(const StageIrregularitySpec& spec,
                                           const StageAxis& axis) {
    StageIrregularity out;
    if (!spec.present) {
        return out;
    }
    const StageIrregularityComponent* components[kStageChannelCount] = {
        &spec.axis, &spec.width, &spec.width, &spec.edge, &spec.edge, &spec.bank, &spec.height};
    for (int c = 0; c < kStageChannelCount; ++c) {
        out.waves_[static_cast<std::size_t>(c)] = make_waves(*components[c], spec.seed, c);
        out.wavelength_min_[static_cast<std::size_t>(c)] = components[c]->wavelength_min;
    }
    out.scale_ = spec.scale;
    // El apagado: por primitiva, donde |κ| pasa del umbral (κ es lineal en cada una).
    std::vector<std::array<double, 2>> raw;
    for (const StagePrimitive& p : axis.primitives()) {
        above(p.s0, p.length, p.curvature0, p.curvature1, kStageIrregularityGateCurvature, raw);
        above(p.s0, p.length, -p.curvature0, -p.curvature1, kStageIrregularityGateCurvature, raw);
    }
    std::sort(raw.begin(), raw.end());
    for (const std::array<double, 2>& r : raw) {
        if (!out.gate_.empty() && r[0] <= out.gate_.back()[1]) {
            out.gate_.back()[1] = rmath::max(out.gate_.back()[1], r[1]);
        } else {
            out.gate_.push_back(r);
        }
    }
    return out;
}

bool StageIrregularity::has_waves() const {
    for (const std::vector<StageWave>& w : waves_) {
        if (!w.empty()) {
            return true;
        }
    }
    return false;
}

StageJet StageIrregularity::scale_jet(double s) const {
    if (scale_.empty()) {
        return StageJet{1.0, 0.0, 0.0};
    }
    if (!(s > scale_.front().s)) {
        return StageJet{scale_.front().value, 0.0, 0.0};
    }
    if (s >= scale_.back().s) {
        return StageJet{scale_.back().value, 0.0, 0.0};
    }
    const auto it = std::upper_bound(
        scale_.begin(), scale_.end(), s,
        [](double v, const StageIrregularitySpec::ScalePoint& p) { return v < p.s; });
    const StageIrregularitySpec::ScalePoint& a = *(it - 1);
    const StageIrregularitySpec::ScalePoint& b = *it;
    const double length = b.s - a.s;
    const double delta = b.value - a.value;
    const StageJet u = smooth((s - a.s) / length);
    return StageJet{a.value + delta * u.v, delta * u.d1 / length, delta * u.d2 / (length * length)};
}

StageJet StageIrregularity::gate_jet(double s) const {
    if (gate_.empty()) {
        return StageJet{1.0, 0.0, 0.0};
    }
    // El primer intervalo que empieza después de s y el anterior.
    const auto it =
        std::upper_bound(gate_.begin(), gate_.end(), s,
                         [](double v, const std::array<double, 2>& r) { return v < r[0]; });
    double dist = kStageIrregularityGateLength;
    double sign = 0.0;
    if (it != gate_.begin()) {
        const std::array<double, 2>& before = *(it - 1);
        if (s <= before[1]) {
            return StageJet{0.0, 0.0, 0.0};
        }
        dist = s - before[1];
        sign = 1.0;
    }
    if (it != gate_.end() && it->front() - s < dist) {
        dist = it->front() - s;
        sign = -1.0;
    }
    if (dist >= kStageIrregularityGateLength) {
        return StageJet{1.0, 0.0, 0.0};
    }
    constexpr double kLength = kStageIrregularityGateLength;
    const StageJet u = smooth(dist / kLength);
    return StageJet{u.v, sign * u.d1 / kLength, u.d2 / (kLength * kLength)};
}

std::array<double, kStageChannelCount> StageIrregularity::values(double s, int count) const {
    std::array<double, kStageChannelCount> out{};
    const double scale = scale_jet(s).v;
    const double gated = scale * gate_jet(s).v;
    for (int c = 0; c < count && c < kStageChannelCount; ++c) {
        double sum = 0.0;
        for (const StageWave& w : waves_[static_cast<std::size_t>(c)]) {
            sum += w.amplitude * rmath::sin(w.k * s + w.phase);
        }
        out[static_cast<std::size_t>(c)] =
            (stage_channel_gated(static_cast<StageChannel>(c)) ? gated : scale) * sum;
    }
    return out;
}

StageJet StageIrregularity::jet(StageChannel channel, double s) const {
    StageJet w;
    for (const StageWave& wave : waves_[static_cast<std::size_t>(channel)]) {
        const double x = wave.k * s + wave.phase;
        const double sn = rmath::sin(x);
        const double cs = rmath::cos(x);
        w.v += wave.amplitude * sn;
        w.d1 += wave.amplitude * wave.k * cs;
        w.d2 -= wave.amplitude * wave.k * wave.k * sn;
    }
    const StageJet a = scale_jet(s);
    const StageJet b = stage_channel_gated(channel) ? gate_jet(s) : StageJet{1.0, 0.0, 0.0};
    // F = σ·G y el canal F·W, con sus derivadas.
    const StageJet f{a.v * b.v, a.d1 * b.v + a.v * b.d1,
                     a.d2 * b.v + 2.0 * a.d1 * b.d1 + a.v * b.d2};
    return StageJet{f.v * w.v, f.d1 * w.v + f.v * w.d1,
                    f.d2 * w.v + 2.0 * f.d1 * w.d1 + f.v * w.d2};
}

StageChannelBound StageIrregularity::scale_bound(double s0, double s1) const {
    if (scale_.empty()) {
        return StageChannelBound{1.0, 0.0, 0.0};
    }
    // Entre dos puntos, σ es monótona: los extremos están en los puntos o en s0 y s1.
    StageChannelBound out;
    out.v = rmath::max(rmath::abs(scale_jet(s0).v), rmath::abs(scale_jet(s1).v));
    for (std::size_t i = 0; i < scale_.size(); ++i) {
        if (scale_[i].s > s0 && scale_[i].s < s1) {
            out.v = rmath::max(out.v, rmath::abs(scale_[i].value));
        }
        if (i + 1 < scale_.size() && scale_[i + 1].s > s0 && scale_[i].s < s1) {
            const double length = scale_[i + 1].s - scale_[i].s;
            const double delta = rmath::abs(scale_[i + 1].value - scale_[i].value);
            out.d1 = rmath::max(out.d1, 1.5 * delta / length);
            out.d2 = rmath::max(out.d2, 6.0 * delta / (length * length));
        }
    }
    return out;
}

StageChannelBound StageIrregularity::gate_bound(double s0, double s1) const {
    constexpr double kLength = kStageIrregularityGateLength;
    for (const std::array<double, 2>& r : gate_) {
        if (r[1] > s0 - kLength && r[0] < s1 + kLength) {
            return StageChannelBound{1.0, 1.5 / kLength, 6.0 / (kLength * kLength)};
        }
    }
    return StageChannelBound{1.0, 0.0, 0.0};
}

StageChannelBound StageIrregularity::bound(StageChannel channel, double s0, double s1) const {
    const std::vector<StageWave>& waves = waves_[static_cast<std::size_t>(channel)];
    if (waves.empty()) {
        return StageChannelBound{};
    }
    StageChannelBound w;
    for (const StageWave& wave : waves) {
        const double a = rmath::abs(wave.amplitude);
        w.v += a;
        w.d1 += a * wave.k;
        w.d2 += a * wave.k * wave.k;
    }
    const StageChannelBound a = scale_bound(s0, s1);
    const StageChannelBound b =
        stage_channel_gated(channel) ? gate_bound(s0, s1) : StageChannelBound{1.0, 0.0, 0.0};
    const StageChannelBound f{a.v * b.v, a.d1 * b.v + a.v * b.d1,
                              a.d2 * b.v + 2.0 * a.d1 * b.d1 + a.v * b.d2};
    return StageChannelBound{f.v * w.v, f.d1 * w.v + f.v * w.d1,
                             f.d2 * w.v + 2.0 * f.d1 * w.d1 + f.v * w.d2};
}

} // namespace rally::sim
