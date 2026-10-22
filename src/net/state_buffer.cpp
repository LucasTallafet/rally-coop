#include "net/state_buffer.h"

#include "net/protocol.h"
#include "sim/rmath.h"

#include <algorithm>
#include <limits>

namespace rally::net {

namespace {

constexpr double kDelaySteps = kInterpolationDelay * kStepRate; // 24
constexpr double kOffsetWindow = 2.0;                           // s
constexpr double kRecentWindow = 0.5;                           // s

Pose pose_of(const CarState& s) { return Pose{s.x, s.y, s.z, s.qx, s.qy, s.qz, s.qw}; }

// El estado seguido con su velocidad durante dt_steps pasos, como mucho kMaxExtrapolation; la
// rotación se queda (el estado no lleva velocidad angular).
Pose extrapolate(const CarState& s, double dt_steps) {
    const double dt = rmath::min(dt_steps / kStepRate, kMaxExtrapolation);
    Pose p = pose_of(s);
    p.x += static_cast<double>(s.vx) * dt;
    p.y += static_cast<double>(s.vy) * dt;
    p.z += static_cast<double>(s.vz) * dt;
    return p;
}

Pose blend(const CarState& a, const CarState& b, double t) {
    Pose p;
    p.x = a.x + (b.x - a.x) * t;
    p.y = a.y + (b.y - a.y) * t;
    p.z = a.z + (b.z - a.z) * t;
    // nlerp por el camino corto (q y -q son la misma rotación).
    const double dot = a.qx * b.qx + a.qy * b.qy + a.qz * b.qz + a.qw * b.qw;
    const double s = dot < 0.0 ? -1.0 : 1.0;
    double qx = a.qx + (s * b.qx - a.qx) * t;
    double qy = a.qy + (s * b.qy - a.qy) * t;
    double qz = a.qz + (s * b.qz - a.qz) * t;
    double qw = a.qw + (s * b.qw - a.qw) * t;
    const double norm = rmath::sqrt(qx * qx + qy * qy + qz * qz + qw * qw);
    if (norm > 0.0) {
        qx /= norm;
        qy /= norm;
        qz /= norm;
        qw /= norm;
    } else {
        qx = qy = qz = 0.0;
        qw = 1.0;
    }
    p.qx = qx;
    p.qy = qy;
    p.qz = qz;
    p.qw = qw;
    return p;
}

} // namespace

void StateBuffer::reset(std::uint8_t generation) {
    count_ = 0;
    active_ = true;
    generation_ = generation;
    received_ = 0;
    missing_ = 0;
    late_ = 0;
    max_index_ = -1;
    max_step_ = 0;
}

void StateBuffer::push(const CarState& state, double arrival_s) {
    if (!active_ || state.generation != generation_) {
        return;
    }
    // Posición ordenada por paso, mirando desde el final (lo normal es añadir al final).
    int pos = count_;
    while (pos > 0 && ring_[static_cast<std::size_t>(pos - 1)].state.step > state.step) {
        --pos;
    }
    if (pos > 0 && ring_[static_cast<std::size_t>(pos - 1)].state.step == state.step) {
        return; // duplicado
    }
    if (count_ == kCapacity) {
        if (pos == 0) {
            return; // anterior al más viejo
        }
        std::copy(ring_.begin() + 1, ring_.begin() + count_, ring_.begin());
        --count_;
        --pos;
    }
    std::copy_backward(ring_.begin() + pos, ring_.begin() + count_, ring_.begin() + count_ + 1);
    ring_[static_cast<std::size_t>(pos)] = Entry{state, arrival_s};
    ++count_;

    ++received_;
    const std::int64_t index = state.step / static_cast<std::uint32_t>(kStateEverySteps);
    if (max_index_ < 0 || state.step > max_step_) {
        if (max_index_ >= 0 && index > max_index_) {
            missing_ += static_cast<std::uint64_t>(index - max_index_ - 1);
        }
        max_index_ = rmath::max(max_index_, index);
        max_step_ = state.step;
    } else {
        ++late_;
        if (missing_ > 0) {
            --missing_; // llegó uno que se daba por perdido
        }
    }
}

double StateBuffer::host_step_estimate(double now_s) const {
    // offset = mínimo de (llegada·240 − paso) de los últimos 2 s: el paquete más rápido da la
    // diferencia de reloj con el menor retardo. Si el host se paró y siguió (pausa, ventana
    // arrastrada), el mínimo viejo se queda atrás: si el de los últimos 0,5 s lo supera en más
    // de 24 pasos, se usa ese.
    constexpr double kInf = std::numeric_limits<double>::infinity();
    double min_window = kInf;
    double min_recent = kInf;
    double min_all = kInf;
    for (int i = 0; i < count_; ++i) {
        const Entry& e = ring_[static_cast<std::size_t>(i)];
        const double v = e.arrival * kStepRate - static_cast<double>(e.state.step);
        min_all = rmath::min(min_all, v);
        if (e.arrival >= now_s - kOffsetWindow) {
            min_window = rmath::min(min_window, v);
        }
        if (e.arrival >= now_s - kRecentWindow) {
            min_recent = rmath::min(min_recent, v);
        }
    }
    double offset = min_all;
    if (min_window < kInf) {
        offset = min_window;
        if (min_recent < kInf && min_recent - min_window > kDelaySteps) {
            offset = min_recent;
        }
    }
    return now_s * kStepRate - offset;
}

double StateBuffer::target_step(double now_s) const {
    return host_step_estimate(now_s) - kDelaySteps;
}

bool StateBuffer::sample(double now_s, Pose& out) const {
    if (count_ == 0) {
        return false;
    }
    const double target = target_step(now_s);
    const CarState& first = ring_[0].state;
    const CarState& last = ring_[static_cast<std::size_t>(count_ - 1)].state;
    if (target <= static_cast<double>(first.step)) {
        out = pose_of(first);
        return true;
    }
    if (target >= static_cast<double>(last.step)) {
        out = extrapolate(last, target - static_cast<double>(last.step));
        return true;
    }
    int i = count_ - 2; // hay un posterior: i + 1 existe
    while (i > 0 && static_cast<double>(ring_[static_cast<std::size_t>(i)].state.step) > target) {
        --i;
    }
    const CarState& a = ring_[static_cast<std::size_t>(i)].state;
    const CarState& b = ring_[static_cast<std::size_t>(i + 1)].state;
    if (b.reset) {
        // Recolocado: sin mezclar. Se sigue a `a` hasta el paso de `b`, y entonces salta.
        out = extrapolate(a, target - static_cast<double>(a.step));
        return true;
    }
    const double t = (target - static_cast<double>(a.step)) / static_cast<double>(b.step - a.step);
    out = blend(a, b, rmath::clamp(t, 0.0, 1.0));
    return true;
}

BufferStats StateBuffer::stats(double now_s) const {
    BufferStats s;
    s.received = received_;
    s.missing = missing_;
    s.late = late_;
    if (count_ > 0) {
        const double est = host_step_estimate(now_s);
        const double newest =
            static_cast<double>(ring_[static_cast<std::size_t>(count_ - 1)].state.step);
        s.delay_s = (est - newest) / kStepRate;
        s.extrapolating = est - kDelaySteps > newest;
        s.host_step = est > 0.0 ? static_cast<std::uint32_t>(est) : 0;
    }
    return s;
}

std::uint32_t StateBuffer::newest_step() const {
    return count_ > 0 ? ring_[static_cast<std::size_t>(count_ - 1)].state.step : 0;
}

void LinkSimulator::set(const Impairment& impairment, std::uint64_t seed) {
    impairment_ = impairment;
    rng_.seed(seed, 54);
}

void LinkSimulator::clear() {
    for (Slot& slot : slots_) {
        slot.used = false;
    }
}

void LinkSimulator::submit(const CarState& state, double now_s) {
    if (impairment_.loss > 0.0 && static_cast<double>(rng_.next_float01()) < impairment_.loss) {
        ++dropped_;
        return;
    }
    double release = now_s + impairment_.delay_s;
    if (impairment_.jitter_s > 0.0) {
        release += (2.0 * static_cast<double>(rng_.next_float01()) - 1.0) * impairment_.jitter_s;
    }
    release = rmath::max(release, now_s);
    for (Slot& slot : slots_) {
        if (!slot.used) {
            slot.used = true;
            slot.release = release;
            slot.state = state;
            return;
        }
    }
    ++dropped_; // anillo lleno
}

bool LinkSimulator::pop_ready(double now_s, CarState& out, double& release_s) {
    Slot* best = nullptr;
    for (Slot& slot : slots_) {
        if (slot.used && slot.release <= now_s &&
            (best == nullptr || slot.release < best->release)) {
            best = &slot;
        }
    }
    if (best == nullptr) {
        return false;
    }
    out = best->state;
    release_s = best->release;
    best->used = false;
    return true;
}

} // namespace rally::net
