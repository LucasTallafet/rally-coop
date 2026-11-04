#pragma once

// Tramos de test y conductor de test del tramo (tarea tramo-fisica, lote 4): los tramos de
// tests/sim/stages, el tramo sintético de 30 km de la medida de la parte A y un conductor que
// sigue el eje con la dirección por el error de rumbo y de t y el acelerador para una velocidad
// objetivo. Lo usan test_stage_ground.cpp, test_stage_world.cpp y stage_measure.cpp.

#include "io/recording.h"
#include "sim/check.h"
#include "sim/pcg32.h"
#include "sim/sim_input.h"
#include "sim/sim_world.h"
#include "sim/stage_track.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <string>

namespace rally::tests {

// Texto de tests/sim/stages/<name>.
inline std::string stage_file(const char* name) {
    std::string bytes;
    std::string error;
    const std::string path = std::string(RALLY_SOURCE_DIR "/tests/sim/stages/") + name;
    RALLY_CHECK(rally::io::read_file(path, bytes, error),
                "tests: no se puede leer un tramo de test");
    return bytes;
}

inline const std::string& lazadas_stage_text() {
    static const std::string text = stage_file("lazadas.stage");
    return text;
}

inline std::string stage_number(double value) {
    std::array<char, 64> buffer{};
    const std::to_chars_result r =
        std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
    return std::string(buffer.data(), r.ptr);
}

// Tramo sintético de unos 30 km (parte A): rumbo medio al este (fuera de las lazadas, a menos
// de 70° del este: el tramo no vuelve sobre sí mismo), curvas clotoide–arco–clotoide de R 15–300
// m con peralte del 3–6 %, una pareja de horquillas (R 8–10, recta de 60–80 m) cada ~2 km tras
// una recta de 100 m, PIV cada 150–400 m con pendientes de hasta el 8 % y curvas verticales de
// 40–80 m, semianchos de 2,6–3,5 m y la superficie cambiando cada km. Semilla fija.
inline std::string synthetic_stage_30km() {
    constexpr double kPi = 3.14159265358979323846;
    constexpr double kEast = 0.5 * kPi;
    constexpr double kMaxSide = 70.0 * kPi / 180.0;
    constexpr double kTarget = 30000.0;
    sim::Pcg32 rng(0x7a3f'2026'1027ull, 4);
    const auto uniform = [&rng](double lo, double hi) {
        return lo + (hi - lo) * (static_cast<double>(rng.next_u32()) / 4294967296.0);
    };
    std::string plan;
    std::string bank = "bank 0 0\n";
    double s = 0.0;
    double heading = kEast;
    double last_bank_s = 0.0;
    double next_lazada = 1500.0;
    const auto straight = [&](double length) {
        plan += "straight " + stage_number(length) + "\n";
        s += length;
    };
    // Curva de giro `turn` (rad, + a la derecha) con radio r y clotoides de `spiral` m; peralte
    // de `percent` hacia el interior, en rampa por las clotoides.
    const auto curve = [&](double turn, double r, double spiral, double percent) {
        const double sign = turn > 0.0 ? 1.0 : -1.0;
        const double signed_r = sign * r;
        const double arc = r * (std::abs(turn) - spiral / r);
        const double value = -sign * percent;
        if (s > last_bank_s) {
            bank += "bank " + stage_number(s) + " 0\n";
        }
        plan += "spiral " + stage_number(spiral) + " inf " + stage_number(signed_r) + "\n";
        plan += "arc " + stage_number(arc) + " " + stage_number(signed_r) + "\n";
        plan += "spiral " + stage_number(spiral) + " " + stage_number(signed_r) + " inf\n";
        bank += "bank " + stage_number(s + spiral) + " " + stage_number(value) + "\n";
        bank += "bank " + stage_number(s + spiral + arc) + " " + stage_number(value) + "\n";
        bank += "bank " + stage_number(s + 2.0 * spiral + arc) + " 0\n";
        last_bank_s = s + 2.0 * spiral + arc;
        s = last_bank_s;
        heading += turn;
    };
    while (s < kTarget) {
        if (s >= next_lazada) {
            // Recta de aproximación, horquilla, recta de vuelta y horquilla al otro lado.
            straight(100.0);
            const double first = rng.bounded(2) == 0 ? 1.0 : -1.0;
            const double r = uniform(8.0, 10.0);
            curve(first * kPi, r, 10.0, 6.0);
            straight(uniform(60.0, 80.0));
            curve(-first * kPi, r, 10.0, 6.0);
            straight(100.0);
            next_lazada = s + uniform(1800.0, 2200.0);
            continue;
        }
        straight(uniform(30.0, 250.0));
        const double target = kEast + uniform(-kMaxSide, kMaxSide);
        const double turn = target - heading;
        if (std::abs(turn) < 10.0 * kPi / 180.0) {
            continue;
        }
        // Radio repartido en logaritmo entre 15 y 300 m.
        const double r = 15.0 * std::exp(uniform(0.0, std::log(20.0)));
        const double spiral = std::min(40.0, 0.4 * std::abs(turn) * r);
        curve(turn, r, spiral, uniform(3.0, 6.0));
    }
    straight(150.0);
    const double length = s;

    // Alzado: PIV cada 150–400 m, pendiente de hasta el 8 %, curvas de 40–80 m.
    std::string profile = "pvi 0 0 0\n";
    double ps = 0.0;
    double height = 0.0;
    double grade = 0.0;
    for (;;) {
        const double step = uniform(150.0, 400.0);
        if (ps + step > length - 150.0) {
            break;
        }
        height += grade * step;
        ps += step;
        profile += "pvi " + stage_number(ps) + " " + stage_number(height) + " " +
                   stage_number(uniform(40.0, 80.0)) + "\n";
        grade = uniform(-0.08, 0.08);
    }
    height += grade * (length - ps);
    profile += "pvi " + stage_number(length) + " " + stage_number(height) + " 0\n";

    // Semianchos: cambian cada 1–3 km en 50 m; superficie por km.
    std::string width = "width 0 3 3\n";
    for (double ws = uniform(1000.0, 3000.0); ws + 50.0 < length; ws += uniform(1000.0, 3000.0)) {
        const double left = uniform(2.6, 3.5);
        const double right = uniform(2.6, 3.5);
        width += "width " + stage_number(ws) + " " + stage_number(left) + " " +
                 stage_number(right) + "\n";
    }
    std::string surface = "surface 0 gravel\n";
    static constexpr std::array<const char*, 3> kSurfaces = {"dirt", "asphalt", "gravel"};
    int k = 0;
    for (double ss = 1000.0; ss < length; ss += 1000.0) {
        surface +=
            "surface " + stage_number(ss) + " " + kSurfaces[static_cast<std::size_t>(k)] + "\n";
        k = (k + 1) % 3;
    }
    return "format = 1\nname = Sintético 30 km (test)\nstart_x = -12000\nstart_z = 0\n"
           "start_heading_deg = 90\nstart_height = 0\n[plan]\n" +
           plan + "[profile]\n" + profile + "[bank]\n" + bank + "[width]\n" + width +
           "[surface]\n" + surface;
}

inline double wrap_angle(double a) {
    constexpr double kPi = 3.14159265358979323846;
    while (a > kPi) {
        a -= 2.0 * kPi;
    }
    while (a < -kPi) {
        a += 2.0 * kPi;
    }
    return a;
}

// Rumbo del coche (0 hacia −z, + a la derecha) desde su orientación.
inline double car_heading(const sim::BodyPose& pose) {
    // Adelante (0, 0, −1) girado por el cuaternión.
    const double x = pose.qx;
    const double y = pose.qy;
    const double z = pose.qz;
    const double w = pose.qw;
    const double fx = -(2.0 * (x * z + w * y));
    const double fz = -(1.0 - 2.0 * (x * x + y * y));
    return std::atan2(fx, -fz);
}

// Conductor de test del tramo: dirección por el error de rumbo frente al eje unos metros por
// delante y por t; acelerador y freno para `speed` m/s, limitada a √(a·R) + frenada en lo que
// viene (a = `lateral` m/s², 4 m/s² de frenada).
class StageDriver {
public:
    StageDriver(const sim::StageTrack& track, double speed, double lateral = 3.0)
        : track_(track), speed_(speed), lateral_(lateral) {}

    double s() const { return s_; }
    double t() const { return t_; }
    bool on_road() const { return on_road_; }
    // Velocidad objetivo; 0 la para.
    void set_speed(double speed) { speed_ = speed; }

    sim::SimInput input(const sim::BodyPose& pose, const sim::BodyVelocity& v) {
        const sim::StageProjection p = started_
                                           ? track_.project(pose.x, pose.z, s_ - 20.0, s_ + 40.0)
                                           : track_.acquire(pose.x, pose.z);
        started_ = true;
        s_ = p.s;
        t_ = p.t;
        on_road_ = p.on_road;
        const double speed = std::hypot(v.vx, v.vz);
        const double ahead = 1.5 + 0.3 * speed;
        const double want = track_.pose(s_ + ahead).heading;
        const double error = wrap_angle(want - car_heading(pose));
        double target = speed_;
        for (double k = 0.0; k <= 100.0; k += 2.0) {
            const double curvature = std::abs(track_.pose(s_ + k).curvature);
            const double r = curvature > 1e-6 ? 1.0 / curvature : 1e6;
            target = std::min(target, std::sqrt(lateral_ * r + 2.0 * 4.0 * k));
        }
        sim::SimInput in;
        in.steer = static_cast<std::int16_t>(std::clamp(2.5 * error - 0.4 * t_, -1.0, 1.0) *
                                             sim::kAxisMax);
        in.throttle =
            static_cast<std::int16_t>(std::clamp(0.6 * (target - speed), 0.0, 1.0) * sim::kAxisMax);
        in.brake =
            static_cast<std::int16_t>(std::clamp(0.6 * (speed - target), 0.0, 1.0) * sim::kAxisMax);
        return in;
    }

private:
    const sim::StageTrack& track_;
    double speed_;
    double lateral_;
    bool started_ = false;
    bool on_road_ = true;
    double s_ = 0.0;
    double t_ = 0.0;
};

} // namespace rally::tests
