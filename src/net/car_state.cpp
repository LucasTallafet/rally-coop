#include "net/car_state.h"

#include "sim/check.h"
#include "sim/rmath.h"

namespace rally::net {

namespace {

constexpr double kSqrt2 = 1.4142135623730951;
constexpr double kQuatScale = 32767.0 * kSqrt2;
constexpr double kRpmPerRadS = 9.549296585513721; // 60 / 2π
constexpr std::uint8_t kAirSurface = 255;

class Writer {
public:
    explicit Writer(std::uint8_t* out) : out_(out) {}
    void u8(std::uint8_t v) { out_[n_++] = v; }
    void u16(std::uint16_t v) {
        u8(static_cast<std::uint8_t>(v & 0xFF));
        u8(static_cast<std::uint8_t>(v >> 8));
    }
    void i16(std::int16_t v) { u16(static_cast<std::uint16_t>(v)); }
    void u32(std::uint32_t v) {
        u16(static_cast<std::uint16_t>(v & 0xFFFF));
        u16(static_cast<std::uint16_t>(v >> 16));
    }
    void i32(std::int32_t v) { u32(static_cast<std::uint32_t>(v)); }
    std::size_t size() const { return n_; }

private:
    std::uint8_t* out_;
    std::size_t n_ = 0;
};

class Reader {
public:
    explicit Reader(const std::uint8_t* in) : in_(in) {}
    std::uint8_t u8() { return in_[n_++]; }
    std::uint16_t u16() {
        const std::uint16_t lo = u8();
        const std::uint16_t hi = u8();
        return static_cast<std::uint16_t>(lo | (hi << 8));
    }
    std::int16_t i16() { return static_cast<std::int16_t>(u16()); }
    std::uint32_t u32() {
        const std::uint32_t lo = u16();
        const std::uint32_t hi = u16();
        return lo | (hi << 16);
    }
    std::int32_t i32() { return static_cast<std::int32_t>(u32()); }

private:
    const std::uint8_t* in_;
    std::size_t n_ = 0;
};

// Redondeo al entero más cercano (la mitad hacia arriba) con saturación.
std::int64_t round_clamped(double v, double lo, double hi) {
    const double r = rmath::floor(v + 0.5);
    return static_cast<std::int64_t>(rmath::clamp(r, lo, hi));
}

std::int32_t to_mm(double meters) {
    return static_cast<std::int32_t>(round_clamped(meters * 1000.0, -2147483648.0, 2147483647.0));
}

std::int16_t to_cm_s(float v) {
    return static_cast<std::int16_t>(
        round_clamped(static_cast<double>(v) * 100.0, -32767.0, 32767.0));
}

} // namespace

CarState car_state_from(const sim::SimWorld& world, std::uint32_t step, std::uint8_t generation,
                        bool reset) {
    RALLY_CHECK(world.has_car(), "net: car_state_from sin coche");
    RALLY_CHECK(world.telemetry_enabled(), "net: car_state_from necesita la telemetría encendida");
    const sim::BodyPose pose = world.car_pose();
    const sim::BodyVelocity velocity = world.car_velocity();
    const sim::CarTelemetry& tel = world.telemetry();

    CarState s;
    s.generation = generation;
    s.step = step;
    s.x = pose.x;
    s.y = pose.y;
    s.z = pose.z;
    s.qx = pose.qx;
    s.qy = pose.qy;
    s.qz = pose.qz;
    s.qw = pose.qw;
    s.vx = velocity.vx;
    s.vy = velocity.vy;
    s.vz = velocity.vz;
    for (std::size_t i = 0; i < 4; ++i) {
        s.compression[i] = tel.wheels[i].compression;
        s.surface[i] = tel.wheels[i].surface;
    }
    s.gear = tel.gear;
    s.rpm = tel.engine_omega * static_cast<float>(kRpmPerRadS);
    s.reset = reset;
    return s;
}

void encode(const CarState& state, std::span<std::uint8_t, kCarStateBytes> out) {
    Writer w(out.data());
    w.u8(kMsgCarState);
    w.u8(state.generation);
    w.u32(state.step);
    w.i32(to_mm(state.x));
    w.i32(to_mm(state.y));
    w.i32(to_mm(state.z));

    // Las tres componentes más pequeñas; la mayor se omite y se hace positiva (q y -q son la
    // misma rotación).
    double q[4] = {state.qx, state.qy, state.qz, state.qw};
    const double norm = rmath::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    if (norm > 0.0) {
        for (double& c : q) {
            c /= norm;
        }
    } else {
        q[0] = q[1] = q[2] = 0.0;
        q[3] = 1.0;
    }
    int largest = 0;
    for (int i = 1; i < 4; ++i) {
        if (rmath::abs(q[i]) > rmath::abs(q[largest])) {
            largest = i;
        }
    }
    const double sign = q[largest] < 0.0 ? -1.0 : 1.0;
    w.u8(static_cast<std::uint8_t>(largest));
    for (int i = 0; i < 4; ++i) {
        if (i != largest) {
            w.i16(static_cast<std::int16_t>(
                round_clamped(sign * q[i] * kQuatScale, -32767.0, 32767.0)));
        }
    }

    w.i16(to_cm_s(state.vx));
    w.i16(to_cm_s(state.vy));
    w.i16(to_cm_s(state.vz));
    for (float c : state.compression) {
        w.u16(static_cast<std::uint16_t>(
            round_clamped(static_cast<double>(c) * 10000.0, 0.0, 65535.0)));
    }
    for (std::int8_t s : state.surface) {
        w.u8(s < 0 ? kAirSurface : static_cast<std::uint8_t>(s));
    }
    w.u8(static_cast<std::uint8_t>(state.gear));
    w.u16(static_cast<std::uint16_t>(round_clamped(static_cast<double>(state.rpm), 0.0, 65535.0)));
    w.u8(state.reset ? 1 : 0);
    RALLY_CHECK(w.size() == kCarStateBytes, "net: el estado del coche no mide 47 B");
}

bool decode(std::span<const std::uint8_t> in, CarState& out) {
    if (in.size() != kCarStateBytes || in[0] != kMsgCarState) {
        return false;
    }
    Reader r(in.data());
    r.u8(); // tipo
    CarState s;
    s.generation = r.u8();
    s.step = r.u32();
    s.x = r.i32() / 1000.0;
    s.y = r.i32() / 1000.0;
    s.z = r.i32() / 1000.0;

    const int omitted = r.u8();
    if (omitted > 3) {
        return false;
    }
    double q[4] = {0, 0, 0, 0};
    double sum = 0.0;
    for (int i = 0; i < 4; ++i) {
        if (i != omitted) {
            q[i] = r.i16() / kQuatScale;
            sum += q[i] * q[i];
        }
    }
    q[omitted] = rmath::sqrt(rmath::max(0.0, 1.0 - sum));
    s.qx = q[0];
    s.qy = q[1];
    s.qz = q[2];
    s.qw = q[3];

    s.vx = static_cast<float>(r.i16() / 100.0);
    s.vy = static_cast<float>(r.i16() / 100.0);
    s.vz = static_cast<float>(r.i16() / 100.0);
    for (float& c : s.compression) {
        c = static_cast<float>(r.u16() / 10000.0);
    }
    for (std::int8_t& surface : s.surface) {
        const std::uint8_t v = r.u8();
        if (v == kAirSurface) {
            surface = -1;
        } else if (v < sim::kSurfaceCount) {
            surface = static_cast<std::int8_t>(v);
        } else {
            return false;
        }
    }
    s.gear = static_cast<std::int8_t>(r.u8());
    s.rpm = static_cast<float>(r.u16());
    s.reset = (r.u8() & 1) != 0;
    out = s;
    return true;
}

} // namespace rally::net
