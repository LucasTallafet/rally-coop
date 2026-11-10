#pragma once

// Salidas guionizadas del tramo (tarea formato-2, lote 6; criterios 1 y 11 del prompt): el 205
// (tests/data/cars/reference.car) sale de la calzada de lazadas con un rumbo fijo en el mundo y
// una velocidad fija, en la recta de salida, en la curva de R 80 y por el interior de las dos
// horquillas. Por paso y rueda, el contacto y el salto de carga |L(n) − L(n−1)|, por bandas de la
// distancia del centro del coche fuera del borde de la calzada más cercana (las de la tabla de
// mensajes/lote-5/prueba/conclusiones-consultor.md). Lo usan stage_exits.cpp (la medida a mano de
// la parte A) y, en la parte B, los tests del criterio 1.

#include "car_worlds.h"
#include "sim/sim_world.h"
#include "sim/stage_track.h"
#include "stage_driver.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace rally::tests {

// Bandas de la distancia del centro del coche fuera del borde de la calzada (m): calzada a más de
// 1 m dentro, último metro, 0–1 (arcén), 1–2, 2–4, 4–8, 8–15 y 15–45. Más allá no se cuenta.
inline constexpr int kExitBands = 8;
inline constexpr std::array<double, kExitBands + 1> kExitBandEdges = {-1e9, -1.0, 0.0,  1.0, 2.0,
                                                                      4.0,  8.0,  15.0, 45.0};
inline constexpr std::array<const char*, kExitBands> kExitBandNames = {"calzada (> 1 m dentro)",
                                                                       "último metro de calzada",
                                                                       "0–1 m (arcén)",
                                                                       "1–2 m",
                                                                       "2–4 m",
                                                                       "4–8 m",
                                                                       "8–15 m",
                                                                       "15–45 m"};

// Pasos-rueda de una banda (o de varias juntas).
struct ExitBand {
    int wheel_steps = 0;
    int no_contact = 0;
    std::vector<float> jumps; // |L(n) − L(n−1)| (N), los pasos-rueda con el paso anterior medido

    void add(const ExitBand& o) {
        wheel_steps += o.wheel_steps;
        no_contact += o.no_contact;
        jumps.insert(jumps.end(), o.jumps.begin(), o.jumps.end());
    }
    double percent() const {
        return wheel_steps > 0 ? 100.0 * no_contact / static_cast<double>(wheel_steps) : 0.0;
    }
    // p99 del salto de carga (N); 0 sin pasos.
    double p99() const {
        if (jumps.empty()) {
            return 0.0;
        }
        std::vector<float> sorted = jumps;
        std::sort(sorted.begin(), sorted.end());
        return static_cast<double>(sorted[sorted.size() * 99 / 100]);
    }
};

struct ExitStats {
    std::array<ExitBand, kExitBands> bands;
    double farthest = 0.0; // la mayor distancia fuera del borde (m)
    double max_impact = 0.0;
    bool placed = false; // la pasada recolocó el coche (atajo) y la salida se cortó ahí

    void add(const ExitStats& o) {
        for (int b = 0; b < kExitBands; ++b) {
            bands[static_cast<std::size_t>(b)].add(o.bands[static_cast<std::size_t>(b)]);
        }
        farthest = std::max(farthest, o.farthest);
        max_impact = std::max(max_impact, o.max_impact);
        placed = placed || o.placed;
    }
    // Las bandas [first, last] juntas.
    ExitBand range(int first, int last) const {
        ExitBand out;
        for (int b = first; b <= last; ++b) {
            out.add(bands[static_cast<std::size_t>(b)]);
        }
        return out;
    }
    // 0–4 m fuera del borde (arcén, cuneta y comienzo del talud) y 4–15 m (el resto de la banda).
    ExitBand near_band() const { return range(2, 4); }
    ExitBand rest_band() const { return range(5, 6); }
};

// Una salida: en `s` de lazadas, a `t` del eje, con el rumbo del eje en s más `turn_deg` (+ a la
// derecha), que el coche sujeta en el mundo durante `seconds`.
struct ExitPlace {
    const char* name;
    double s;
    double t;
    double turn_deg;
    double seconds;
};

// Recta de salida, 12° a la derecha; curva de R 80 (a derechas: clotoide desde s 100, arco s
// 140–210), recto por la tangente en s 120: sale por fuera; horquilla 1 (a derechas, arco s
// 740–748,85) y horquilla 2 (a izquierdas, arco s 838,85–847,70), 20° hacia el interior desde la
// recta de antes.
inline constexpr std::array<ExitPlace, 4> kExitPlaces = {{
    {"recta", 20.0, 1.0, 12.0, 6.0},
    {"curva R 80", 120.0, 0.0, 0.0, 6.0},
    {"horquilla 1 por dentro", 725.0, 1.0, 20.0, 4.0},
    {"horquilla 2 por dentro", 823.0, -1.0, -20.0, 4.0},
}};

inline constexpr std::array<double, 3> kExitSpeedsKmh = {30.0, 60.0, 90.0};

// Distancia del punto (x, z) fuera del borde de la calzada más cercana: negativa en una calzada
// (−(semiancho − |t|)), positiva fuera.
inline double outside_road_edge(const sim::StageTrack& track, double x, double z) {
    sim::StageProjection p;
    if (!track.on_any_road(x, z, p)) {
        p = track.acquire(x, z);
    }
    const sim::StagePose pose = track.pose(p.s);
    const double half = p.t >= 0.0 ? pose.half_width_right : pose.half_width_left;
    return p.distance - half;
}

// La salida `place` a `kmh` sobre el suelo `variant` de `text` (lazadas), con el microrrelieve del
// juego: place.seconds (las primeras 0,25 s no se cuentan) o hasta que el coche está a más de
// 45 m fuera o la pasada lo recoloca. Con `step_ms`, el tiempo de cada paso (ms; solo medida).
// `micro_relief` = false apaga el microrrelieve del tramo (el criterio 1a de formato-2; el
// recorrido y el conductor no cambian).
inline ExitStats run_exit(const std::string& text, const sim::StageTrack& track,
                          sim::StageGroundVariant variant, const ExitPlace& place, double kmh,
                          std::vector<double>* step_ms = nullptr, bool micro_relief = true) {
    constexpr double kPi = 3.14159265358979323846;
    constexpr int kSteps = sim::kStepsPerSecond;
    const double speed = kmh / 3.6;
    const sim::StagePose p = track.pose(place.s);
    const double heading = p.heading + place.turn_deg * kPi / 180.0;
    sim::CarSpawn spawn = sim::stage_spawn(track, place.s, static_cast<float>(speed));
    spawn.x = p.x + place.t * rmath::cos(p.heading);
    spawn.z = p.z + place.t * rmath::sin(p.heading);
    spawn.y = p.y + p.bank * place.t;
    spawn.yaw = static_cast<float>(-heading);
    sim::WorldDesc desc;
    desc.stage_text = text;
    desc.stage_variant = variant;
    desc.stage_micro_relief = micro_relief;
    desc.car = true;
    desc.car_data = reference_car();
    desc.car_spawn = spawn;
    sim::SimWorld world;
    world.build(desc);
    world.seal();
    world.set_telemetry(true);
    ExitStats out;
    std::array<float, sim::kWheelCount> previous{};
    const int settle = kSteps / 4;
    const std::int32_t shortcuts = world.stage_run()->shortcuts;
    const auto steps = static_cast<int>(place.seconds * kSteps);
    for (int i = 0; i < steps; ++i) {
        const sim::BodyPose pose = world.car_pose();
        const sim::BodyVelocity v = world.car_velocity();
        sim::SimInput in;
        in.steer = static_cast<std::int16_t>(
            std::clamp(2.5 * wrap_angle(heading - car_heading(pose)), -1.0, 1.0) * sim::kAxisMax);
        in.throttle = static_cast<std::int16_t>(
            std::clamp(0.5 * (speed - rmath::hypot(v.vx, v.vz)), 0.0, 1.0) * sim::kAxisMax);
        const auto t0 = std::chrono::steady_clock::now();
        world.step(in);
        if (step_ms != nullptr) {
            step_ms->push_back(
                std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0)
                    .count());
        }
        if (world.stage_run()->shortcuts != shortcuts) {
            out.placed = true;
            break;
        }
        const sim::CarTelemetry& tel = world.telemetry();
        const sim::BodyPose after = world.car_pose();
        const double d = outside_road_edge(track, after.x, after.z);
        if (i >= settle) {
            out.farthest = std::max(out.farthest, d);
            out.max_impact = std::max(out.max_impact, static_cast<double>(tel.impact));
            int band = -1;
            for (int b = 0; b < kExitBands; ++b) {
                if (d >= kExitBandEdges[static_cast<std::size_t>(b)] &&
                    d < kExitBandEdges[static_cast<std::size_t>(b) + 1]) {
                    band = b;
                }
            }
            if (band >= 0) {
                ExitBand& eb = out.bands[static_cast<std::size_t>(band)];
                for (int w = 0; w < sim::kWheelCount; ++w) {
                    const auto& wheel = tel.wheels[static_cast<std::size_t>(w)];
                    ++eb.wheel_steps;
                    eb.no_contact += wheel.contact == 0 ? 1 : 0;
                    if (i > settle) {
                        eb.jumps.push_back(
                            std::abs(wheel.load - previous[static_cast<std::size_t>(w)]));
                    }
                }
            }
        }
        for (int w = 0; w < sim::kWheelCount; ++w) {
            previous[static_cast<std::size_t>(w)] = tel.wheels[static_cast<std::size_t>(w)].load;
        }
        if (d > 45.0) {
            break;
        }
    }
    return out;
}

} // namespace rally::tests
