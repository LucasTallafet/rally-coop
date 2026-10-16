// Tarea 2.8, parte S: ABS, control de tracción y dureza de la suspensión (sim/car_controller.h,
// DriverAids de sim/steer_assist.h). Criterios 1–8 del prompt (docs/prompts/fase-2/2.8.md), con
// los cambios de mensajes/lote-1/2.8-sim-revision.md, y la tabla de regímenes del plan
// (mensajes/2.8-sim-plan.md). Las filas «nota:» son la tabla «cada ajuste se nota».

#include "car_worlds.h"
#include "io/recording.h"
#include "sim/car_controller.h"
#include "sim/car_data.h"
#include "sim/drivetrain.h"
#include "sim/jolt_runtime.h"
#include "sim/sim_input.h"
#include "sim/sim_world.h"
#include "sim/steer_assist.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

namespace {

using rally::sim::BodyPose;
using rally::sim::CarData;
using rally::sim::kAxisMax;
using rally::sim::kStepsPerSecond;
using rally::sim::kWheelCount;
using rally::sim::kWheelSubsteps;
using rally::sim::SimInput;
using rally::sim::SimWorld;
using rally::sim::Surface;
using rally::sim::SuspensionSetting;
using rally::sim::TractionControl;
using rally::sim::WorldDesc;
using rally::tests::flat_world;
using rally::tests::reference_car;

constexpr double kPi = 3.14159265358979323846;
constexpr double kGravity = 9.81;
constexpr double kKmh = 1.0 / 3.6; // m/s por km/h
constexpr double kDeg = 180.0 / kPi;

SimInput axes_input(double steer, double throttle, double brake, std::uint16_t buttons = 0) {
    SimInput input;
    input.steer = static_cast<std::int16_t>(std::clamp(steer, -1.0, 1.0) * kAxisMax);
    input.throttle = static_cast<std::int16_t>(std::clamp(throttle, 0.0, 1.0) * kAxisMax);
    input.brake = static_cast<std::int16_t>(std::clamp(brake, 0.0, 1.0) * kAxisMax);
    input.buttons = buttons;
    return input;
}

void run(SimWorld& world, const SimInput& input, int steps) {
    for (int i = 0; i < steps; ++i) {
        world.step(input);
    }
}

double horizontal_speed(const SimWorld& world) {
    const rally::sim::BodyVelocity v = world.car_velocity();
    return std::sqrt(static_cast<double>(v.vx * v.vx + v.vz * v.vz));
}

double horizontal_distance(const BodyPose& a, const BodyPose& b) {
    return std::sqrt((a.x - b.x) * (a.x - b.x) + (a.z - b.z) * (a.z - b.z));
}

// Ejes del coche en el mundo a partir de su orientación.
struct Axes {
    double fx, fy, fz; // adelante (-z local)
    double rx, ry, rz; // derecha (+x local)
};

Axes axes(const BodyPose& p) {
    const double x = p.qx;
    const double y = p.qy;
    const double z = p.qz;
    const double w = p.qw;
    const double c0[3] = {1 - 2 * (y * y + z * z), 2 * (x * y + w * z), 2 * (x * z - w * y)};
    const double c2[3] = {2 * (x * z + w * y), 2 * (y * z - w * x), 1 - 2 * (x * x + y * y)};
    return Axes{-c2[0], -c2[1], -c2[2], c0[0], c0[1], c0[2]};
}

// Cabeceo (> 0 morro arriba) y balanceo (> 0 con el lado derecho arriba), en grados.
double pitch_deg(const BodyPose& p) { return std::asin(axes(p).fy) * kDeg; }
double roll_deg(const BodyPose& p) { return std::asin(axes(p).ry) * kDeg; }
// Rumbo: 0 mirando a -z, > 0 girando a la derecha.
double heading_deg(const BodyPose& p) {
    const Axes a = axes(p);
    return std::atan2(a.fx, -a.fz) * kDeg;
}

// Residuo de una serie (de 960 Hz) respecto a la media de sus 96 muestras anteriores (0,1 s):
// RMS y pico a pico. Un ciclo límite de los subpasos o un relé lento sale aquí; un cambio lento
// del valor regulado, no. reset() corta la serie (la ayuda deja de actuar).
class Ripple {
public:
    static constexpr int kWindow = 96;

    void add(double x) {
        if (filled_ == kWindow) {
            const double residual = x - sum_ / kWindow;
            sum_sq_ += residual * residual;
            ++count_;
            low_ = std::min(low_, residual);
            high_ = std::max(high_, residual);
        }
        const auto k = static_cast<std::size_t>(next_ % kWindow);
        if (filled_ == kWindow) {
            sum_ -= ring_[k];
        } else {
            ++filled_;
        }
        ring_[k] = x;
        sum_ += x;
        ++next_;
    }
    void reset() {
        filled_ = 0;
        next_ = 0;
        sum_ = 0.0;
    }
    double rms() const { return count_ > 0 ? std::sqrt(sum_sq_ / count_) : 0.0; }
    double peak_to_peak() const { return count_ > 0 ? high_ - low_ : 0.0; }
    int count() const { return count_; }

private:
    std::array<double, kWindow> ring_{};
    int filled_ = 0;
    int next_ = 0;
    double sum_ = 0.0;
    double sum_sq_ = 0.0;
    int count_ = 0;
    double low_ = 1e9;
    double high_ = -1e9;
};

// Umbral de «sin oscilación» del plan (§6): RMS del residuo ≤ 0,1 (10 % del objetivo).
constexpr double kMaxRippleRms = 0.1;

// --- Frenadas ---------------------------------------------------------------------------------

struct Stop {
    double distance = 0.0;
    // |Cambio de rumbo| en grados desde que entra la dirección: hasta parar y hasta bajar de
    // 36 km/h (carcass_damping_fade, 10 m/s: por debajo, el recorte de la huella de la 2.5).
    double turned = 0.0;
    double turned_fast = 0.0;
    double ripple_rms = 0.0;
    double ripple_p2p = 0.0;
    int ripple_samples = 0;
    bool locked_above_gate = false; // alguna rueda con ω = 0 por encima de la puerta del ABS
    double max_abs_cut = 0.0;       // N·m
};

// Frenada a fondo desde la velocidad de salida de `desc` hasta parar (< 0,05 m/s), con la
// dirección en `steer` desde el paso `steer_from`. El residuo del ABS se mide de 90 a 15 km/h en
// cada rueda que modula.
Stop brake_stop(const WorldDesc& desc, double steer = 0.0, std::uint16_t buttons = 0,
                int steer_from = 0) {
    SimWorld world;
    world.build(desc);
    world.seal();
    const BodyPose start = world.car_pose();
    std::array<Ripple, kWheelCount> ripples;
    Stop stop;
    int steps = 0;
    double steer_heading = heading_deg(start); // rumbo al entrar la dirección
    while (horizontal_speed(world) > 0.05 && steps < 30 * kStepsPerSecond) {
        if (steps == steer_from) {
            steer_heading = heading_deg(world.car_pose());
        }
        world.step(axes_input(steps >= steer_from ? steer : 0.0, 0.0, 1.0, buttons));
        ++steps;
        const double v = horizontal_speed(world);
        if (v >= reference_car().carcass_damping_fade) {
            stop.turned_fast = std::abs(heading_deg(world.car_pose()) - steer_heading);
        }
        for (int i = 0; i < kWheelCount; ++i) {
            const rally::sim::WheelOutput& w = world.car_controller().wheel(i);
            stop.max_abs_cut = std::max(stop.max_abs_cut, static_cast<double>(w.abs_torque_cut));
            if (w.omega == 0.0f && std::abs(w.contact_speed) > 1.2 * rally::sim::kAbsMinSpeed) {
                stop.locked_above_gate = true;
            }
            Ripple& ripple = ripples[static_cast<std::size_t>(i)];
            for (const float x : w.abs_slip) {
                if (x == 0.0f || v > 90.0 * kKmh || v < 15.0 * kKmh) {
                    ripple.reset();
                } else {
                    ripple.add(x);
                }
            }
        }
    }
    stop.distance = horizontal_distance(start, world.car_pose());
    stop.turned = std::abs(heading_deg(world.car_pose()) - steer_heading);
    for (const Ripple& r : ripples) {
        stop.ripple_rms = std::max(stop.ripple_rms, r.rms());
        stop.ripple_p2p = std::max(stop.ripple_p2p, r.peak_to_peak());
        stop.ripple_samples += r.count();
    }
    return stop;
}

// El coche de referencia en punto muerto con el tren abierto (como las frenadas de la 2.5) a
// 100 km/h sobre `surface`, con o sin ABS y sin ayuda de dirección.
WorldDesc neutral_brake_world(Surface surface, bool abs) {
    WorldDesc desc =
        rally::tests::in_neutral(flat_world(surface, false, static_cast<float>(100.0 * kKmh)));
    desc.bodies[0].half_x = 300.0f; // con la dirección girada cabe la curva
    desc.aids.abs = abs;
    return desc;
}

// --- Salidas ----------------------------------------------------------------------------------

struct Launch {
    double max_kappa = 0.0; // máximo de la media de 0,1 s de κ_r/κ pico de las motrices
    double time = -1.0;     // s hasta la velocidad objetivo (-1 si no llega)
    double ripple_rms = 0.0;
    double ripple_p2p = 0.0;
    int ripple_samples = 0;
    double max_cut = 0.0; // N·m de par motor quitado
};

// Salida a fondo desde parado (coche completo, caja automática, tras asentarse 1 s) hasta
// `target` m/s. κ_r de cada motriz en media móvil de 0,1 s, medido entre `low` y `high` m/s. El
// residuo de q del control se mide desde 0 hasta `target`. Con `gear` = −1, en marcha atrás
// (caja secuencial) y con el deslizamiento hacia atrás.
Launch launch(Surface surface, TractionControl traction, double target, double low, double high,
              int gear = 1) {
    WorldDesc desc = flat_world(surface);
    desc.aids.traction = traction;
    if (gear < 0) {
        desc.car_spawn.gear = rally::sim::kReverseGear;
        desc.aids.gearbox = rally::sim::GearboxMode::Sequential;
    }
    SimWorld world;
    world.build(desc);
    world.seal();
    run(world, SimInput{}, kStepsPerSecond);
    const CarData& car = reference_car();
    const double kappa_peak = car.tire(surface).slip_ratio_peak;
    const double sign = gear < 0 ? -1.0 : 1.0;
    constexpr int kMean = kStepsPerSecond / 10;
    std::array<std::array<double, kMean>, kWheelCount> ring{};
    std::array<double, kWheelCount> sum{};
    Ripple ripple;
    Launch result;
    for (int s = 0; s < 20 * kStepsPerSecond; ++s) {
        world.step(axes_input(0.0, 1.0, 0.0));
        const double v = horizontal_speed(world);
        const rally::sim::DrivetrainOutput& d = world.car_controller().drivetrain();
        result.max_cut = std::max(result.max_cut, static_cast<double>(d.traction_torque_cut));
        if (traction != TractionControl::Off) {
            for (const float q : d.traction_q) {
                ripple.add(q);
            }
        }
        for (int i = 0; i < kWheelCount; ++i) {
            const auto k = static_cast<std::size_t>(i);
            const double kappa = sign * world.car_controller().wheel(i).kappa / kappa_peak;
            const auto slot = static_cast<std::size_t>(s % kMean);
            sum[k] += kappa - ring[k][slot];
            ring[k][slot] = kappa;
            if (s >= kMean && v >= low && v <= high) {
                result.max_kappa = std::max(result.max_kappa, sum[k] / kMean);
            }
        }
        if (v >= target) {
            result.time = static_cast<double>(s + 1) / kStepsPerSecond;
            break;
        }
    }
    result.ripple_rms = ripple.rms();
    result.ripple_p2p = ripple.peak_to_peak();
    result.ripple_samples = ripple.count();
    return result;
}

const char* traction_name(TractionControl t) {
    switch (t) {
    case TractionControl::Off:
        return "sin control";
    case TractionControl::Low:
        return "bajo";
    case TractionControl::High:
        return "alto";
    }
    return "?";
}

const char* suspension_name(SuspensionSetting s) {
    switch (s) {
    case SuspensionSetting::Soft:
        return "blanda";
    case SuspensionSetting::Medium:
        return "media";
    case SuspensionSetting::Hard:
        return "dura";
    }
    return "?";
}

constexpr std::array kSettings = {SuspensionSetting::Soft, SuspensionSetting::Medium,
                                  SuspensionSetting::Hard};

// Cambio relativo entre dos valores seguidos de la tabla «cada ajuste se nota»: |b − a| / el
// menor.
double relative_change(double a, double b) {
    return std::abs(b - a) / std::max(1e-9, std::min(std::abs(a), std::abs(b)));
}

} // namespace

// --- Criterio 1: ABS en recta -------------------------------------------------------------------

TEST_CASE("aids: ABS full braking from 100 km/h stops within 1.1 times the threshold stop") {
    const rally::sim::JoltRuntime runtime;
    struct Case {
        Surface surface;
        double threshold; // m: frenada en el umbral de la 2.5 (reparto ideal)
    };
    const std::array<Case, 3> cases = {Case{Surface::Asphalt, 37.6}, Case{Surface::Gravel, 56.0},
                                       Case{Surface::Snow, 93.5}};
    std::string table;
    for (const Case& c : cases) {
        const Stop with_abs = brake_stop(neutral_brake_world(c.surface, true));
        const Stop locked = brake_stop(neutral_brake_world(c.surface, false));
        const Stop handbrake =
            brake_stop(neutral_brake_world(c.surface, false), 0.0, rally::sim::kButtonHandbrake);
        char row[300];
        std::snprintf(row, sizeof(row),
                      "%s: con ABS %.1f m (límite %.1f m = 1,1 × %.1f); sin ABS %.1f m; sin ABS "
                      "y con freno de mano %.1f m; residuo del ABS RMS %.3f, pico a pico %.3f "
                      "(%d muestras)\n",
                      rally::sim::surface_name(c.surface), with_abs.distance, 1.1 * c.threshold,
                      c.threshold, locked.distance, handbrake.distance, with_abs.ripple_rms,
                      with_abs.ripple_p2p, with_abs.ripple_samples);
        table += row;
        CHECK(with_abs.distance <= 1.1 * c.threshold);
        CHECK_FALSE(with_abs.locked_above_gate);
        CHECK(with_abs.ripple_samples > 0);
        CHECK(with_abs.ripple_rms <= kMaxRippleRms);
        if (c.surface == Surface::Asphalt) {
            CHECK(with_abs.distance < locked.distance);
        }
    }
    MESSAGE("frenada a fondo desde 100 km/h en punto muerto:\n", table);
}

TEST_CASE("aids: ABS braking in gear from 100 km/h on asphalt does not oscillate") {
    const rally::sim::JoltRuntime runtime;
    WorldDesc desc = flat_world(Surface::Asphalt, false, static_cast<float>(100.0 * kKmh));
    desc.aids.abs = true;
    const Stop with_abs = brake_stop(desc);
    desc.aids.abs = false;
    const Stop without = brake_stop(desc);
    MESSAGE("frenada a fondo desde 100 km/h con la marcha (automático): con ABS ",
            with_abs.distance, " m, sin ABS ", without.distance, " m; residuo del ABS RMS ",
            with_abs.ripple_rms, ", pico a pico ", with_abs.ripple_p2p, " (",
            with_abs.ripple_samples, " muestras)");
    CHECK(with_abs.ripple_samples > 0);
    CHECK(with_abs.ripple_rms <= kMaxRippleRms);
    CHECK_FALSE(with_abs.locked_above_gate);
}

// --- Criterio 2: ABS girando (y su fila de «cada ajuste se nota») -------------------------------

// Criterio 2 según la respuesta del consultor (mensajes/lote-1-respuesta-2.8-sim.md §1): la
// dirección entra 0,25 s después del freno (sin ABS las ruedas ya están bloqueadas) y el cambio
// de rumbo se mide desde ahí hasta bajar de 36 km/h, donde se separa el ABS de bloquear. Hasta
// parar y con la dirección a la vez que el freno solo se imprimen: por debajo de 36 km/h la rueda
// bloqueada aún da algo de lateral por el recorte de la huella de la 2.5 (hallazgo para la
// 2.10), y a la vez, el coche gira en los ~0,1 s que tardan en bloquearse las ruedas.
constexpr int kSteerAfterBrake = kStepsPerSecond / 4;

TEST_CASE("aids: ABS braking with 30 percent steer turns at least 10 degrees, locked at most 2") {
    const rally::sim::JoltRuntime runtime;
    std::string table;
    for (const Surface surface : {Surface::Asphalt, Surface::Snow}) {
        const Stop with_abs =
            brake_stop(neutral_brake_world(surface, true), 0.3, 0, kSteerAfterBrake);
        const Stop locked =
            brake_stop(neutral_brake_world(surface, false), 0.3, 0, kSteerAfterBrake);
        const Stop together_abs = brake_stop(neutral_brake_world(surface, true), 0.3);
        const Stop together_locked = brake_stop(neutral_brake_world(surface, false), 0.3);
        char row[400];
        std::snprintf(row, sizeof(row),
                      "%s: hasta 36 km/h, con ABS %.1f° y sin ABS %.2f°; hasta parar, %.1f° y "
                      "%.2f° (%.1f m y %.1f m); con la dirección a la vez que el freno, hasta "
                      "parar, %.1f° y %.2f°\n",
                      rally::sim::surface_name(surface), with_abs.turned_fast, locked.turned_fast,
                      with_abs.turned, locked.turned, with_abs.distance, locked.distance,
                      together_abs.turned, together_locked.turned);
        table += row;
        CHECK(with_abs.turned_fast >= 10.0);
        CHECK(locked.turned_fast <= 2.0);
        if (surface == Surface::Asphalt) {
            const double change = relative_change(locked.turned_fast, with_abs.turned_fast);
            MESSAGE("nota: ABS (sin / con): cambio de rumbo frenando y girando en asfalto hasta "
                    "36 km/h ",
                    locked.turned_fast, "° / ", with_abs.turned_fast, "° (", 100.0 * change, " %)");
            CHECK(change >= 0.15);
        }
    }
    MESSAGE("frenada a fondo desde 100 km/h con el 30 % de dirección a la derecha:\n", table);
}

// --- Criterio 3: ABS en la rampa
// ------------------------------------------------------------------

TEST_CASE("aids: on the 15 percent ramp with the brake the ABS changes nothing") {
    const rally::sim::JoltRuntime runtime;
    std::array<std::string, 2> states;
    std::array<double, 2> moved{};
    for (int a = 0; a < 2; ++a) {
        WorldDesc desc = rally::tests::in_neutral(rally::tests::ramp_world());
        desc.car_spawn.y = 0.25;
        desc.aids.abs = a == 1;
        SimWorld world;
        world.build(desc);
        world.seal();
        run(world, axes_input(0.0, 0.0, 1.0), 3 * kStepsPerSecond);
        const BodyPose start = world.car_pose();
        run(world, axes_input(0.0, 0.0, 1.0), 10 * kStepsPerSecond);
        const BodyPose end = world.car_pose();
        moved[static_cast<std::size_t>(a)] = std::sqrt((end.x - start.x) * (end.x - start.x) +
                                                       (end.y - start.y) * (end.y - start.y) +
                                                       (end.z - start.z) * (end.z - start.z));
        states[static_cast<std::size_t>(a)] = world.save_state();
    }
    MESSAGE("rampa del 15 % con el freno, 10 s: sin ABS ", moved[0] * 1000.0, " mm; con ABS ",
            moved[1] * 1000.0, " mm");
    CHECK(states[0] == states[1]);
    CHECK(moved[1] < 0.001);
}

// --- Criterios 4 y 5: control de tracción
// ---------------------------------------------------------

TEST_CASE("aids: traction control on snow from rest keeps the slip within its limits") {
    const rally::sim::JoltRuntime runtime;
    const double target = 50.0 * kKmh;
    const Launch off = launch(Surface::Snow, TractionControl::Off, target, 10.0 * kKmh, target);
    const Launch low = launch(Surface::Snow, TractionControl::Low, target, 10.0 * kKmh, target);
    const Launch high = launch(Surface::Snow, TractionControl::High, target, 10.0 * kKmh, target);
    std::string table;
    for (const Launch* l : {&off, &low, &high}) {
        const TractionControl t = l == &off   ? TractionControl::Off
                                  : l == &low ? TractionControl::Low
                                              : TractionControl::High;
        char row[260];
        std::snprintf(row, sizeof(row),
                      "%s: κ_r/κ pico máximo (media de 0,1 s, 10–50 km/h) %.2f; 0–50 km/h en "
                      "%.2f s (%.2f m/s²); par quitado máximo %.0f N·m; residuo de q RMS %.3f, "
                      "pico a pico %.3f\n",
                      traction_name(t), l->max_kappa, l->time,
                      l->time > 0.0 ? target / l->time : 0.0, l->max_cut, l->ripple_rms,
                      l->ripple_p2p);
        table += row;
    }
    MESSAGE("salida a fondo en nieve:\n", table);
    REQUIRE(off.time > 0.0);
    REQUIRE(low.time > 0.0);
    REQUIRE(high.time > 0.0);
    CHECK(off.max_kappa > 2.0);
    CHECK(low.max_kappa <= 2.0);
    CHECK(high.max_kappa <= 1.3);
    CHECK(high.time <= off.time); // aceleración media 0–50 con alto ≥ sin control
    CHECK(low.ripple_rms <= kMaxRippleRms);
    CHECK(high.ripple_rms <= kMaxRippleRms);
    // Cada ajuste se nota: κ_r máximo sin / bajo / alto.
    const double off_low = relative_change(off.max_kappa, low.max_kappa);
    const double low_high = relative_change(low.max_kappa, high.max_kappa);
    MESSAGE("nota: control de tracción (sin / bajo / alto): κ_r/κ pico máximo en nieve ",
            off.max_kappa, " / ", low.max_kappa, " / ", high.max_kappa, " (", 100.0 * off_low,
            " % y ", 100.0 * low_high, " %)");
    CHECK(off_low >= 0.15);
    CHECK(low_high >= 0.15);
}

TEST_CASE("aids: traction control high does not choke the start in sand") {
    const rally::sim::JoltRuntime runtime;
    const double target = 20.0 * kKmh;
    const Launch off = launch(Surface::Sand, TractionControl::Off, target, 0.0, target);
    const Launch high = launch(Surface::Sand, TractionControl::High, target, 0.0, target);
    MESSAGE("arena, a fondo hasta 20 km/h: sin control ", off.time, " s; con alto ", high.time,
            " s (límite 1,05 × sin control); residuo de q RMS ", high.ripple_rms, ", pico a pico ",
            high.ripple_p2p);
    REQUIRE(off.time > 0.0);
    REQUIRE(high.time > 0.0);
    CHECK(high.time < 10.0);
    CHECK(high.time <= 1.05 * off.time);
    CHECK(high.ripple_rms <= kMaxRippleRms);
}

TEST_CASE("aids: traction control in reverse on snow limits the slip backwards") {
    const rally::sim::JoltRuntime runtime;
    const Launch off =
        launch(Surface::Snow, TractionControl::Off, 30.0 * kKmh, 10.0 * kKmh, 30.0 * kKmh, -1);
    const Launch high =
        launch(Surface::Snow, TractionControl::High, 30.0 * kKmh, 10.0 * kKmh, 30.0 * kKmh, -1);
    MESSAGE("marcha atrás en nieve a fondo, κ_r/κ pico máximo de 10 a 30 km/h: sin control ",
            off.max_kappa, " (", off.time, " s); alto ", high.max_kappa, " (", high.time, " s)");
    REQUIRE(high.time > 0.0);
    CHECK(high.max_kappa <= 1.3);
    CHECK(high.max_cut > 0.0);
}

// Margen de estabilidad del corte con el embrague cerrado (revisión del plan §3): K = h·α/Δω,
// con α la aceleración que el corte entero quita a una motriz sola (autoblocante abierto) en 1ª
// con el par máximo a plena presión, y Δω la banda parado (la más estrecha). Estable con K < 2.
TEST_CASE("aids: the traction cut gain stays below 2 on the four cars") {
    const std::array<const char*, 4> names = {"mid_awd_turbo", "front_awd_turbo_5cyl",
                                              "front_rwd_na", "front_awd_turbo_groupa"};
    std::string table;
    double worst = 0.0;
    for (const char* name : names) {
        std::string bytes;
        std::string error;
        REQUIRE(rally::io::read_file(std::string(RALLY_SOURCE_DIR "/data/cars/") + name + ".car",
                                     bytes, error));
        const CarData car = rally::sim::load_car_or_abort(bytes, name, rally::sim::CarKeys::Full);
        double peak = 0.0;
        for (int i = 0; i < car.torque_points; ++i) {
            peak = std::max(peak, static_cast<double>(car.torque_atm[static_cast<std::size_t>(i)]));
        }
        peak *= car.torque_scale * (1.0 + car.boost_gain * car.boost_max);
        const double share =
            0.5 * std::max(car.drive_front_share, 1.0f - car.drive_front_share); // por rueda
        const double ratio = car.gear_ratio_1 * share;
        const double alpha =
            peak * ratio / (car.engine_inertia * ratio * ratio + car.wheel_inertia);
        const double band =
            rally::sim::kTractionBand * rally::sim::kTractionMinSlip / car.wheel_radius;
        const double h = 1.0 / (kStepsPerSecond * kWheelSubsteps);
        const double k = h * alpha / band;
        worst = std::max(worst, k);
        char row[160];
        std::snprintf(row, sizeof(row),
                      "%s: par %.0f N·m, α %.0f rad/s², banda %.2f rad/s, K %.2f\n", name, peak,
                      alpha, band, k);
        table += row;
    }
    MESSAGE("ganancia del corte por subpaso, parado y en 1ª:\n", table);
    CHECK(worst < 2.0);
}

// --- Criterio 6: suspensión -------------------------------------------------------------------

namespace {

struct Rest {
    double com_y = 0.0;
    double pitch = 0.0;
    double roll = 0.0;
    std::array<double, 2> free_travel{}; // m hasta el tope, delante y detrás
    double compression = 0.0;            // media de las cuatro ruedas
};

Rest at_rest(SuspensionSetting setting) {
    WorldDesc desc = flat_world(Surface::Asphalt);
    desc.aids.suspension = setting;
    SimWorld world;
    world.build(desc);
    world.seal();
    run(world, SimInput{}, 3 * kStepsPerSecond);
    Rest rest;
    rest.com_y = world.car_center_of_mass().y;
    rest.pitch = pitch_deg(world.car_pose());
    rest.roll = roll_deg(world.car_pose());
    for (int i = 0; i < kWheelCount; ++i) {
        const double length =
            world.car_wheel(i).suspension_length - rally::sim::kMinSuspensionLength;
        rest.free_travel[i < 2 ? 0 : 1] += 0.5 * length;
        rest.compression += 0.25 * world.car_controller().wheel(i).compression;
    }
    return rest;
}

// Balanceo medio (grados, positivo) en curva estable a 0,6 g y 60 km/h en asfalto: la dirección
// sigue la aceleración lateral con un integrador y el acelerador la velocidad con un PI. Mide
// los 2 últimos de 8 s. `lateral` sale con la aceleración lateral media medida (g).
double steady_roll(SuspensionSetting setting, double& lateral) {
    WorldDesc desc = rally::tests::open_driveline(
        flat_world(Surface::Asphalt, false, static_cast<float>(60.0 * kKmh)));
    desc.bodies[0].half_x = 300.0f;
    desc.aids.suspension = setting;
    SimWorld world;
    world.build(desc);
    world.seal();
    world.set_telemetry(true);
    double steer = 0.1;
    double integral = 0.0;
    double roll_sum = 0.0;
    double lateral_sum = 0.0;
    int measured = 0;
    for (int s = 0; s < 8 * kStepsPerSecond; ++s) {
        const double error_v = 60.0 * kKmh - horizontal_speed(world);
        integral = std::clamp(integral + error_v / kStepsPerSecond, -2.0, 2.0);
        const double throttle = std::clamp(0.5 * error_v + 0.5 * integral, 0.0, 1.0);
        world.step(axes_input(steer, throttle, 0.0));
        const double a_lat = world.telemetry().a_right / kGravity;
        steer = std::clamp(steer + 0.3 * (0.6 - a_lat) / kStepsPerSecond, 0.0, 1.0);
        if (s >= 6 * kStepsPerSecond) {
            roll_sum += std::abs(roll_deg(world.car_pose()));
            lateral_sum += a_lat;
            ++measured;
        }
    }
    lateral = lateral_sum / measured;
    return roll_sum / measured;
}

// Cabeceo medio (grados, positivo) frenando a 0,8 g en asfalto en punto muerto, entre 80 y
// 40 km/h: el freno sigue la deceleración con un integrador. `decel` sale con la media (g).
double braking_pitch(SuspensionSetting setting, double& decel) {
    WorldDesc desc = rally::tests::in_neutral(
        flat_world(Surface::Asphalt, false, static_cast<float>(100.0 * kKmh)));
    desc.aids.suspension = setting;
    SimWorld world;
    world.build(desc);
    world.seal();
    world.set_telemetry(true);
    double level = 0.6;
    double pitch_sum = 0.0;
    double decel_sum = 0.0;
    int measured = 0;
    while (horizontal_speed(world) > 40.0 * kKmh) {
        world.step(axes_input(0.0, 0.0, level));
        const double a = -world.telemetry().a_forward / kGravity;
        level = std::clamp(level + 2.0 * (0.8 - a) / kStepsPerSecond, 0.0, 1.0);
        if (horizontal_speed(world) < 80.0 * kKmh) {
            pitch_sum += -pitch_deg(world.car_pose());
            decel_sum += a;
            ++measured;
        }
    }
    decel = decel_sum / std::max(measured, 1);
    return pitch_sum / std::max(measured, 1);
}

} // namespace

TEST_CASE("aids: the three suspension settings rest at the same height and attitude") {
    const rally::sim::JoltRuntime runtime;
    const Rest medium = at_rest(SuspensionSetting::Medium);
    std::string table;
    for (const SuspensionSetting s : kSettings) {
        const Rest rest = at_rest(s);
        char row[240];
        std::snprintf(row, sizeof(row),
                      "%s: centro de masas a %.4f m, cabeceo %.4f°, balanceo %.4f°; recorrido "
                      "libre hasta el tope delante %.2f cm, detrás %.2f cm\n",
                      suspension_name(s), rest.com_y, rest.pitch, rest.roll,
                      100.0 * rest.free_travel[0], 100.0 * rest.free_travel[1]);
        table += row;
        CHECK(std::abs(rest.com_y - medium.com_y) <= 0.001);
        CHECK(std::abs(rest.pitch - medium.pitch) <= 0.05);
        CHECK(std::abs(rest.roll - medium.roll) <= 0.05);
        // Si en blanda quedaran menos de 3 cm hasta el tope, la tarea para (prompt).
        CHECK(rest.free_travel[0] >= 0.03);
        CHECK(rest.free_travel[1] >= 0.03);
    }
    MESSAGE("parado 3 s en llano:\n", table);
}

TEST_CASE("aids: soft rolls and pitches at least 1.3 times as much as hard") {
    const rally::sim::JoltRuntime runtime;
    std::array<double, 3> roll{};
    std::array<double, 3> pitch{};
    std::string table;
    for (std::size_t k = 0; k < kSettings.size(); ++k) {
        double lateral = 0.0;
        double decel = 0.0;
        roll[k] = steady_roll(kSettings[k], lateral);
        pitch[k] = braking_pitch(kSettings[k], decel);
        char row[200];
        std::snprintf(row, sizeof(row),
                      "%s: balanceo %.2f° a %.3f g; cabeceo %.2f° frenando a %.3f g\n",
                      suspension_name(kSettings[k]), roll[k], lateral, pitch[k], decel);
        table += row;
        CHECK(std::abs(lateral - 0.6) <= 0.03);
        CHECK(std::abs(decel - 0.8) <= 0.04);
    }
    MESSAGE("suspensión en asfalto:\n", table);
    CHECK(roll[0] >= 1.3 * roll[2]);
    CHECK(pitch[0] >= 1.3 * pitch[2]);
    const double soft_medium = relative_change(roll[0], roll[1]);
    const double medium_hard = relative_change(roll[1], roll[2]);
    MESSAGE("nota: suspensión (blanda / media / dura): balanceo a 0,6 g ", roll[0], "° / ", roll[1],
            "° / ", roll[2], "° (", 100.0 * soft_medium, " % y ", 100.0 * medium_hard, " %)");
    CHECK(soft_medium >= 0.15);
    CHECK(medium_hard >= 0.15);
}

TEST_CASE("aids: dropped from 0.5 m soft and hard bounce at most once more than medium") {
    const rally::sim::JoltRuntime runtime;
    std::array<int, 3> bounces{};
    std::string table;
    for (std::size_t k = 0; k < kSettings.size(); ++k) {
        const double equilibrium = at_rest(kSettings[k]).compression;
        WorldDesc desc = flat_world(Surface::Asphalt);
        desc.car_spawn.y = 0.5;
        desc.aids.suspension = kSettings[k];
        SimWorld world;
        world.build(desc);
        world.seal();
        bool above = false;
        for (int i = 0; i < 4 * kStepsPerSecond; ++i) {
            world.step(SimInput{});
            double c = 0.0;
            for (int w = 0; w < kWheelCount; ++w) {
                c += 0.25 * world.car_controller().wheel(w).compression;
            }
            const bool now_above = c > equilibrium + 0.01;
            if (now_above && !above) {
                ++bounces[k];
            }
            above = now_above;
        }
        table += std::string(suspension_name(kSettings[k])) + ": " + std::to_string(bounces[k]) +
                 " rebotes; ";
    }
    MESSAGE("caída desde 0,5 m (compresión > equilibrio + 1 cm): ", table);
    CHECK(bounces[0] <= bounces[1] + 1);
    CHECK(bounces[2] <= bounces[1] + 1);
}

// --- Criterio 7: la caja
// --------------------------------------------------------------------------

TEST_CASE("aids: the gearbox setting shows in the speed after 8 s flat out without paddles") {
    const rally::sim::JoltRuntime runtime;
    std::array<double, 2> speed{};
    for (int m = 0; m < 2; ++m) {
        WorldDesc desc = flat_world(Surface::Asphalt);
        desc.bodies[0] = rally::tests::slab(Surface::Asphalt, false, 1200.0);
        desc.aids.gearbox =
            m == 0 ? rally::sim::GearboxMode::Automatic : rally::sim::GearboxMode::Sequential;
        SimWorld world;
        world.build(desc);
        world.seal();
        run(world, SimInput{}, kStepsPerSecond);
        run(world, axes_input(0.0, 1.0, 0.0), 8 * kStepsPerSecond);
        speed[static_cast<std::size_t>(m)] = horizontal_speed(world) * 3.6;
    }
    const double change = relative_change(speed[0], speed[1]);
    MESSAGE("nota: caja (automática / secuencial sin levas): velocidad a los 8 s a fondo ",
            speed[0], " / ", speed[1], " km/h (", 100.0 * change, " %)");
    CHECK(change >= 0.15);
}

// --- Regímenes ----------------------------------------------------------------------------------

TEST_CASE("aids: below 6 km/h the ABS changes nothing and at 1 m/s the wheels still lock") {
    const rally::sim::JoltRuntime runtime;
    std::array<std::uint64_t, 2> hashes{};
    for (int a = 0; a < 2; ++a) {
        WorldDesc desc = flat_world(Surface::Asphalt, false, 1.0f);
        desc.aids.abs = a == 1;
        SimWorld world;
        world.build(desc);
        world.seal();
        run(world, axes_input(0.0, 0.0, 1.0), 2 * kStepsPerSecond);
        for (int i = 0; i < kWheelCount; ++i) {
            CHECK(world.car_controller().wheel(i).omega == 0.0f);
        }
        hashes[static_cast<std::size_t>(a)] = world.state_hash();
    }
    CHECK(hashes[0] == hashes[1]);
}

TEST_CASE("aids: cruising without reaching any limit, ABS and traction control change nothing") {
    const rally::sim::JoltRuntime runtime;
    std::array<std::uint64_t, 2> hashes{};
    float max_q = 0.0f;
    for (int a = 0; a < 2; ++a) {
        // En 2ª con la caja secuencial: sin cambios (al cerrar el embrague tras un cambio las
        // ruedas pueden patinar, y eso sí lo corta el control).
        WorldDesc desc = flat_world(Surface::Asphalt, false, 15.0f);
        desc.car_spawn.gear = 2;
        desc.aids.gearbox = rally::sim::GearboxMode::Sequential;
        if (a == 1) {
            desc.aids.abs = true;
            desc.aids.traction = TractionControl::High;
        }
        SimWorld world;
        world.build(desc);
        world.seal();
        for (int s = 0; s < 5 * kStepsPerSecond; ++s) {
            world.step(axes_input(0.1, 0.3, 0.0));
            for (const float q : world.car_controller().drivetrain().traction_q) {
                max_q = std::max(max_q, q);
            }
            CHECK(world.car_controller().drivetrain().traction_torque_cut == 0.0f);
        }
        hashes[static_cast<std::size_t>(a)] = world.state_hash();
    }
    MESSAGE("crucero suave: q máximo del control ", max_q);
    CHECK(hashes[0] == hashes[1]);
}

TEST_CASE("aids: in the air the traction control does not cut the engine") {
    const rally::sim::JoltRuntime runtime;
    WorldDesc desc = flat_world(Surface::Asphalt, false, 10.0f);
    desc.car_spawn.y = 1.0;
    desc.aids.traction = TractionControl::High;
    SimWorld world;
    world.build(desc);
    world.seal();
    int airborne = 0;
    for (int s = 0; s < kStepsPerSecond; ++s) {
        world.step(axes_input(0.0, 1.0, 0.0));
        bool any_contact = false;
        for (int i = 0; i < kWheelCount; ++i) {
            any_contact = any_contact || world.car_controller().wheel(i).contact;
        }
        if (!any_contact) {
            ++airborne;
            CHECK(world.car_controller().drivetrain().traction_torque_cut == 0.0f);
        }
    }
    CHECK(airborne > 10);
}

TEST_CASE("aids: with the handbrake the ABS leaves the rear locked and modulates the front") {
    const rally::sim::JoltRuntime runtime;
    WorldDesc desc = rally::tests::in_neutral(
        flat_world(Surface::Gravel, false, static_cast<float>(60.0 * kKmh)));
    desc.aids.abs = true;
    SimWorld world;
    world.build(desc);
    world.seal();
    int rear_locked = 0;
    int front_rolling = 0;
    int measured = 0;
    while (horizontal_speed(world) > 15.0 * kKmh) {
        world.step(axes_input(0.0, 0.0, 1.0, rally::sim::kButtonHandbrake));
        if (world.car_controller().drivetrain().clutch_open) {
            ++measured;
            const rally::sim::CarController& c = world.car_controller();
            rear_locked += c.wheel(rally::sim::kRearLeft).omega == 0.0f &&
                                   c.wheel(rally::sim::kRearRight).omega == 0.0f
                               ? 1
                               : 0;
            front_rolling += c.wheel(rally::sim::kFrontLeft).omega > 0.0f &&
                                     c.wheel(rally::sim::kFrontRight).omega > 0.0f
                                 ? 1
                                 : 0;
            CHECK(c.wheel(rally::sim::kRearLeft).abs_torque_cut == 0.0f);
            CHECK(c.wheel(rally::sim::kRearRight).abs_torque_cut == 0.0f);
        }
    }
    MESSAGE("freno y freno de mano con ABS en grava de 60 a 15 km/h: traseras bloqueadas en ",
            rear_locked, " de ", measured, " pasos; delanteras girando en ", front_rolling);
    CHECK(measured > 0);
    // El primer paso, el freno de mano aún sube (ControlState).
    CHECK(rear_locked >= measured - kStepsPerSecond / 10);
    CHECK(front_rolling == measured);
}

TEST_CASE("aids: braking backwards with the ABS keeps the wheels turning above 6 km/h") {
    const rally::sim::JoltRuntime runtime;
    WorldDesc desc = rally::tests::in_neutral(
        flat_world(Surface::Asphalt, false, static_cast<float>(-30.0 * kKmh)));
    desc.aids.abs = true;
    const Stop with_abs = brake_stop(desc);
    desc.aids.abs = false;
    const Stop without = brake_stop(desc);
    MESSAGE("frenada hacia atrás desde 30 km/h: con ABS ", with_abs.distance, " m, sin ABS ",
            without.distance, " m; par quitado máximo ", with_abs.max_abs_cut, " N·m");
    CHECK_FALSE(with_abs.locked_above_gate);
    CHECK(without.locked_above_gate);
    CHECK(with_abs.max_abs_cut > 0.0);
}

TEST_CASE("aids: after a reset with the aids on the next steps at rest have no cut") {
    const rally::sim::JoltRuntime runtime;
    WorldDesc desc = flat_world(Surface::Snow, false, static_cast<float>(60.0 * kKmh));
    desc.aids.abs = true;
    desc.aids.traction = TractionControl::High;
    SimWorld world;
    world.build(desc);
    world.seal();
    run(world, axes_input(0.0, 0.0, 1.0), kStepsPerSecond / 2);
    bool abs_acted = false;
    for (int i = 0; i < kWheelCount; ++i) {
        abs_acted = abs_acted || world.car_controller().wheel(i).abs_torque_cut > 0.0f;
    }
    CHECK(abs_acted);
    world.step(axes_input(0.0, 0.0, 0.0, rally::sim::kButtonReset));
    for (int s = 0; s < kStepsPerSecond / 4; ++s) {
        world.step(SimInput{});
        CHECK(world.car_controller().drivetrain().traction_torque_cut == 0.0f);
        for (int i = 0; i < kWheelCount; ++i) {
            CHECK(world.car_controller().wheel(i).abs_torque_cut == 0.0f);
        }
    }
}

// --- Criterio 8: SaveState y rendimiento -------------------------------------------------------

TEST_CASE("aids: SaveState with ABS, traction control and soft suspension restores exactly") {
    const rally::sim::JoltRuntime runtime;
    WorldDesc desc = flat_world(Surface::Snow);
    desc.aids.abs = true;
    desc.aids.traction = TractionControl::High;
    desc.aids.suspension = SuspensionSetting::Soft;
    // Salida a fondo (el control corta) y después frenada a fondo (el ABS modula).
    const auto script = [](int s) {
        return s < 4 * kStepsPerSecond ? axes_input(0.2, 1.0, 0.0) : axes_input(0.2, 0.0, 1.0);
    };
    constexpr int kSave = 3 * kStepsPerSecond;
    SimWorld world;
    world.build(desc);
    world.seal();
    for (int s = 0; s < kSave; ++s) {
        world.step(script(s));
    }
    REQUIRE(world.car_controller().drivetrain().traction_torque_cut > 0.0f);
    const std::string saved = world.save_state();
    const rally::sim::ControlState controls = world.controls();
    bool abs_acted = false;
    for (int s = kSave; s < kSave + 2 * kStepsPerSecond; ++s) {
        world.step(script(s));
        for (int i = 0; i < kWheelCount; ++i) {
            abs_acted = abs_acted || world.car_controller().wheel(i).abs_torque_cut > 0.0f;
        }
    }
    CHECK(abs_acted);
    SimWorld restored;
    restored.build(desc);
    restored.seal();
    restored.restore_state(saved, controls);
    for (int s = kSave; s < kSave + 2 * kStepsPerSecond; ++s) {
        restored.step(script(s));
    }
    CHECK(restored.save_state() == world.save_state());
    CHECK(restored.state_hash() == world.state_hash());
}

TEST_CASE("aids: ms per step on bench_lap with ABS, traction control high and soft suspension") {
    const rally::sim::JoltRuntime runtime;
    std::string bytes;
    std::string error;
    REQUIRE(rally::io::read_file(RALLY_TEST_DATA_DIR "/recordings/bench_lap.rrec", bytes, error));
    const rally::io::ParseResult parsed = rally::io::parse(bytes);
    REQUIRE(parsed.ok);
    const rally::io::Recording& recording = parsed.recording;
    WorldDesc desc;
    REQUIRE(rally::io::parse_world(recording.header.world, desc, error,
                                   recording.header.format_version));
    desc.car_data = rally::sim::load_car_or_abort(recording.header.car_text, "bench_lap",
                                                  rally::sim::CarKeys::Sim);
    // El coche de la grabación no lleva [settings] (es de antes de la 2.8): los de reference.car.
    desc.car_data.has_settings = true;
    desc.car_data.soft = reference_car().soft;
    desc.car_data.hard = reference_car().hard;
#ifdef NDEBUG
    const std::size_t max_steps = recording.footer.total_steps;
#else
    const std::size_t max_steps = 10 * kStepsPerSecond; // en Debug, solo 10 s
#endif
    std::string table;
    for (int a = 0; a < 2; ++a) {
        if (a == 1) {
            desc.aids.abs = true;
            desc.aids.traction = TractionControl::High;
            desc.aids.suspension = SuspensionSetting::Soft;
        }
        SimWorld world;
        world.build(desc);
        world.seal();
        std::vector<double> ms;
        ms.reserve(max_steps);
        for (const rally::io::InputRun& r : recording.runs) {
            for (std::uint32_t i = 0; i < r.count && ms.size() < max_steps; ++i) {
                const auto t0 = std::chrono::steady_clock::now();
                world.step(r.input);
                const auto t1 = std::chrono::steady_clock::now();
                ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
            }
        }
        double mean = 0.0;
        for (const double m : ms) {
            mean += m;
        }
        mean /= static_cast<double>(std::max<std::size_t>(ms.size(), 1));
        std::sort(ms.begin(), ms.end());
        const double p99 = ms.empty() ? 0.0 : ms[ms.size() * 99 / 100];
        const double max = ms.empty() ? 0.0 : ms.back();
        char row[200];
        std::snprintf(row, sizeof(row),
                      "%s: %zu pasos, media %.4f ms, p99 %.4f ms, máximo %.4f ms\n",
                      a == 0 ? "ayudas por defecto" : "ABS, control alto y blanda", ms.size(), mean,
                      p99, max);
        table += row;
    }
    MESSAGE("bench_lap (sin comprobar tiempos):\n", table);
}

// --- Riesgos del prompt: lo que se pregunta a Lucas -------------------------------------------

TEST_CASE("aids: power-on circle on gravel, maximum beta with traction control low and off") {
    const rally::sim::JoltRuntime runtime;
    std::string table;
    for (const TractionControl t : {TractionControl::Off, TractionControl::Low}) {
        WorldDesc desc = flat_world(Surface::Gravel, false, static_cast<float>(40.0 * kKmh));
        desc.bodies[0].half_x = 300.0f;
        desc.aids.traction = t;
        SimWorld world;
        world.build(desc);
        world.seal();
        world.set_telemetry(true);
        double beta = 0.0;
        for (int s = 0; s < 6 * kStepsPerSecond; ++s) {
            world.step(axes_input(0.5, 1.0, 0.0));
            if (horizontal_speed(world) > 5.0) {
                beta = std::max(beta, std::abs(static_cast<double>(world.telemetry().beta)));
            }
        }
        table +=
            std::string(traction_name(t)) + ": β máximo " + std::to_string(beta * kDeg) + "°; ";
    }
    MESSAGE("círculo en grava a fondo con el 50 % de dirección desde 40 km/h (solo se informa): ",
            table);
}
