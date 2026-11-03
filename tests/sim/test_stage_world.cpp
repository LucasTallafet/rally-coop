#include "car_worlds.h"
#include "jolt_allocation_counter.h"
#include "new_counter.h"
#include "sim/fnv1a.h"
#include "sim/jolt_runtime.h"
#include "sim/sim_world.h"
#include "sim/stage_ground.h"
#include "sim/stage_track.h"
#include "sim/test_zone.h"
#include "stage_driver.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// El tramo en SimWorld (tarea tramo-fisica, lote 4; mensajes/lote-4.md §2.5 y §2.6): el 205
// (tests/data/cars/reference.car) sobre el suelo del tramo. Criterios 3 y 5–8 del prompt y los
// regímenes del plan.

using rally::sim::BodyPose;
using rally::sim::BodyVelocity;
using rally::sim::kAxisMax;
using rally::sim::SimInput;
using rally::sim::SimWorld;
using rally::sim::StagePose;
using rally::sim::StageRunState;
using rally::sim::StageTrack;
using rally::sim::WorldDesc;

namespace {

// state_hash() tras la secuencia fija del test de reproducibilidad (criterio 6): el mismo en
// Debug y en Release. Cambia si cambia la física del tramo, el coche de referencia o lazadas.
constexpr std::uint64_t kExpectedStageStateHash = 0x6817'49b4'9626'5c69ull;

constexpr int kSteps = rally::sim::kStepsPerSecond;
constexpr double kGravity = 9.81;

StageTrack load(const std::string& text) {
    StageTrack track;
    const rally::sim::StageParseResult r = StageTrack::build(text, track);
    INFO(r.error);
    REQUIRE(r.ok);
    return track;
}

const StageTrack& lazadas() {
    static const StageTrack track = load(rally::tests::lazadas_stage_text());
    return track;
}

// Salida en (s, t) del tramo, con el rumbo del eje más `turn` (rad, + a la derecha) y apoyada en
// la calzada (o a la altura del borde si t cae fuera del núcleo).
rally::sim::CarSpawn spawn_at(const StageTrack& track, double s, double t, float speed,
                              double turn = 0.0) {
    rally::sim::CarSpawn spawn = rally::sim::stage_spawn(track, s, speed);
    const StagePose p = track.pose(s);
    const double edge =
        (t >= 0.0 ? p.half_width_right : p.half_width_left) + rally::sim::kStageShoulderWidth;
    const double along_bank = std::clamp(t, -edge, edge);
    spawn.x = p.x + t * std::cos(p.heading);
    spawn.z = p.z + t * std::sin(p.heading);
    spawn.y = p.y + p.bank * along_bank;
    spawn.yaw = static_cast<float>(-(p.heading + turn));
    return spawn;
}

WorldDesc stage_desc(const std::string& text, const rally::sim::CarSpawn& spawn) {
    WorldDesc desc;
    desc.stage_text = text;
    desc.car = true;
    desc.car_data = rally::tests::reference_car();
    desc.car_spawn = spawn;
    return desc;
}

double static_load() {
    return static_cast<double>(rally::tests::reference_car().mass) * kGravity / 4.0;
}

// Acelerador para sujetar `speed` m/s (sin freno ni dirección).
SimInput hold_speed(const BodyVelocity& v, double speed) {
    SimInput in;
    in.throttle = static_cast<std::int16_t>(
        std::clamp(0.5 * (speed - std::hypot(v.vx, v.vz)), 0.0, 1.0) * kAxisMax);
    return in;
}

// Lo que miden los criterios 3 y 4 en una pasada: el mayor salto de carga entre pasos, los
// picos de un paso, los pasos-rueda sin carga, cuántos se midieron y el episodio más largo de
// una rueda sin carga (segunda respuesta del consultor, §2).
struct LoadStats {
    double max_jump = 0.0;  // el mayor |L(n) − L(n−1)|
    double max_spike = 0.0; // el mayor |L(n) − 2·L(n−1) + L(n−2)| (diagnóstico de juntas)
    int unloaded = 0;       // pasos-rueda sin contacto o sin carga
    int wheel_steps = 0;    // pasos-rueda medidos
    int longest = 0;        // pasos seguidos de una rueda sin carga, el episodio más largo
    double max_impact = 0.0;

    void add(const LoadStats& o) {
        max_jump = std::max(max_jump, o.max_jump);
        max_spike = std::max(max_spike, o.max_spike);
        unloaded += o.unloaded;
        wheel_steps += o.wheel_steps;
        longest = std::max(longest, o.longest);
        max_impact = std::max(max_impact, o.max_impact);
    }
    double percent() const {
        return wheel_steps > 0 ? 100.0 * unloaded / static_cast<double>(wheel_steps) : 0.0;
    }
};

// Diagnóstico de una parada: cada rueda (carga, h, suelo bajo ella) en un paso con una rueda sin
// carga, como pide la segunda respuesta del consultor (§6). Hasta 12 líneas por pasada.
void print_wheels(const SimWorld& world, int step) {
    const BodyPose pose = world.car_pose();
    const rally::sim::CarTelemetry& t = world.telemetry();
    std::string line;
    for (int w = 0; w < rally::sim::kWheelCount; ++w) {
        const rally::sim::SimWorld::WheelPose wp = world.car_wheel(w);
        const double yaw = -rally::tests::car_heading(pose);
        const double wx = pose.x + wp.attach_x * std::cos(yaw) + wp.attach_z * std::sin(yaw);
        const double wz = pose.z - wp.attach_x * std::sin(yaw) + wp.attach_z * std::cos(yaw);
        const double t_wheel = world.stage() != nullptr ? world.stage()->acquire(wx, wz).t : 0.0;
        const rally::sim::GroundProbe g = world.probe_ground(wx, wz);
        const auto& wheel = t.wheels[static_cast<std::size_t>(w)];
        char buffer[200];
        std::snprintf(buffer, sizeof(buffer),
                      " | r%d t %.2f carga %.0f h %.1f mm suelo %.3f (sup %d)", w, t_wheel,
                      wheel.load, 1000.0 * wheel.h, g.y, static_cast<int>(g.surface));
        line += buffer;
    }
    MESSAGE("diag paso ", step, line);
}

template <typename Input>
LoadStats measure_loads(SimWorld& world, int settle, int steps, Input&& input) {
    world.set_telemetry(true);
    LoadStats stats;
    float previous[rally::sim::kWheelCount] = {};
    float before_previous[rally::sim::kWheelCount] = {};
    int run[rally::sim::kWheelCount] = {};
    int printed = 0;
    for (int i = 0; i < settle + steps; ++i) {
        world.step(input(world));
        const rally::sim::CarTelemetry& t = world.telemetry();
        if (i >= settle) {
            bool any = false;
            for (int w = 0; w < rally::sim::kWheelCount; ++w) {
                const auto& wheel = t.wheels[static_cast<std::size_t>(w)];
                const bool lost = wheel.contact == 0 || wheel.load <= 0.0f;
                any = any || lost;
                stats.unloaded += lost ? 1 : 0;
                ++stats.wheel_steps;
                run[w] = lost ? run[w] + 1 : 0;
                stats.longest = std::max(stats.longest, run[w]);
                if (i > settle + 1) {
                    const double jump = std::abs(static_cast<double>(wheel.load - previous[w]));
                    stats.max_jump = std::max(stats.max_jump, jump);
                    // Un pico de un paso (una junta) da una segunda diferencia grande; la
                    // oscilación del coche, no.
                    const double spike = std::abs(
                        static_cast<double>(wheel.load - 2.0f * previous[w] + before_previous[w]));
                    stats.max_spike = std::max(stats.max_spike, spike);
                }
            }
            stats.max_impact = std::max(stats.max_impact, static_cast<double>(t.impact));
            if (any && printed < 12 && world.stage() != nullptr) {
                ++printed;
                print_wheels(world, i);
            }
        }
        for (int w = 0; w < rally::sim::kWheelCount; ++w) {
            before_previous[w] = previous[w];
            previous[w] = t.wheels[static_cast<std::size_t>(w)].load;
        }
    }
    return stats;
}

// Losa plana de `surface` con su microrrelieve bajo la salida (la cara de arriba a su altura),
// de 400 × 400 m: la referencia de la recta del criterio 3 por el mismo camino en planta, así que
// la rueda nota la misma h que en el tramo y lo que cambia es solo la geometría.
WorldDesc slab_under(const rally::sim::CarSpawn& spawn,
                     rally::sim::Surface surface = rally::sim::Surface::Gravel) {
    rally::sim::BoxDesc box;
    box.x = spawn.x;
    box.z = spawn.z;
    box.y = spawn.y - 0.5;
    box.half_x = 200.0f;
    box.half_y = 0.5f;
    box.half_z = 200.0f;
    box.surface = surface;
    box.micro = true;
    WorldDesc desc;
    desc.bodies = {box};
    desc.car = true;
    desc.car_data = rally::tests::reference_car();
    desc.car_spawn = spawn;
    return desc;
}

// El mundo de lazadas con la salida `spawn`, el suelo `variant` y el microrrelieve o sin él.
WorldDesc
lazadas_desc(const rally::sim::CarSpawn& spawn,
             rally::sim::StageGroundVariant variant = rally::sim::StageGroundVariant::Design,
             bool micro = true) {
    WorldDesc desc = stage_desc(rally::tests::lazadas_stage_text(), spawn);
    desc.stage_variant = variant;
    desc.stage_micro_relief = micro;
    return desc;
}

// s de lazadas donde empieza la clotoide de entrada de la curva de R 100.
constexpr double kCurveStart = 857.6991118430775 + 50.0;

} // namespace

// --- Criterio 3: sin juntas bajo la calzada
// ---------------------------------------------------------

namespace {

// Velocidades y duración del criterio 3: en Release, 30, 60 y 90 km/h durante 3 s; en Debug, una
// versión corta (60 km/h, 2 s) que basta para Debug = Release y la ausencia de reservas.
#ifdef NDEBUG
constexpr double kCriterionSpeeds[] = {30.0, 60.0, 90.0};
constexpr int kCriterionSteps = 3 * kSteps;
#else
constexpr double kCriterionSpeeds[] = {60.0};
constexpr int kCriterionSteps = 2 * kSteps;
#endif

// La recta (desde s = −15, cruza la junta de s = 0, sin dirección ni freno) y la curva de R 100
// (tierra, peralte −3 %, con la dirección del conductor de test y sin freno) a `speed`.
LoadStats straight_run(const WorldDesc& desc, double speed) {
    SimWorld world;
    world.build(desc);
    world.seal();
    return measure_loads(world, kSteps / 2, kCriterionSteps,
                         [speed](SimWorld& w) { return hold_speed(w.car_velocity(), speed); });
}

LoadStats curve_run(const WorldDesc& desc, double speed, bool* on_road = nullptr) {
    SimWorld world;
    world.build(desc);
    world.seal();
    rally::tests::StageDriver driver(lazadas(), speed, 100.0);
    const LoadStats stats =
        measure_loads(world, kSteps / 2, kCriterionSteps, [speed, &driver](SimWorld& w) {
            SimInput in = driver.input(w.car_pose(), w.car_velocity());
            in.throttle = hold_speed(w.car_velocity(), speed).throttle;
            in.brake = 0;
            return in;
        });
    if (on_road != nullptr) {
        *on_road = world.stage_run() == nullptr || world.stage_run()->on_road;
    }
    return stats;
}

rally::sim::CarSpawn straight_spawn(double speed) {
    return spawn_at(lazadas(), -15.0, 0.0, static_cast<float>(speed));
}

rally::sim::CarSpawn curve_spawn(double speed) {
    return spawn_at(lazadas(), kCurveStart + 5.0, 0.0, static_cast<float>(speed));
}

} // namespace

TEST_CASE("stage_world: without micro relief no wheel loses load on the straight and the curve") {
    // Segunda respuesta del consultor, §2 (A): la geometría sola, en absoluto.
    const rally::sim::JoltRuntime runtime;
    for (const double kmh : kCriterionSpeeds) {
        const double speed = kmh / 3.6;
        const auto plain = rally::sim::StageGroundVariant::Design;
        const LoadStats straight =
            straight_run(lazadas_desc(straight_spawn(speed), plain, false), speed);
        bool on_road = false;
        const LoadStats curve =
            curve_run(lazadas_desc(curve_spawn(speed), plain, false), speed, &on_road);
        MESSAGE("sin microrrelieve a ", kmh, " km/h: pasos-rueda sin carga ", straight.unloaded,
                " en la recta y ", curve.unloaded, " en la curva; salto de carga ",
                straight.max_jump, " y ", curve.max_jump, " N");
        CHECK(straight.unloaded == 0);
        CHECK(curve.unloaded == 0);
        CHECK(on_road);
    }
}

#ifdef NDEBUG
TEST_CASE("stage_world: with micro relief the loads match their references on the straight and the "
          "curve") {
    // Segunda respuesta del consultor, §2 (B) y criterios 3 (b) y (c): con MicroRelief::Stage,
    // la recta frente a la losa de su superficie por el mismo camino y la curva frente al mismo
    // tramo con la malla 4 veces más densa (y la rejilla de 0,375 m). Las tres velocidades se
    // suman: pasos-rueda sin carga ≤ la referencia + 0,5 puntos y episodio más largo ≤ la
    // referencia + 12 pasos. Y el salto de carga de cada pasada ≤ la referencia + 1 % de la
    // carga estática.
    const rally::sim::JoltRuntime runtime;
    const double limit = 0.01 * static_load();
    LoadStats straight;
    LoadStats slab;
    LoadStats curve;
    LoadStats dense;
    for (const double kmh : kCriterionSpeeds) {
        const double speed = kmh / 3.6;
        const LoadStats s = straight_run(lazadas_desc(straight_spawn(speed)), speed);
        const LoadStats r = straight_run(slab_under(straight_spawn(speed)), speed);
        const LoadStats c = curve_run(lazadas_desc(curve_spawn(speed)), speed);
        const LoadStats d = curve_run(
            lazadas_desc(curve_spawn(speed), rally::sim::StageGroundVariant::DenseMesh), speed);
        MESSAGE(kmh, " km/h: salto de carga en la recta ", s.max_jump, " N (losa ", r.max_jump,
                " N), en la curva ", c.max_jump, " N (malla densa ", d.max_jump,
                " N); pasos-rueda sin carga: recta ", s.unloaded, " (losa ", r.unloaded,
                "), curva ", c.unloaded, " (malla densa ", d.unloaded,
                "); picos de un paso: recta ", s.max_spike, " N (losa ", r.max_spike, " N), curva ",
                c.max_spike, " N (malla densa ", d.max_spike, " N)");
        CHECK(s.max_jump <= r.max_jump + limit);
        CHECK(c.max_jump <= d.max_jump + limit);
        straight.add(s);
        slab.add(r);
        curve.add(c);
        dense.add(d);
    }
    MESSAGE("con microrrelieve, las tres velocidades: recta ", straight.percent(),
            " % de pasos-rueda sin carga y episodio de ", straight.longest, " pasos (losa ",
            slab.percent(), " % y ", slab.longest, "); curva ", curve.percent(), " % y ",
            curve.longest, " (malla densa ", dense.percent(), " % y ", dense.longest, ")");
    CHECK(straight.percent() <= slab.percent() + 0.5);
    CHECK(straight.longest <= slab.longest + 12);
    CHECK(curve.percent() <= dense.percent() + 0.5);
    CHECK(curve.longest <= dense.longest + 12);
}
#endif

TEST_CASE("stage_world: on a flat mesh the load jumps are those of a slab") {
    // Criterio 3 sin geometría: la recta de la horquilla llana (malla plana, sin peralte ni
    // pendiente) frente a la losa por el mismo camino.
    const rally::sim::JoltRuntime runtime;
    const std::string text = rally::tests::stage_file("hairpin_flat.stage");
    const StageTrack track = load(text);
    for (const double kmh : kCriterionSpeeds) {
        const double speed = kmh / 3.6;
        const rally::sim::CarSpawn spawn = spawn_at(track, 10.0, 0.0, static_cast<float>(speed));
        const LoadStats a = straight_run(stage_desc(text, spawn), speed);
        const LoadStats b = straight_run(slab_under(spawn), speed);
        MESSAGE("malla llana a ", kmh, " km/h: salto de carga ", a.max_jump, " N (losa ",
                b.max_jump, " N), pico de un paso ", a.max_spike, " N (losa ", b.max_spike,
                " N), pasos-rueda sin carga ", a.unloaded, " (losa ", b.unloaded, ")");
        CHECK(a.max_jump <= b.max_jump + 0.01 * static_load());
        CHECK(a.unloaded <= b.unloaded);
    }
}

// --- Criterio 4: salir del arcén y volver
// -----------------------------------------------------------

namespace {

struct EdgeRun {
    LoadStats loads;
    bool left_road = false;
    bool back_on_road = false;
};

// 3 s (2 s en Debug) a la velocidad de la salida, sin dirección, en `desc`.
EdgeRun drive_across(const WorldDesc& desc) {
    SimWorld world;
    world.build(desc);
    world.seal();
    EdgeRun run;
    const double speed = desc.car_spawn.speed;
    run.loads = measure_loads(world, kSteps / 4, kCriterionSteps - kSteps / 4, [&](SimWorld& w) {
        const bool on_road = w.stage_run()->on_road;
        run.left_road = run.left_road || !on_road;
        run.back_on_road = run.back_on_road || (run.left_road && on_road);
        return hold_speed(w.car_velocity(), speed);
    });
    const bool on_road = world.stage_run()->on_road;
    run.left_road = run.left_road || !on_road;
    run.back_on_road = run.back_on_road || (run.left_road && on_road);
    return run;
}

// Recta (s 20), 8° hacia fuera desde t = 1,5; curva de R 80 (peralte −5 %), recto desde
// t = −1,5, sale por fuera; de vuelta desde el terreno, 8° hacia dentro desde t = −7; y, solo en
// la parte (A) (tercera respuesta del consultor, §3), por el borde interior de la horquilla de
// R 6 (s 742, 8 % y −6 %) a 20 km/h, 60° hacia dentro desde t = 2,9. En Debug, la recta desde
// t = 2,5 y la horquilla (2 s).
constexpr const char* kEdgeNames[4] = {"recta", "curva", "vuelta", "horquilla"};
#ifdef NDEBUG
constexpr int kEdgeCases = 3;
constexpr int kGeometryCases[] = {0, 1, 2, 3};
constexpr double kStraightStart = 1.5;
#else
constexpr int kEdgeCases = 1;
constexpr int kGeometryCases[] = {0, 3};
constexpr double kStraightStart = 2.5;
#endif

rally::sim::CarSpawn edge_spawn(int c) {
    const StageTrack& track = lazadas();
    const float speed = static_cast<float>(30.0 / 3.6);
    constexpr double kDegree = 3.14159265358979323846 / 180.0;
    constexpr double kTurn = 8.0 * kDegree;
    if (c == 0) {
        return spawn_at(track, 20.0, kStraightStart, speed, kTurn);
    }
    if (c == 1) {
        return spawn_at(track, 150.0, -1.5, speed);
    }
    if (c == 3) {
        return spawn_at(track, 742.0, 2.9, static_cast<float>(20.0 / 3.6), 60.0 * kDegree);
    }
    return spawn_at(track, 20.0, -7.0, speed, kTurn);
}

} // namespace

TEST_CASE("stage_world: without micro relief leaving and coming back over the shoulder at 30 kmh") {
    // Segunda respuesta del consultor, §2 (A): la geometría del borde sola, en absoluto.
    const rally::sim::JoltRuntime runtime;
    for (const int c : kGeometryCases) {
        const EdgeRun run = drive_across(
            lazadas_desc(edge_spawn(c), rally::sim::StageGroundVariant::Design, false));
        MESSAGE("sin microrrelieve, ", std::string(kEdgeNames[c]), ": impacto máximo ",
                run.loads.max_impact, " m/s; pasos-rueda sin carga ", run.loads.unloaded);
        CHECK((c == 2 ? run.back_on_road : run.left_road));
        CHECK(run.loads.unloaded == 0);
        CHECK(run.loads.max_impact <= 1.0);
    }
}

#ifdef NDEBUG
TEST_CASE("stage_world: with micro relief the shoulder crossings match the dense reference") {
    // Segunda respuesta del consultor, §2 (B): el mismo cruce con MicroRelief::Stage frente a la
    // malla 4 veces más densa y la rejilla de 0,375 m; los tres casos se suman.
    const rally::sim::JoltRuntime runtime;
    LoadStats stage;
    LoadStats dense;
    for (int c = 0; c < kEdgeCases; ++c) {
        const EdgeRun run = drive_across(lazadas_desc(edge_spawn(c)));
        const EdgeRun ref =
            drive_across(lazadas_desc(edge_spawn(c), rally::sim::StageGroundVariant::DenseMesh));
        MESSAGE("con microrrelieve, ", std::string(kEdgeNames[c]), ": impacto máximo ",
                run.loads.max_impact, " m/s; pasos-rueda sin carga ", run.loads.unloaded,
                " (malla densa ", ref.loads.unloaded, "), episodio más largo ", run.loads.longest,
                " (", ref.loads.longest, ")");
        CHECK(run.loads.max_impact <= 1.0);
        stage.add(run.loads);
        dense.add(ref.loads);
    }
    MESSAGE("cruces del arcén con microrrelieve: ", stage.percent(),
            " % de pasos-rueda sin carga "
            "y episodio de ",
            stage.longest, " pasos (malla densa ", dense.percent(), " % y ", dense.longest, ")");
    CHECK(stage.percent() <= dense.percent() + 0.5);
    CHECK(stage.longest <= dense.longest + 12);

    // Escalón efectivo (geometría + microrrelieve mezclado del tramo) al cruzar las líneas
    // calzada–arcén y arcén–terreno de lazadas, de 30 cm dentro a 30 cm fuera: al informe.
    const StageTrack& track = lazadas();
    const rally::sim::TestZone noise;
    double road_shoulder = 0.0;
    double shoulder_terrain = 0.0;
    for (double s = 0.0; s <= track.axis().length(); s += 0.5) {
        const StagePose p = track.pose(s);
        for (const double side : {-1.0, 1.0}) {
            const double w = side > 0.0 ? p.half_width_right : p.half_width_left;
            const double e = w + rally::sim::kStageShoulderWidth;
            const auto h = [&](double t) {
                const double x = p.x + side * t * std::cos(p.heading);
                const double z = p.z + side * t * std::sin(p.heading);
                const rally::sim::StageReliefFrame f =
                    rally::sim::stage_relief_frame(track, s, x, z);
                return rally::sim::stage_relief_height(f, noise, f.s, f.t, x, z);
            };
            road_shoulder = std::max(road_shoulder, std::abs(h(w + 0.3) - h(w - 0.3)));
            shoulder_terrain = std::max(shoulder_terrain, std::abs(h(e + 0.3) - h(e - 0.3)));
        }
    }
    MESSAGE("diferencia de h de 30 cm dentro a 30 cm fuera: calzada–arcén ", 100.0 * road_shoulder,
            " cm, arcén–terreno ", 100.0 * shoulder_terrain, " cm");
}
#endif

// --- Criterio 5: seguimiento en el mundo
// ------------------------------------------------------------

TEST_CASE("stage_world: the reset button puts the car still on the road at the reset point") {
    const rally::sim::JoltRuntime runtime;
    const StageTrack& track = lazadas();
    SimWorld world;
    world.build(stage_desc(rally::tests::lazadas_stage_text(),
                           spawn_at(track, 50.0, 0.0, static_cast<float>(15.0))));
    world.seal();
    rally::tests::StageDriver driver(track, 15.0);
    int world_steps = 0;
    for (int i = 0; i < 2 * kSteps; ++i) {
        world.step(driver.input(world.car_pose(), world.car_velocity()));
        ++world_steps;
    }
    const StageRunState before = *world.stage_run();
    CHECK(before.on_road);
    CHECK(before.reset_s > 50.0);
    SimInput reset;
    reset.buttons = rally::sim::kButtonReset;
    world.step(reset);
    ++world_steps;
    const StageRunState& after = *world.stage_run();
    const rally::sim::CarSpawn target = rally::sim::stage_spawn(track, before.reset_s);
    const BodyPose pose = world.car_pose();
    const BodyVelocity v = world.car_velocity();
    CHECK(pose.x == target.x);
    CHECK(pose.y == target.y);
    CHECK(pose.z == target.z);
    CHECK(v.vx == 0.0f);
    CHECK(v.vy == 0.0f);
    CHECK(v.vz == 0.0f);
    CHECK(after.on_road);
    CHECK(after.s == before.reset_s);
    CHECK(after.t == 0.0);
    CHECK(after.resets == 1);
    // Respuesta del consultor §3: el paso del botón también cuenta.
    CHECK(after.steps == world_steps);
    // Sigue con la siguiente pulsación sin abortar y quieto apoyado.
    for (int i = 0; i < kSteps; ++i) {
        world.step(SimInput{});
    }
    CHECK(world.stage_run()->on_road);
}

TEST_CASE("stage_world: a shortcut driven over the terrain puts the car back at exit_s") {
    const rally::sim::JoltRuntime runtime;
    const std::string text = rally::tests::stage_file("hairpin_flat.stage");
    const StageTrack track = load(text);
    SimWorld world;
#ifdef NDEBUG
    // Por el brazo de ida desde s 140, girando al este en s 160.
    world.build(stage_desc(text, spawn_at(track, 140.0, 0.0, static_cast<float>(30.0 / 3.6))));
#else
    // Versión corta: sale ya fuera del arcén en s 165, mirando al este, hacia el otro brazo.
    world.build(stage_desc(text, spawn_at(track, 165.0, 5.0, static_cast<float>(30.0 / 3.6),
                                          0.5 * 3.14159265358979323846)));
#endif
    world.seal();
    world.set_telemetry(true);
    constexpr double kEast = 0.5 * 3.14159265358979323846;
    bool shortcut = false;
    double exit_s = 0.0;
    double max_impact = 0.0;
    int unloaded = 0;
    std::int64_t world_steps = 0;
    for (int i = 0; i < 20 * kSteps && !shortcut; ++i) {
        const BodyPose pose = world.car_pose();
        // Por el brazo de ida hasta s ≈ 160 y después al este, por el terreno, al otro brazo.
        const double s = track.acquire(pose.x, pose.z).s;
        const double want = s < 160.0 && world.stage_run()->on_road ? 0.0 : kEast;
        SimInput in = hold_speed(world.car_velocity(), 30.0 / 3.6);
        in.steer = static_cast<std::int16_t>(
            std::clamp(2.0 * rally::tests::wrap_angle(want - rally::tests::car_heading(pose)), -1.0,
                       1.0) *
            kAxisMax);
        const StageRunState before = *world.stage_run();
        world.step(in);
        ++world_steps;
        const StageRunState& after = *world.stage_run();
        if (after.shortcuts > before.shortcuts) {
            shortcut = true;
            exit_s = after.s;
            CHECK(after.s == before.exit_s);
            const rally::sim::CarSpawn target = rally::sim::stage_spawn(track, before.exit_s);
            const BodyPose placed = world.car_pose();
            CHECK(placed.x == target.x);
            CHECK(placed.z == target.z);
            CHECK(world.car_velocity().vx == 0.0f);
            CHECK(after.on_road);
        } else if (i > kSteps / 2) {
            max_impact = std::max(max_impact, static_cast<double>(world.telemetry().impact));
            for (const auto& wheel : world.telemetry().wheels) {
                unloaded += wheel.contact == 0 ? 1 : 0;
            }
        }
    }
    MESSAGE("atajo: recoloca en s = ", exit_s, "; impacto máximo por el terreno ", max_impact,
            " m/s");
    REQUIRE(shortcut);
    CHECK(world.stage_run()->shortcuts == 1);
    // El paso del atajo también cuenta (consultor §3).
    CHECK(world.stage_run()->steps == world_steps);
    CHECK(max_impact <= 1.0);
    CHECK(unloaded == 0);
}

namespace {

// Lo que da el conductor de test por un tramo: la pasada y, contados por el test, los pasos del
// mundo y el paso en que aparecieron la salida, el primer parcial y la meta.
struct Drive {
    StageRunState run{};
    std::int64_t world_steps = 0;
    std::int64_t seen_start = -1;
    std::int64_t seen_split = -1;
    std::int64_t seen_finish = -1;
    bool always_on_road = true;
};

// El conductor de test desde la salida, parado, hasta la meta o `seconds`; pulsa recolocar en
// los pasos del mundo de `resets`.
Drive drive_stage(const std::string& text, double speed, double lateral, int seconds,
                  const std::vector<int>& resets = {}) {
    const StageTrack track = load(text);
    SimWorld world;
    world.build(stage_desc(text, spawn_at(track, 0.0, 0.0, 0.0f)));
    world.seal();
    rally::tests::StageDriver driver(track, speed, lateral);
    Drive out;
    for (int i = 0; i < seconds * kSteps; ++i) {
        SimInput in = driver.input(world.car_pose(), world.car_velocity());
        if (std::find(resets.begin(), resets.end(), i) != resets.end()) {
            in.buttons = rally::sim::kButtonReset;
        }
        world.step(in);
        ++out.world_steps;
        const StageRunState& run = *world.stage_run();
        if (out.seen_start < 0 && run.start_step >= 0) {
            out.seen_start = out.world_steps;
        }
        if (out.seen_split < 0 && run.split_count > 0) {
            out.seen_split = out.world_steps;
        }
        if (out.seen_finish < 0 && run.finish_step >= 0) {
            out.seen_finish = out.world_steps;
        }
        if (out.always_on_road && !run.on_road) {
            MESSAGE("el conductor sale de la carretera en s = ", run.s, ", t = ", run.t);
        }
        out.always_on_road = out.always_on_road && run.on_road;
        if (run.finish_step >= 0) {
            break;
        }
    }
    out.run = *world.stage_run();
    return out;
}

} // namespace

#ifdef NDEBUG
TEST_CASE("stage_world: the test driver completes lazadas and a stage with a split") {
    const rally::sim::JoltRuntime runtime;
    const Drive lazadas_drive =
        drive_stage(rally::tests::lazadas_stage_text(), 60.0 / 3.6, 2.5, 200);
    const StageRunState& lazadas_run = lazadas_drive.run;
    MESSAGE("lazadas: salida en el paso ", lazadas_run.start_step, ", meta en el ",
            lazadas_run.finish_step, " (",
            (lazadas_run.finish_step - lazadas_run.start_step) / static_cast<double>(kSteps),
            " s)");
    CHECK(lazadas_drive.always_on_road);
    CHECK(lazadas_run.start_step > 0);
    CHECK(lazadas_run.finish_step > lazadas_run.start_step);
    CHECK(lazadas_run.resets == 0);
    CHECK(lazadas_run.shortcuts == 0);

    // Con dos recolocaciones con el botón por el camino: `steps` son los pasos del mundo y la
    // salida, el parcial y la meta salen en el paso del mundo en que pasan (consultor §3).
    const Drive splits =
        drive_stage(rally::tests::stage_file("splits.stage"), 90.0 / 3.6, 3.0, 200, {3000, 9000});
    const StageRunState& run = splits.run;
    MESSAGE("parciales: salida ", run.start_step, ", parcial ", run.split_steps[0], ", meta ",
            run.finish_step, "; ", run.resets, " recolocaciones");
    CHECK(splits.always_on_road);
    CHECK(run.resets == 2);
    CHECK(run.split_count == 1);
    CHECK(run.steps == splits.world_steps);
    CHECK(run.start_step == splits.seen_start);
    CHECK(run.split_steps[0] == splits.seen_split);
    CHECK(run.finish_step == splits.seen_finish);
}
#endif

TEST_CASE("stage_world: the run counts every world step with button resets and a shortcut") {
    // Versión corta (también en Debug): 2 s por la horquilla llana con dos botones; después, el
    // atajo del test del atajo. `steps` = pasos del mundo.
    const rally::sim::JoltRuntime runtime;
    const Drive drive =
        drive_stage(rally::tests::stage_file("hairpin_flat.stage"), 10.0, 3.0, 2, {100, 300});
    CHECK(drive.run.resets == 2);
    CHECK(drive.run.steps == drive.world_steps);
    CHECK(drive.run.start_step == drive.seen_start);
}

// --- Criterio 6: reproducibilidad ---------------------------------------------------------------

namespace {

struct Recorded {
    std::vector<SimInput> inputs;
    std::uint64_t hash = 0;
};

// 1500 pasos del conductor por lazadas con un botón de recolocar en el 900.
Recorded record_run() {
    const StageTrack& track = lazadas();
    SimWorld world;
    world.build(stage_desc(rally::tests::lazadas_stage_text(), spawn_at(track, 0.0, 0.0, 0.0f)));
    world.seal();
    rally::tests::StageDriver driver(track, 70.0 / 3.6);
    Recorded out;
    for (int i = 0; i < 1500; ++i) {
        SimInput in = driver.input(world.car_pose(), world.car_velocity());
        if (i == 900) {
            in.buttons = rally::sim::kButtonReset;
        }
        out.inputs.push_back(in);
        world.step(in);
    }
    out.hash = world.state_hash();
    return out;
}

std::uint64_t replay_run(const std::vector<SimInput>& inputs) {
    SimWorld world;
    world.build(
        stage_desc(rally::tests::lazadas_stage_text(), spawn_at(lazadas(), 0.0, 0.0, 0.0f)));
    world.seal();
    for (const SimInput& in : inputs) {
        world.step(in);
    }
    return world.state_hash();
}

std::uint64_t run_hash(const StageRunState& state) {
    rally::sim::Fnv1a64 hash;
    rally::sim::hash_stage_run(hash, state);
    return hash.value();
}

} // namespace

TEST_CASE("stage_world: the same inputs on the stage give the same state hash twice") {
    const rally::sim::JoltRuntime runtime;
    const Recorded first = record_run();
    const std::uint64_t second = replay_run(first.inputs);
    CHECK(first.hash == second);
    if (first.hash != kExpectedStageStateHash) {
        char text[32];
        std::snprintf(text, sizeof(text), "0x%016llx", static_cast<unsigned long long>(first.hash));
        MESSAGE("state_hash en el tramo: ", text, " (kExpectedStageStateHash)");
    }
    CHECK(first.hash == kExpectedStageStateHash);
}

TEST_CASE("stage_world: each field of the run state changes its hash") {
    // Un estado con todos los campos distintos de cero.
    StageRunState base;
    std::memset(&base, 0, sizeof(base));
    base.s = 1.5;
    base.t = -0.5;
    base.reset_s = 2.5;
    base.exit_s = 3.5;
    base.off_travel = 4.5;
    base.last_x = 5.5;
    base.last_z = 6.5;
    base.steps = 7;
    base.start_step = 8;
    base.finish_step = 9;
    base.resets = 10;
    base.shortcuts = 11;
    base.split_count = 12;
    base.on_road = true;
    for (int k = 0; k < rally::sim::kMaxSplits; ++k) {
        base.split_steps[k] = 100 + k;
    }
    const std::uint64_t h = run_hash(base);
    const auto changed = [h](StageRunState s) { return run_hash(s) != h; };
    StageRunState x = base;
    x.s += 1.0;
    CHECK(changed(x));
    x = base;
    x.t += 1.0;
    CHECK(changed(x));
    x = base;
    x.reset_s += 1.0;
    CHECK(changed(x));
    x = base;
    x.exit_s += 1.0;
    CHECK(changed(x));
    x = base;
    x.off_travel += 1.0;
    CHECK(changed(x));
    x = base;
    x.last_x += 1.0;
    CHECK(changed(x));
    x = base;
    x.last_z += 1.0;
    CHECK(changed(x));
    x = base;
    x.steps += 1;
    CHECK(changed(x));
    x = base;
    x.start_step += 1;
    CHECK(changed(x));
    x = base;
    x.finish_step += 1;
    CHECK(changed(x));
    x = base;
    x.resets += 1;
    CHECK(changed(x));
    x = base;
    x.shortcuts += 1;
    CHECK(changed(x));
    x = base;
    x.split_count += 1;
    CHECK(changed(x));
    x = base;
    x.on_road = false;
    CHECK(changed(x));
    x = base;
    x.split_steps[0] += 1;
    CHECK(changed(x));
    x = base;
    x.split_steps[rally::sim::kMaxSplits - 1] += 1;
    CHECK(changed(x));
    // El relleno tras on_road no entra.
    x = base;
    std::memset(reinterpret_cast<unsigned char*>(&x.on_road) + 1, 0xab, 3);
    CHECK(run_hash(x) == h);
}

TEST_CASE("stage_world: state_hash includes the run state that restore_state does not restore") {
    const rally::sim::JoltRuntime runtime;
    const WorldDesc desc =
        stage_desc(rally::tests::lazadas_stage_text(), spawn_at(lazadas(), 0.0, 0.0, 0.0f));
    SimWorld world;
    world.build(desc);
    world.seal();
    rally::tests::StageDriver driver(lazadas(), 15.0);
    for (int i = 0; i < 600; ++i) {
        world.step(driver.input(world.car_pose(), world.car_velocity()));
    }
    SimWorld restored;
    restored.build(desc);
    restored.seal();
    restored.restore_state(world.save_state(), world.controls());
    CHECK(restored.save_state() == world.save_state());
    CHECK(restored.stage_run()->steps == 0);
    CHECK(restored.state_hash() != world.state_hash());
}

// --- Criterios 7 y 8: reservas y coste
// ---------------------------------------------------------------

TEST_CASE("stage_world: a step on the stage allocates nothing of ours and Jolt as in the zone") {
    const rally::sim::JoltRuntime runtime;
    const StageTrack& track = lazadas();
    SimWorld world;
    world.build(stage_desc(rally::tests::lazadas_stage_text(),
                           spawn_at(track, 100.0, 0.0, static_cast<float>(20.0))));
    world.seal();
    rally::tests::StageDriver driver(track, 20.0);
    // Las entradas, antes: el conductor reserva (pose del tramo no, pero sí el vector).
    std::vector<SimInput> inputs;
    inputs.reserve(2010);
    for (int i = 0; i < 10; ++i) {
        const SimInput in = driver.input(world.car_pose(), world.car_velocity());
        world.step(in);
    }
    const rally::tests::JoltAllocationCounter jolt;
    constexpr int kCount = 2000;
    int steps_other = 0;
    std::uint64_t ours = 0;
    std::vector<double> ms;
    ms.reserve(kCount);
    for (int i = 0; i < kCount; ++i) {
        const SimInput in = driver.input(world.car_pose(), world.car_velocity());
        const int before = jolt.count();
        const std::uint64_t ours_before = rally::tests::global_new_count();
        const auto t0 = std::chrono::steady_clock::now();
        world.step(in);
        const auto t1 = std::chrono::steady_clock::now();
        ours += rally::tests::global_new_count() - ours_before;
        const int n = jolt.count() - before;
        steps_other += n != 0 && n != 1 ? 1 : 0;
        ms.push_back(std::chrono::duration<double, std::milli>(t1 - t0).count());
    }
    double mean = 0.0;
    for (const double m : ms) {
        mean += m;
    }
    mean /= kCount;
    MESSAGE("en el tramo: reservas de Jolt en ", kCount, " pasos: ", jolt.count(), "; paso medio ",
            mean, " ms");
    CHECK(ours == 0);
    CHECK(steps_other == 0);
    CHECK(jolt.count() <= kCount);
#ifdef NDEBUG
    // Holgado: las cifras de verdad las da rally_stage_measure (presupuesto 0,06 ms).
    CHECK(mean <= 0.5);
#endif
}

// --- Regímenes
// --------------------------------------------------------------------------------------

namespace {

// 2 s con el freno pisado tras 0,5 s: desplazamiento en planta y ruedas sin carga. Devuelve la
// carga media de la rueda menos cargada (N).
double check_parked(SimWorld& world, const std::string& where, bool four_wheels = true) {
    world.set_telemetry(true);
    SimInput brake;
    brake.brake = kAxisMax;
    for (int i = 0; i < kSteps / 2; ++i) {
        world.step(brake);
    }
    const BodyPose start = world.car_pose();
    int unloaded = 0;
    double load_sum[rally::sim::kWheelCount] = {};
    for (int i = 0; i < 2 * kSteps; ++i) {
        world.step(brake);
        for (int w = 0; w < rally::sim::kWheelCount; ++w) {
            const auto& wheel = world.telemetry().wheels[static_cast<std::size_t>(w)];
            unloaded += wheel.contact == 0 || wheel.load <= 0.0f ? 1 : 0;
            load_sum[w] += wheel.load;
        }
    }
    const BodyPose end = world.car_pose();
    const double moved = std::hypot(end.x - start.x, end.z - start.z);
    double lightest = 1e9;
    for (const double sum : load_sum) {
        lightest = std::min(lightest, sum / (2.0 * kSteps));
    }
    MESSAGE(where, ": se mueve ", 1000.0 * moved, " mm en 2 s; la rueda menos cargada, ", lightest,
            " N de media; pasos-rueda sin carga ", unloaded);
    CHECK(moved < 0.01);
    if (four_wheels) {
        CHECK(unloaded == 0);
    }
    return lightest;
}

} // namespace

TEST_CASE(
    "stage_world: parked on the road without bank and on the 8 percent with the 6 percent bank") {
    const rally::sim::JoltRuntime runtime;
    const StageTrack& track = lazadas();
    {
        SimWorld world;
        world.build(
            stage_desc(rally::tests::lazadas_stage_text(), spawn_at(track, 10.0, 0.0, 0.0f)));
        world.seal();
        check_parked(world, "s 10, sin peralte");
    }
    {
        // s 440: peralte del 4 % y pendiente del −6 %, en la curva de R 40 (casi un plano).
        SimWorld world;
        world.build(
            stage_desc(rally::tests::lazadas_stage_text(), spawn_at(track, 440.0, 0.0, 0.0f)));
        world.seal();
        check_parked(world, "s 440, −6 % y 4 %, al construir");
        SimInput reset;
        reset.buttons = rally::sim::kButtonReset;
        world.step(reset);
        CHECK(std::abs(world.stage_run()->s - 440.0) < 0.01);
        check_parked(world, "s 440, tras recolocar");
    }
    {
        // s 744: el 8 % con el −6 % solo está en el arco de R 6 de la horquilla, que no es un
        // plano: la pendiente del borde interior (r 5,3 m) es del 9,1 % y la del exterior
        // (r 6,7 m), del 7,2 %; en la batalla del 205 (2,54 m), 4,9 cm de alabeo. Se pide que
        // no se mueva; la carga de cada rueda va al informe.
        SimWorld world;
        world.build(
            stage_desc(rally::tests::lazadas_stage_text(), spawn_at(track, 744.0, 0.0, 0.0f)));
        world.seal();
        check_parked(world, "s 744, 8 % y −6 % en la horquilla, al construir", false);
        SimInput reset;
        reset.buttons = rally::sim::kButtonReset;
        world.step(reset);
        CHECK(std::abs(world.stage_run()->s - 744.0) < 0.01);
        check_parked(world, "s 744, tras recolocar", false);
    }
}

TEST_CASE("stage_world: starting from rest the car moves off with every wheel loaded") {
    const rally::sim::JoltRuntime runtime;
    SimWorld world;
    world.build(
        stage_desc(rally::tests::lazadas_stage_text(), spawn_at(lazadas(), 10.0, 0.0, 0.0f)));
    world.seal();
    world.set_telemetry(true);
    SimInput in;
    in.throttle = kAxisMax;
    int unloaded = 0;
    for (int i = 0; i < 3 * kSteps; ++i) {
        world.step(in);
        if (i > kSteps / 4) {
            for (const auto& wheel : world.telemetry().wheels) {
                unloaded += wheel.contact == 0 ? 1 : 0;
            }
        }
    }
    const BodyVelocity v = world.car_velocity();
    MESSAGE("arranque: ", 3.6 * std::hypot(v.vx, v.vz), " km/h en 3 s");
    CHECK(std::hypot(v.vx, v.vz) > 5.0);
    CHECK(unloaded == 0);
    CHECK(world.stage_run()->start_step > 0);
}

TEST_CASE("stage_world: dropped 0.5 m over the crest the car lands on the road") {
    const rally::sim::JoltRuntime runtime;
    const StageTrack& track = lazadas();
    rally::sim::CarSpawn spawn = spawn_at(track, 300.0, 0.0, static_cast<float>(60.0 / 3.6));
    spawn.y += 0.5;
    SimWorld world;
    world.build(stage_desc(rally::tests::lazadas_stage_text(), spawn));
    world.seal();
    world.set_telemetry(true);
    rally::tests::StageDriver driver(track, 60.0 / 3.6);
    int airborne = 0;
    double max_impact = 0.0;
    double lowest = 1e9;
    for (int i = 0; i < 2 * kSteps; ++i) {
        world.step(driver.input(world.car_pose(), world.car_velocity()));
        const rally::sim::CarTelemetry& t = world.telemetry();
        int contacts = 0;
        for (const auto& wheel : t.wheels) {
            contacts += wheel.contact;
        }
        airborne += contacts == 0 ? 1 : 0;
        max_impact = std::max(max_impact, static_cast<double>(t.impact));
        const BodyPose pose = world.car_pose();
        const double road = track.pose(track.acquire(pose.x, pose.z).s).y;
        lowest = std::min(lowest, pose.y - road);
    }
    MESSAGE("vuelo: ", airborne, " pasos en el aire, impacto máximo ", max_impact,
            " m/s, el origen del coche baja hasta ", lowest, " m respecto al eje");
    CHECK(airborne > 0);
    CHECK(std::isfinite(max_impact));
    // La caja del coche (a ground_clearance del origen) nunca baja de la calzada.
    CHECK(lowest > -static_cast<double>(rally::tests::reference_car().ground_clearance));
    CHECK(world.stage_run()->on_road);
}

TEST_CASE("stage_world: outside the strip there is no ground and the car falls") {
    const rally::sim::JoltRuntime runtime;
    SimWorld world;
    world.build(
        stage_desc(rally::tests::lazadas_stage_text(), spawn_at(lazadas(), 100.0, 70.0, 0.0f)));
    world.seal();
    world.set_telemetry(true);
    const double y0 = world.car_pose().y;
    int contacts = 0;
    for (int i = 0; i < 2 * kSteps; ++i) {
        world.step(SimInput{});
        for (const auto& wheel : world.telemetry().wheels) {
            contacts += wheel.contact;
        }
    }
    const double fallen = y0 - world.car_pose().y;
    MESSAGE("fuera de la franja: cae ", fallen, " m en 2 s");
    CHECK(contacts == 0);
    CHECK(fallen > 15.0);
    CHECK_FALSE(world.stage_run()->on_road);
}

TEST_CASE("stage_world: the extensions carry the car before the start and past the finish") {
    const rally::sim::JoltRuntime runtime;
    const StageTrack& track = lazadas();
    {
        // Desde la prolongación de entrada hacia la salida.
        SimWorld world;
        world.build(stage_desc(rally::tests::lazadas_stage_text(),
                               spawn_at(track, -15.0, 0.0, static_cast<float>(10.0))));
        world.seal();
        for (int i = 0; i < 2 * kSteps; ++i) {
            world.step(hold_speed(world.car_velocity(), 10.0));
        }
        CHECK(world.stage_run()->on_road);
        CHECK(world.stage_run()->start_step > 0);
        CHECK(world.stage_run()->s > 0.0);
    }
    {
        // Pasada la meta, hasta salir de la prolongación de frenada.
        SimWorld world;
        world.build(stage_desc(
            rally::tests::lazadas_stage_text(),
            spawn_at(track, track.axis().length() + 100.0, 0.0, static_cast<float>(15.0))));
        world.seal();
        world.set_telemetry(true);
        int contacts = 0;
        for (int i = 0; i < 5 * kSteps; ++i) {
            world.step(hold_speed(world.car_velocity(), 15.0));
            for (const auto& wheel : world.telemetry().wheels) {
                contacts += wheel.contact;
            }
        }
        const StageRunState& run = *world.stage_run();
        const BodyPose pose = world.car_pose();
        MESSAGE("pasada la prolongación: s = ", run.s, ", a ",
                track.acquire(pose.x, pose.z).distance, " m del final del eje");
        CHECK(run.s == track.s_end());
        CHECK_FALSE(run.on_road);
        CHECK(run.finish_step >= 0);
        CHECK(std::isfinite(pose.y));
        CHECK(contacts > 0);
    }
}
