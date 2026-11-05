// Volumen seguro de cabina (lote 5, cabina-volumen): el ojo final nunca a menos de 8 cm de la
// puerta, el techo, el final del techo ni el parabrisas; dentro, no cambia; sin saltos.

#include "io/recording.h"
#include "platform/cabin_volume.h"
#include "platform/cockpit.h"
#include "platform/head_look.h"
#include "platform/head_motion.h"
#include "platform/seat.h"
#include "sim/car_data.h"
#include "sim/pcg32.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <string>

using namespace rally::platform;

namespace {

constexpr const char* kCars[] = {"mid_awd_turbo", "front_awd_turbo_5cyl", "front_awd_turbo_groupa",
                                 "front_rwd_na"};
constexpr double kSlack = 1e-6;

rally::sim::CarData load_car(const char* name) {
    std::string text;
    std::string error;
    REQUIRE(rally::io::read_file(std::string(RALLY_SOURCE_DIR) + "/data/cars/" + name + ".car",
                                 text, error));
    return rally::sim::load_car_or_abort(text, name, rally::sim::CarKeys::Full);
}

// Distancias del ojo a los cuatro planos de [cockpit] (sin el margen), medidas desde
// cockpit_point y no desde el volumen. Positivas dentro. `mirror`: copiloto (puerta reflejada).
struct Clearance {
    double door;
    double roof;
    double behind;
    double windshield;
};

Clearance clearance(const rally::sim::CarData& car, const Vec3d& eye, bool mirror) {
    const rally::sim::CockpitData& k = car.cockpit;
    Clearance c{};
    const double door_x = cockpit_point(car, 0.0, -static_cast<double>(k.door_left), 0.0).x;
    c.door = mirror ? -door_x - eye.x : eye.x - door_x;
    // Techo dibujado (cockpit.cpp): del borde superior del parabrisas al final del techo. Distancia
    // con signo, positiva por debajo.
    const Vec3d rf = cockpit_point(car, static_cast<double>(k.windshield_top_ahead), 0.0,
                                   static_cast<double>(k.windshield_top_above));
    const Vec3d rr = cockpit_point(car, -static_cast<double>(k.roof_behind), 0.0,
                                   static_cast<double>(k.roof_above));
    const double rdy = rr.y - rf.y;
    const double rdz = rr.z - rf.z;
    c.roof = (rdz * (rf.y - eye.y) + rdy * (eye.z - rf.z)) / std::sqrt(rdy * rdy + rdz * rdz);
    c.behind = cockpit_point(car, -static_cast<double>(k.roof_behind), 0.0, 0.0).z - eye.z;
    const Vec3d base = cockpit_point(car, static_cast<double>(k.windshield_base_ahead), 0.0,
                                     -static_cast<double>(k.windshield_base_below));
    const Vec3d top = cockpit_point(car, static_cast<double>(k.windshield_top_ahead), 0.0,
                                    static_cast<double>(k.windshield_top_above));
    const double dy = top.y - base.y;
    const double dz = top.z - base.z;
    // Perpendicular a la recta base-borde superior: distancia con signo, positiva por detrás/abajo
    // (el lado del ojo nominal).
    const double cross = dy * (eye.z - base.z) - dz * (eye.y - base.y);
    const double side = dy * (car_eye(car).z - base.z) - dz * (car_eye(car).y - base.y);
    c.windshield =
        std::abs(cross) / std::sqrt(dy * dy + dz * dz) * (cross * side >= 0.0 ? 1.0 : -1.0);
    return c;
}

std::uint64_t bits(double v) { return std::bit_cast<std::uint64_t>(v); }

bool same_bits(const Vec3d& a, const Vec3d& b) {
    return bits(a.x) == bits(b.x) && bits(a.y) == bits(b.y) && bits(a.z) == bits(b.z);
}

double dist(const Vec3d& a, const Vec3d& b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.y - b.y) * (a.y - b.y) +
                     (a.z - b.z) * (a.z - b.z));
}

} // namespace

TEST_CASE("cabin_volume: the final eye keeps 8 cm from every plane in every extreme") {
    const double margin = kCabinMargin - kSlack;
    for (const char* name : kCars) {
        const rally::sim::CarData car = load_car(name);
        const CabinVolume volume = cabin_volume(car, false);
        Clearance worst{1e9, 1e9, 1e9, 1e9};
        const double yaws[] = {-kLookYawMax, 0.0, kLookYawMax};
        const double pitches[] = {kLookPitchUp, 0.0, -kLookPitchDown};
        const double signs[] = {-1.0, 0.0, 1.0};
        for (int up = -15; up <= 15; up += 15) {
            for (int fwd = -15; fwd <= 15; fwd += 15) {
                for (int right = -15; right <= 15; right += 15) {
                    const Vec3d seat = driver_eye(car, SeatOffsetCm{up, fwd, right});
                    for (const double yaw : yaws) {
                        for (const double pitch : pitches) {
                            // F9 en los topes de cada eje, con los dos signos (y quieto).
                            for (const double sf : signs) {
                                for (const double sr : signs) {
                                    for (const double su : signs) {
                                        for (const double sp : {0.0}) {
                                            HeadOffset f9;
                                            f9.forward = sf * kHeadLimitForward;
                                            f9.right = sr * kHeadLimitRight;
                                            f9.up = su * kHeadLimitUp;
                                            f9.pitch = sp * kHeadLimitPitch;
                                            f9.roll = sp * kHeadLimitRoll;
                                            const HeadView view = head_view(yaw, pitch, &f9);
                                            const Vec3d eye = clamp_to_cabin(
                                                volume, {seat.x + view.eye_offset.x,
                                                         seat.y + view.eye_offset.y,
                                                         seat.z + view.eye_offset.z});
                                            const Clearance c = clearance(car, eye, false);
                                            worst.door = std::min(worst.door, c.door);
                                            worst.roof = std::min(worst.roof, c.roof);
                                            worst.behind = std::min(worst.behind, c.behind);
                                            worst.windshield =
                                                std::min(worst.windshield, c.windshield);
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
        MESSAGE(name, ": holgura minima puerta ", worst.door, " techo ", worst.roof, " final ",
                worst.behind, " parabrisas ", worst.windshield, " m");
        CHECK(worst.door >= margin);
        CHECK(worst.roof >= margin);
        CHECK(worst.behind >= margin);
        CHECK(worst.windshield >= margin);

        // El copiloto, con la mirada en los topes (sin asiento ni F9).
        const CabinVolume cvol = cabin_volume(car, true);
        const Vec3d ceye = {-car_eye(car).x, car_eye(car).y, car_eye(car).z};
        double cworst = 1e9;
        for (const double yaw : yaws) {
            for (const double pitch : pitches) {
                const HeadView view = head_view(yaw, pitch, nullptr);
                const Vec3d eye =
                    clamp_to_cabin(cvol, {ceye.x + view.eye_offset.x, ceye.y + view.eye_offset.y,
                                          ceye.z + view.eye_offset.z});
                const Clearance c = clearance(car, eye, true);
                cworst = std::min({cworst, c.door, c.roof, c.behind, c.windshield});
            }
        }
        MESSAGE(name, ": copiloto, holgura minima ", cworst, " m");
        CHECK(cworst >= margin);
    }
}

TEST_CASE("cabin_volume: the nominal eye and 1000 points inside come out bit for bit") {
    for (const char* name : kCars) {
        const rally::sim::CarData car = load_car(name);
        for (const bool copilot : {false, true}) {
            const CabinVolume volume = cabin_volume(car, copilot);
            const Vec3d nominal = copilot ? Vec3d{-car_eye(car).x, car_eye(car).y, car_eye(car).z}
                                          : driver_eye(car, SeatOffsetCm{});
            INFO(name, copilot ? " (copiloto)" : " (conductor)");
            const HeadView view = head_view(0.0, 0.0, nullptr);
            const Vec3d start = {nominal.x + view.eye_offset.x, nominal.y + view.eye_offset.y,
                                 nominal.z + view.eye_offset.z};
            CHECK(same_bits(clamp_to_cabin(volume, start), start));

            rally::sim::Pcg32 rng(0x5eed, copilot ? 2 : 1);
            const auto uniform = [&rng](double lo, double hi) {
                return lo + (hi - lo) * static_cast<double>(rng.next_float01());
            };
            int tested = 0;
            while (tested < 1000) {
                const Vec3d p = copilot
                                    ? Vec3d{uniform(volume.x_max - 0.4, volume.x_max),
                                            uniform(car_eye(car).y - 0.4, car_eye(car).y + 0.12),
                                            uniform(volume.z_max - 0.9, volume.z_max)}
                                    : Vec3d{uniform(volume.x_min, volume.x_min + 0.4),
                                            uniform(car_eye(car).y - 0.4, car_eye(car).y + 0.12),
                                            uniform(volume.z_max - 0.9, volume.z_max)};
                if (volume.wy * p.y + volume.wz * p.z > volume.wc ||
                    volume.ry * p.y + volume.rz * p.z > volume.rc) {
                    continue; // delante del parabrisas o sobre el techo: fuera
                }
                CHECK(same_bits(clamp_to_cabin(volume, p), p));
                ++tested;
            }
        }
    }
}

TEST_CASE("cabin_volume: moving the eye 1 mm per step never makes the final eye jump more") {
    for (const char* name : kCars) {
        const rally::sim::CarData car = load_car(name);
        const CabinVolume volume = cabin_volume(car, false);
        const Vec3d start = driver_eye(car, SeatOffsetCm{});
        // Hacia la puerta, el techo, atrás, el parabrisas y en diagonales.
        const std::array<Vec3d, 9> dirs = {{{0.0, 1.0, -1.0},
                                            {-1.0, 1.0, -0.3},
                                            {-1.0, 0.0, 0.0},
                                            {0.0, 1.0, 0.0},
                                            {0.0, 0.0, 1.0},
                                            {0.0, 0.5, -1.0},
                                            {0.0, 1.0, -0.2},
                                            {-1.0, 1.0, 1.0},
                                            {-1.0, 1.0, -1.0}}};
        for (const Vec3d& d : dirs) {
            const double n = std::sqrt(d.x * d.x + d.y * d.y + d.z * d.z);
            Vec3d previous = clamp_to_cabin(volume, start);
            for (int i = 1; i <= 1500; ++i) {
                const double s = 0.001 * i / n;
                const Vec3d out = clamp_to_cabin(
                    volume, {start.x + d.x * s, start.y + d.y * s, start.z + d.z * s});
                INFO(name, " dir (", d.x, ",", d.y, ",", d.z, ") paso ", i);
                CHECK(dist(out, previous) <= 0.001 + 1e-9);
                previous = out;
            }
        }
    }
}

TEST_CASE("cabin_volume: the copilot volume is the driver one mirrored in x") {
    for (const char* name : kCars) {
        const rally::sim::CarData car = load_car(name);
        const CabinVolume d = cabin_volume(car, false);
        const CabinVolume c = cabin_volume(car, true);
        CHECK(c.x_max == -d.x_min);
        CHECK(d.x_max > 1e300);
        CHECK(c.x_min < -1e300);
        CHECK(c.ry == d.ry);
        CHECK(c.rz == d.rz);
        CHECK(c.rc == d.rc);
        CHECK(c.z_max == d.z_max);
        CHECK(c.wy == d.wy);
        CHECK(c.wz == d.wz);
        CHECK(c.wc == d.wc);
    }
}
