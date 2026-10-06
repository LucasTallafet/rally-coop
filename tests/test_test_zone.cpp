#include "car_worlds.h"
#include "io/recording.h"
#include "io/replay.h"
#include "jolt_allocation_counter.h"
#include "new_counter.h"
#include "sim/demo_world.h"
#include "sim/jolt_runtime.h"
#include "sim/lap_timer.h"
#include "sim/pcg32.h"
#include "sim/sim_input.h"
#include "sim/sim_world.h"
#include "sim/test_zone.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

namespace {

using rally::sim::AreaId;
using rally::sim::AxisPoint;
using rally::sim::kAxisMax;
using rally::sim::LapTimer;
using rally::sim::SimInput;
using rally::sim::SimWorld;
using rally::sim::Surface;
using rally::sim::TestZone;
using rally::sim::WorldDesc;
using rally::sim::ZoneArea;
namespace zone = rally::sim::zone;

constexpr double kPi = 3.14159265358979323846;

// Hash del suelo de la zona (alturas y superficies que entran en Jolt) con la pista ciega de
// grava. Tiene que ser el mismo en Debug y en Release. Si un cambio intencionado de la zona
// lo altera, se copia aquí el valor nuevo y se dice en el mensaje del commit.
constexpr std::uint64_t kExpectedGroundHash = 0x2c688c9b1672ee18ull;

// Reservas de Jolt 5.6.0 por paso sobre el suelo de la zona: la misma reconstrucción de la
// broad phase que en el recinto (tests/test_sim_world.cpp), pero con un solo cuerpo en la
// capa móvil QuadTree::UpdatePrepare hace su new[] de 4 B y BuildTree no reserva nada. 0 si
// la caja no cambia de caja envolvente. Con la caja acelerando, una por paso.
constexpr int kZoneJoltAllocationsPerStep = 1;
constexpr int kZoneCountedSteps = 2000;
constexpr int kExpectedZoneJoltAllocations = 2000;

constexpr std::array<Surface, rally::sim::kSurfaceCount> kSurfaces = {
    Surface::Asphalt, Surface::Gravel, Surface::Dirt, Surface::Snow, Surface::Sand};

// Mundo de la zona con el coche de referencia en (x, z), con rumbo `heading` (0 hacia -z,
// positivo a la derecha) y `speed` m/s hacia delante.
WorldDesc zone_with_car_at(const TestZone& zone, double x, double z, double heading = 0.0,
                           double speed = 0.0) {
    WorldDesc desc = rally::sim::zone_world(zone.blind_surface(), rally::tests::reference_car());
    desc.car_spawn.x = x;
    desc.car_spawn.y = zone.ground_height(x, z);
    desc.car_spawn.z = z;
    desc.car_spawn.yaw = static_cast<float>(-heading);
    desc.car_spawn.speed = static_cast<float>(speed);
    return desc;
}

// Un punto dentro de cada área, lejos de sus bordes. El arcén de la carretera va bajo la
// calzada: su punto está en el arcén, a un lado de ella.
void inside_point(const ZoneArea& a, double& x, double& z) {
    if (a.road) {
        const AxisPoint p = TestZone::axis_point(a, 0.5 * TestZone::axis_length(a));
        const double t =
            a.id == AreaId::RoadShoulder ? zone::kRoadHalfWidth + 0.5 * zone::kShoulderWidth : 0.0;
        x = p.x + t * std::cos(p.psi);
        z = p.z + t * std::sin(p.psi);
    } else {
        x = a.cx;
        z = a.cz;
    }
}

double random_in(rally::sim::Pcg32& rng, double lo, double hi) {
    return lo + (hi - lo) * static_cast<double>(rng.next_float01());
}

// Rumbo del coche (0 hacia -z, positivo a la derecha) a partir de su orientación.
double car_heading(const rally::sim::BodyPose& p) {
    const double fx = -2.0 * (p.qx * p.qz + p.qw * p.qy);
    const double fz = -(1.0 - 2.0 * (p.qx * p.qx + p.qy * p.qy));
    return std::atan2(fx, -fz);
}

double wrap(double a) {
    while (a > kPi) {
        a -= 2.0 * kPi;
    }
    while (a < -kPi) {
        a += 2.0 * kPi;
    }
    return a;
}

} // namespace

TEST_CASE("test_zone: the same description gives the same ground hash twice") {
    const rally::sim::JoltRuntime runtime;
    using Clock = std::chrono::steady_clock;
    const Clock::time_point start = Clock::now();
    SimWorld a;
    a.build(rally::sim::zone_world(Surface::Gravel, rally::tests::reference_car()));
    a.seal();
    const double build_ms = std::chrono::duration<double, std::milli>(Clock::now() - start).count();
    SimWorld b;
    b.build(rally::sim::zone_world(Surface::Gravel, rally::tests::reference_car()));
    b.seal();
    MESSAGE("suelo: ", a.ground_bytes() / 1024, " KiB; build() + seal(): ", build_ms, " ms");
    char hashes[100];
    std::snprintf(hashes, sizeof(hashes), "hash del suelo 0x%016llx; hash de la zona 0x%016llx",
                  static_cast<unsigned long long>(a.ground_hash()),
                  static_cast<unsigned long long>(a.zone_hash()));
    MESSAGE(hashes);
    CHECK(a.zone_hash() == b.zone_hash());
    char message[200];
    std::snprintf(message, sizeof(message),
                  "hash del suelo 0x%016llx, esperado 0x%016llx. Si el cambio de la zona es "
                  "intencionado, copia el actual en kExpectedGroundHash "
                  "(tests/test_test_zone.cpp).",
                  static_cast<unsigned long long>(a.ground_hash()),
                  static_cast<unsigned long long>(kExpectedGroundHash));
    INFO(message);
    CHECK(a.ground_hash() == b.ground_hash());
    CHECK(a.ground_hash() == kExpectedGroundHash);
    CHECK(a.state_hash() == b.state_hash());
}

TEST_CASE("test_zone: a point inside each area gives its surface and the background is dirt") {
    const rally::sim::JoltRuntime runtime;
    SimWorld world;
    world.build(rally::sim::zone_world(Surface::Snow, rally::tests::reference_car()));
    world.seal();
    const TestZone& zone = *world.zone();
    for (const ZoneArea& a : zone.areas()) {
        double x = 0.0;
        double z = 0.0;
        inside_point(a, x, z);
        INFO("área ", std::string(a.key), " en (", x, ", ", z, ")");
        CHECK(zone.surface_at(x, z) == a.surface);
        CHECK(zone.area_at(x, z) == static_cast<int>(a.id));
        // La misma superficie en el material de Jolt bajo el punto.
        const rally::sim::GroundProbe probe = world.probe_ground(x, z);
        REQUIRE(probe.hit);
        CHECK(probe.surface == a.surface);
    }
    CHECK(zone.area(AreaId::BlindTrack).surface == Surface::Snow);
    // Fondo: entre áreas y en las esquinas del suelo.
    const double background[][2] = {
        {-20.0, 300.0}, {20.0, 0.0}, {-700.0, -700.0}, {700.0, 700.0}, {300.0, -600.0}};
    for (const auto& p : background) {
        INFO("fondo en (", p[0], ", ", p[1], ")");
        CHECK(zone.surface_at(p[0], p[1]) == Surface::Dirt);
        CHECK(zone.area_at(p[0], p[1]) == -1);
        const rally::sim::GroundProbe probe = world.probe_ground(p[0], p[1]);
        REQUIRE(probe.hit);
        CHECK(probe.surface == Surface::Dirt);
    }
}

TEST_CASE("test_zone: areas are at least 30 m apart") {
    const TestZone zone;
    // Distancia entre áreas por muestreo: puntos del contorno de una contra la distancia con
    // signo de la otra. Las tres de la recta (recta, baches de asfalto y de grava) se tocan, y
    // la calzada de la carretera va sobre su arcén.
    const auto touching = [](AreaId a, AreaId b) {
        const auto straight = [](AreaId id) {
            return id == AreaId::Straight || id == AreaId::BumpsAsphalt ||
                   id == AreaId::BumpsGravel;
        };
        const auto road = [](AreaId id) {
            return id == AreaId::Road || id == AreaId::RoadShoulder;
        };
        return (straight(a) && straight(b)) || (road(a) && road(b));
    };
    double closest = 1e9;
    for (const ZoneArea& a : zone.areas()) {
        for (const ZoneArea& b : zone.areas()) {
            if (a.id == b.id || touching(a.id, b.id)) {
                continue;
            }
            // Contorno de a muestreado cada 0,5 m sobre su caja (rectángulos) o su eje.
            std::vector<std::array<double, 2>> outline;
            if (a.road) {
                const double length = TestZone::axis_length(a);
                for (double s = 0.0; s <= length; s += 0.5) {
                    const AxisPoint p = TestZone::axis_point(a, s);
                    outline.push_back({p.x + a.half_width * std::cos(p.psi),
                                       p.z + a.half_width * std::sin(p.psi)});
                    outline.push_back({p.x - a.half_width * std::cos(p.psi),
                                       p.z - a.half_width * std::sin(p.psi)});
                }
            } else {
                for (double t = 0.0; t <= 1.0; t += 0.0025) {
                    const double x = a.min_x + t * (a.max_x - a.min_x);
                    const double z = a.min_z + t * (a.max_z - a.min_z);
                    outline.push_back({x, a.min_z});
                    outline.push_back({x, a.max_z});
                    outline.push_back({a.min_x, z});
                    outline.push_back({a.max_x, z});
                }
            }
            for (const auto& p : outline) {
                const double d = TestZone::signed_distance(b, p[0], p[1]);
                if (d < closest) {
                    closest = d;
                }
                if (d < 30.0) {
                    INFO(std::string(a.key), " y ", std::string(b.key), " a ", d, " m en (", p[0],
                         ", ", p[1], ")");
                    CHECK(d >= 30.0);
                    break;
                }
            }
        }
    }
    MESSAGE("separación mínima entre áreas: ", closest, " m");
    // Al menos 30 m de tierra entre cualquier área y la cara interior de los muros.
    const double usable = zone::kUsableHalfSize;
    double closest_to_wall = 1e9;
    for (const ZoneArea& a : zone.areas()) {
        INFO(std::string(a.key));
        CHECK(a.min_x > -usable + 30.0);
        CHECK(a.max_x < usable - 30.0);
        CHECK(a.min_z > -usable + 30.0);
        CHECK(a.max_z < usable - 30.0);
        closest_to_wall = std::min({closest_to_wall, a.min_x + usable, usable - a.max_x,
                                    a.min_z + usable, usable - a.max_z});
    }
    MESSAGE("zona útil: ", 2.0 * usable, " × ", 2.0 * usable, " m; área más cercana a un muro: a ",
            closest_to_wall, " m");
    CHECK(closest_to_wall >= 30.0);
}

TEST_CASE("test_zone: the blind trials start on the blind track, the rest on the straight") {
    const TestZone zone;
    const WorldDesc normal = rally::sim::zone_world(Surface::Snow, rally::tests::reference_car());
    const WorldDesc blind = rally::sim::zone_world(Surface::Snow, rally::tests::reference_car(),
                                                   rally::sim::ZoneStart::BlindTrack);
    const rally::sim::CarSpawn& a = normal.car_spawn;
    const rally::sim::CarSpawn& b = blind.car_spawn;
    CHECK(zone.area_at(a.x, a.z) == static_cast<int>(AreaId::Straight));
    CHECK(zone.area_at(b.x, b.z) == static_cast<int>(AreaId::BlindTrack));
    CHECK(b.x == zone.blind_start().x);
    CHECK(b.z == zone.blind_start().z);
    CHECK(std::string(zone.blind_start().key) == "blind_track");
    // Los dos puntos miran al norte y el coche sale con su orientación.
    CHECK(zone.start().yaw == 0.0);
    CHECK(zone.blind_start().yaw == 0.0);
    CHECK(a.yaw == 0.0f);
    CHECK(b.yaw == 0.0f);
    CHECK(a.y == zone.ground_height(a.x, a.z));
    CHECK(blind.blind_surface == Surface::Snow);
}

TEST_CASE("test_zone: the ground is flat to the edge and four 4 m walls close the usable zone") {
    const rally::sim::JoltRuntime runtime;
    SimWorld world;
    world.build(rally::sim::zone_world(Surface::Gravel, rally::tests::reference_car()));
    world.seal();
    const TestZone& zone = *world.zone();
    const double edge = -zone::kGroundOrigin;
    const double usable = zone::kUsableHalfSize;
    CHECK(2.0 * usable == doctest::Approx(1515.25));
    // Sin talud: llano hasta el borde, también en las esquinas y en el suelo de Jolt.
    for (const double sign : {-1.0, 1.0}) {
        CHECK(zone.ground_height(sign * (edge - 9.0), 100.0) == 0.0);
        CHECK(zone.ground_height(100.0, sign * (edge - 0.1)) == 0.0);
        CHECK(zone.ground_height(sign * (edge - 0.1), sign * (edge - 0.1)) == 0.0);
        const rally::sim::GroundProbe probe = world.probe_ground(sign * (edge - 4.0), 0.0);
        REQUIRE(probe.hit);
        CHECK(std::abs(probe.y) < 0.01);
    }
    // Norte, sur, oeste y este: 4 m de alto apoyados en el suelo, 1 m de grueso, la cara
    // interior en el límite de la zona útil. Norte y sur llegan a la cara exterior del este y
    // el oeste: las esquinas quedan cerradas.
    const auto& walls = zone.walls();
    for (const rally::sim::WallBox& w : walls) {
        CHECK(w.cy - w.half_y == 0.0);
        CHECK(2.0 * w.half_y == zone::kWallHeight);
        CHECK(2.0 * std::min(w.half_x, w.half_z) == zone::kWallThickness);
    }
    CHECK(walls[0].cz + walls[0].half_z == -usable);
    CHECK(walls[1].cz - walls[1].half_z == usable);
    CHECK(walls[2].cx + walls[2].half_x == -usable);
    CHECK(walls[3].cx - walls[3].half_x == usable);
    for (const int i : {0, 1}) {
        CHECK(walls[i].cx - walls[i].half_x == walls[2].cx - walls[2].half_x);
        CHECK(walls[i].cx + walls[i].half_x == walls[3].cx + walls[3].half_x);
    }
    for (const int i : {2, 3}) {
        CHECK(walls[i].cz - walls[i].half_z <= walls[0].cz + walls[0].half_z);
        CHECK(walls[i].cz + walls[i].half_z >= walls[1].cz - walls[1].half_z);
    }
    // Dentro del suelo de Jolt.
    for (const rally::sim::WallBox& w : walls) {
        CHECK(std::max(std::abs(w.cx) + w.half_x, std::abs(w.cz) + w.half_z) < edge);
    }
}

namespace {

// Lanza el coche de la zona desde (x, z) con rumbo `heading` hacia el muro que tiene delante,
// a `speed` m/s y sosteniéndola con el acelerador hasta el choque, y sigue 3 s más. Devuelve
// la velocidad horizontal del paso anterior al choque, la mayor distancia del centro del coche
// al centro de la zona (max(|x|, |z|)), la mayor altura y la distancia al final.
struct WallHit {
    double speed = 0.0;
    double max_extent = 0.0;
    double max_y = 0.0;
    double final_extent = 0.0; // max(|x|, |z|) al final
};

WallHit launch_at_wall(double x, double z, double heading, double speed) {
    const TestZone zone;
    // 2.6: mide el muro, no el coche. El coche ya no llega a 80 m/s (5ª al limitador: ~58 m/s):
    // sale en N, sin aire ni rodadura, y llega al muro a la velocidad de salida.
    WorldDesc desc = rally::tests::in_neutral(zone_with_car_at(zone, x, z, heading, speed));
    desc.car_data.cda = 0.0f;
    for (rally::sim::TireSurface& tire : desc.car_data.tires) {
        tire.rolling_resistance = 0.0f;
    }
    SimWorld world;
    world.build(desc);
    world.seal();
    WallHit hit;
    double previous_speed = 0.0;
    int after_hit = -1;
    for (int i = 0; i < 60 * rally::sim::kStepsPerSecond && after_hit < 3 * 240; ++i) {
        const rally::sim::BodyVelocity v = world.car_velocity();
        const double now = std::hypot(v.vx, v.vz);
        SimInput in;
        if (after_hit < 0) {
            in.throttle = static_cast<std::int16_t>(
                std::clamp(2.0 * (speed + 0.5 - now), 0.0, 1.0) * kAxisMax);
        }
        world.step(in);
        const rally::sim::BodyPose after = world.car_pose();
        hit.max_extent = std::max({hit.max_extent, std::abs(after.x), std::abs(after.z)});
        hit.max_y = std::max(hit.max_y, after.y);
        if (after_hit < 0 && world.last_impact() > 5.0f) {
            after_hit = 0;
            hit.speed = previous_speed;
        } else if (after_hit >= 0) {
            ++after_hit;
        }
        previous_speed = now;
    }
    const rally::sim::BodyPose end = world.car_pose();
    hit.final_extent = std::max(std::abs(end.x), std::abs(end.z));
    return hit;
}

} // namespace

TEST_CASE("test_zone: the car launched at 30 and 80 m/s against each wall stays inside the zone") {
    const rally::sim::JoltRuntime runtime;
    const double usable = zone::kUsableHalfSize;
    const double half_length = 0.5 * rally::tests::reference_car().box_length;
    struct Run {
        std::string wall;
        double x;
        double z;
        double heading;
    };
    // 30 m/s es lo que pide la prueba; 80 m/s, lo que alcanzó la caja de Lucas en la zona y
    // la velocidad punta teórica del coche provisional. Salida a 100 m del muro, lejos de las
    // áreas (todas a menos de 650 m del centro) y de las esquinas.
    for (const double speed : {30.0, 80.0}) {
        const double start = usable - 100.0;
        const Run runs[] = {{"norte", 700.0, -start, 0.0},
                            {"sur", -700.0, start, kPi},
                            {"oeste", -start, -700.0, -0.5 * kPi},
                            {"este", start, 700.0, 0.5 * kPi}};
        for (const Run& run : runs) {
            const WallHit hit = launch_at_wall(run.x, run.z, run.heading, speed);
            // El morro llega a max_extent + media longitud. Lo que pase de la cara del muro es
            // la penetración de un paso, que Jolt deshace.
            const double penetration = hit.max_extent + half_length - usable;
            MESSAGE("muro ", run.wall, ": choque a ", hit.speed, " m/s; penetración máxima ",
                    100.0 * penetration, " cm; altura máxima del coche ", hit.max_y,
                    " m; al final a ", usable - hit.final_extent, " m del muro");
            INFO("muro ", run.wall, " a ", speed, " m/s");
            CHECK(hit.speed >= speed - 1.0);
            CHECK(penetration < speed * rally::sim::kStepSeconds);
            CHECK(hit.max_y < zone::kWallHeight);
            // Al final, dentro de la zona: el centro, a más de media anchura del muro.
            CHECK(hit.final_extent < usable - 0.5 * rally::tests::reference_car().box_width);
        }
    }
}

TEST_CASE("test_zone: reset from 10 far points goes to the nearest respawn, still and aligned") {
    const rally::sim::JoltRuntime runtime;
    const TestZone zone;
    const double far_points[][2] = {
        // 2.6b: (-300, 0) pasa a (-500, 600): la carretera de prueba queda a 32 m de él.
        {700.0, 700.0}, {-700.0, 700.0},  {700.0, -700.0}, {-700.0, -700.0}, {-500.0, 600.0},
        {300.0, 100.0}, {-250.0, -400.0}, {500.0, -300.0}, {-300.0, -740.0}, {250.0, 650.0}};
    SimInput reset;
    reset.buttons = rally::sim::kButtonReset;
    for (const auto& p : far_points) {
        // El más cercano, buscado aquí por fuerza bruta.
        int expected = 0;
        double best = 1e18;
        for (std::size_t i = 0; i < zone.respawns().size(); ++i) {
            const double d = std::hypot(zone.respawns()[i].x - p[0], zone.respawns()[i].z - p[1]);
            if (d < best) {
                best = d;
                expected = static_cast<int>(i);
            }
        }
        const rally::sim::RespawnPoint& r = zone.respawns()[static_cast<std::size_t>(expected)];
        INFO("desde (", p[0], ", ", p[1], ") hasta ", std::string(r.key), " a ", best, " m");
        CHECK(best > 100.0);

        SimWorld world;
        world.build(zone_with_car_at(zone, p[0], p[1]));
        world.seal();
        world.step(SimInput{});
        world.step(reset);
        const rally::sim::BodyPose pose = world.car_pose();
        CHECK(pose.x == doctest::Approx(r.x).epsilon(1e-12));
        CHECK(pose.z == doctest::Approx(r.z).epsilon(1e-12));
        CHECK(pose.y == doctest::Approx(zone.ground_height(r.x, r.z)).epsilon(1e-9));
        CHECK(std::abs(wrap(car_heading(pose) + r.yaw)) < 1e-6);
        const rally::sim::BodyVelocity v = world.car_velocity();
        CHECK(v.vx == 0.0f);
        CHECK(v.vy == 0.0f);
        CHECK(v.vz == 0.0f);
        CHECK(v.wy == 0.0f);
        // Y se queda ahí: apoyado en el suelo de Jolt, sin hundirse ni caer.
        for (int i = 0; i < 240; ++i) {
            world.step(SimInput{});
        }
        const rally::sim::BodyPose later = world.car_pose();
        CHECK(std::hypot(later.x - r.x, later.z - r.z) < 0.01);
        CHECK(std::abs(later.y - pose.y) < 0.02);
    }
}

TEST_CASE("lap_timer: forward crossings time a lap, backward ones and resets do not") {
    rally::sim::FinishLine line;
    line.x = 0.0;
    line.z = 0.0;
    line.forward_x = 0.0;
    line.forward_z = -1.0;
    line.half_length = 4.0;
    // Un paso que cruza la línea hacia delante (de z = 0,1 a z = -0,1) o hacia atrás.
    const auto cross = [](LapTimer& t, bool forward, double x = 0.0) {
        t.update(x, forward ? 0.1 : -0.1, x, forward ? -0.1 : 0.1, false);
    };
    const auto idle = [](LapTimer& t, int steps) {
        for (int i = 0; i < steps; ++i) {
            t.update(0.0, 5.0, 0.0, 5.0, false);
        }
    };

    SUBCASE("two forward crossings give a lap with the steps between them") {
        LapTimer t(line);
        idle(t, 10);
        cross(t, true);
        CHECK(t.state().lap_start == 11);
        CHECK(t.state().laps == 0);
        idle(t, 99);
        cross(t, true);
        CHECK(t.state().laps == 1);
        CHECK(t.state().last_lap == 100);
        CHECK(t.state().best_lap == 100);
        idle(t, 49);
        cross(t, true);
        CHECK(t.state().laps == 2);
        CHECK(t.state().last_lap == 50);
        CHECK(t.state().best_lap == 50);
    }
    SUBCASE("a backward crossing does not count, nor does the forward one that undoes it") {
        LapTimer t(line);
        cross(t, true);
        idle(t, 20);
        cross(t, false);
        CHECK(t.state().debt == 1);
        idle(t, 5);
        cross(t, true);
        CHECK(t.state().debt == 0);
        CHECK(t.state().laps == 0);
        CHECK(t.state().lap_start == 1);
        idle(t, 10);
        cross(t, true);
        CHECK(t.state().laps == 1);
        CHECK(t.state().last_lap == 38);
    }
    SUBCASE("only backward crossings never start a lap") {
        LapTimer t(line);
        cross(t, false);
        cross(t, false);
        CHECK(t.state().lap_start == -1);
        CHECK(t.state().laps == 0);
    }
    SUBCASE("a crossing beside the line does not count") {
        LapTimer t(line);
        cross(t, true, 4.5);
        CHECK(t.state().lap_start == -1);
        cross(t, true, -3.9);
        CHECK(t.state().lap_start == 2);
    }
    SUBCASE("reset cancels the lap in progress and the debt") {
        LapTimer t(line);
        cross(t, true);
        idle(t, 30);
        cross(t, false);
        t.update(0.0, 5.0, 100.0, 100.0, true);
        CHECK(t.state().lap_start == -1);
        CHECK(t.state().debt == 0);
        cross(t, true);
        CHECK(t.state().laps == 0);
        CHECK(t.state().lap_start == t.state().steps);
    }
}

namespace {

// Conductor automático del coche por el eje de la pista ciega: persigue un punto 8 m por
// delante y ajusta la velocidad a la curva que viene (con poca aceleración lateral, ~1,3 m/s²,
// para no derrapar en ninguna superficie). La entrada cambia cada 4 pasos, como en el bucle a
// 60 fps.
class BlindDriver {
public:
    explicit BlindDriver(const ZoneArea& track) : track_(track) {
        const double length = TestZone::axis_length(track);
        for (double s = 0.0; s < length; s += 0.5) {
            points_.push_back(TestZone::axis_point(track, s));
        }
    }

    SimInput input(const rally::sim::BodyPose& pose, const rally::sim::BodyVelocity& v) {
        // Punto más cercano del eje, buscado cerca del anterior.
        const std::size_t n = points_.size();
        std::size_t best = index_;
        double best_d = 1e18;
        for (std::size_t k = 0; k < 80; ++k) {
            const std::size_t i = (index_ + k) % n;
            const double d = std::hypot(points_[i].x - pose.x, points_[i].z - pose.z);
            if (d < best_d) {
                best_d = d;
                best = i;
            }
        }
        index_ = best;
        const AxisPoint& target = points_[(best + 16) % n];
        const double want = std::atan2(target.x - pose.x, -(target.z - pose.z));
        const double error = wrap(want - car_heading(pose));
        // Curva más cerrada en los 25 m siguientes, por el cambio de rumbo del eje.
        double max_curvature = 0.0;
        for (std::size_t k = 0; k < 50; ++k) {
            const AxisPoint& a = points_[(best + k) % n];
            const AxisPoint& b = points_[(best + k + 1) % n];
            max_curvature = std::max(max_curvature, std::abs(wrap(b.psi - a.psi)) / 0.5);
        }
        const double radius = max_curvature > 1e-6 ? 1.0 / max_curvature : 1e6;
        const double target_speed = std::min(11.0, std::sqrt(1.3 * radius));
        const double speed = std::hypot(v.vx, v.vz);
        SimInput in;
        in.steer = static_cast<std::int16_t>(std::clamp(3.0 * error, -1.0, 1.0) * kAxisMax);
        in.throttle = static_cast<std::int16_t>(std::clamp(0.6 * (target_speed - speed), 0.0, 1.0) *
                                                kAxisMax);
        in.brake = static_cast<std::int16_t>(std::clamp(0.6 * (speed - target_speed), 0.0, 1.0) *
                                             kAxisMax);
        return in;
    }

private:
    const ZoneArea& track_;
    std::vector<AxisPoint> points_;
    std::size_t index_ = 0;
};

// Graba `seconds` segundos del conductor automático desde la salida de la pista ciega.
rally::io::Recording drive_blind_lap(int seconds, std::int64_t& laps) {
    WorldDesc desc = rally::sim::zone_world(Surface::Gravel, rally::tests::reference_car(),
                                            rally::sim::ZoneStart::BlindTrack);
    SimWorld world;
    world.build(desc);
    world.seal();
    BlindDriver driver(world.zone()->area(AreaId::BlindTrack));
    rally::io::RecordingHeader header;
    header.commit = "test";
    header.world = rally::io::serialize_world(desc);
    header.car_text = rally::tests::reference_car_text();
    header.zone_hash = world.zone_hash();
    rally::io::Recorder recorder;
    const int steps = seconds * rally::sim::kStepsPerSecond;
    recorder.begin(std::move(header), static_cast<std::size_t>(steps));
    SimInput in;
    for (int i = 0; i < steps; ++i) {
        if (i % 4 == 0) {
            in = driver.input(world.car_pose(), world.car_velocity());
        }
        world.step(in);
        recorder.append(in);
    }
    laps = world.lap()->laps;
    return recorder.finish(world.state_hash(), rally::io::timed_steps(world),
                           rally::io::road_result(world));
}

} // namespace

TEST_CASE("test_zone: a recorded lap of the blind track is timed and replays the same") {
    const rally::sim::JoltRuntime runtime;
    std::int64_t laps = 0;
    const rally::io::Recording recording = drive_blind_lap(150, laps);
    MESSAGE("vueltas: ", laps, "; mejor: ", recording.footer.timed_steps, " pasos (",
            static_cast<double>(recording.footer.timed_steps) / rally::sim::kStepsPerSecond, " s)");
    REQUIRE(laps >= 1);
    CHECK(recording.footer.timed_steps > 0);

    // La repetición da el mismo tiempo y el mismo hash.
    const rally::io::ParseResult parsed = rally::io::parse(rally::io::serialize(recording));
    REQUIRE(parsed.ok);
    const rally::io::ReplayResult replayed = rally::io::replay(parsed.recording);
    CHECK(replayed.hash_ok);
    CHECK(replayed.timed_steps == recording.footer.timed_steps);

    // La grabación guardada para rally_replay (tests/data/recordings) es esta misma. Con
    // RALLY_WRITE_TEST_RECORDINGS=1 se escribe de nuevo (tras un cambio intencionado).
    const char* path = RALLY_TEST_DATA_DIR "/recordings/zone_lap.rrec";
    std::string bytes;
    std::string error;
    char* write_flag = nullptr;
    std::size_t write_flag_size = 0;
    const bool rewrite =
        _dupenv_s(&write_flag, &write_flag_size, "RALLY_WRITE_TEST_RECORDINGS") == 0 &&
        write_flag != nullptr;
    std::free(write_flag);
    if (rewrite) {
        REQUIRE(rally::io::write_file(path, rally::io::serialize(recording), error));
    }
    REQUIRE(rally::io::read_file(path, bytes, error));
    const rally::io::ParseResult stored = rally::io::parse(bytes);
    REQUIRE(stored.ok);
    CHECK(stored.recording.header.zone_hash == recording.header.zone_hash);
    CHECK(stored.recording.footer.final_hash == recording.footer.final_hash);
    CHECK(stored.recording.footer.timed_steps == recording.footer.timed_steps);

    // La misma grabación con otro hash de la zona: se repite y avisa, sin comparar el hash
    // final. La guardada (zone_lap_other_zone.rrec) la usa el test de rally_replay.
    rally::io::Recording other = recording;
    other.header.zone_hash ^= 1u;
    const rally::io::ReplayResult changed = rally::io::replay(other);
    CHECK(changed.ran);
    CHECK(changed.zone_mismatch);
    CHECK_FALSE(changed.hash_ok);
    CHECK(changed.problem.find("la zona ha cambiado") != std::string::npos);
    const char* other_path = RALLY_TEST_DATA_DIR "/recordings/zone_lap_other_zone.rrec";
    if (rewrite) {
        REQUIRE(rally::io::write_file(other_path, rally::io::serialize(other), error));
    }
    REQUIRE(rally::io::read_file(other_path, bytes, error));
    const rally::io::ParseResult stored_other = rally::io::parse(bytes);
    REQUIRE(stored_other.ok);
    CHECK(stored_other.recording.header.zone_hash == other.header.zone_hash);
}

TEST_CASE("test_zone: crossing the line backwards or resetting during a lap gives no time") {
    const rally::sim::JoltRuntime runtime;
    const TestZone zone;
    const rally::sim::FinishLine& line = zone.finish_line();
    SimInput forward;
    forward.throttle = kAxisMax / 2;
    SimInput backward = forward; // en R, el acelerador lleva el coche hacia atrás
    SimInput reset;
    reset.buttons = rally::sim::kButtonReset;
    // Cambia de marcha con dos pulsaciones (parado, regla D4): bajar dos veces da R; subir dos,
    // 1ª. Cada cambio dura shift_time (tarea 2.6): se espera a que termine.
    const auto shift = [](SimWorld& world, std::uint16_t button) {
        for (int i = 0; i < 2; ++i) {
            SimInput press;
            press.buttons = button;
            world.step(press);
            for (int k = 0; k < world.car_controller().car().shift_steps; ++k) {
                world.step(SimInput{});
            }
        }
    };

    SUBCASE("backwards over the line and forwards again") {
        SimWorld world;
        world.build(zone_with_car_at(zone, line.x, line.z - 5.0)); // 5 m pasada la línea
        world.seal();
        shift(world, rally::sim::kButtonShiftDown);
        REQUIRE(world.car_controller().gear() == rally::sim::kReverseGear);
        for (int i = 0; i < 3 * rally::sim::kStepsPerSecond; ++i) {
            world.step(backward);
        }
        SimInput stop;
        stop.brake = kAxisMax;
        for (int i = 0; i < 3 * rally::sim::kStepsPerSecond; ++i) {
            world.step(stop);
        }
        CHECK(world.car_pose().z > line.z);
        CHECK(world.lap()->debt == 1);
        CHECK(world.lap()->lap_start == -1);
        shift(world, rally::sim::kButtonShiftUp);
        REQUIRE(world.car_controller().gear() == 1);
        for (int i = 0; i < 4 * rally::sim::kStepsPerSecond; ++i) {
            world.step(forward);
        }
        CHECK(world.car_pose().z < line.z);
        CHECK(world.lap()->debt == 0);
        CHECK(world.lap()->lap_start == -1);
        CHECK(world.lap()->laps == 0);
    }
    SUBCASE("reset during a lap cancels it") {
        SimWorld world;
        world.build(zone_with_car_at(zone, line.x, line.z + 5.0));
        world.seal();
        // 2.6: con medio acelerador y el embrague real, 3 s para pasar la línea (antes, 2 s).
        for (int i = 0; i < 3 * rally::sim::kStepsPerSecond; ++i) {
            world.step(forward);
        }
        REQUIRE(world.lap()->lap_start > 0);
        world.step(reset);
        CHECK(world.lap()->lap_start == -1);
        // Desde la recolocación (50 m antes de la meta) la siguiente pasada empieza otra
        // vuelta, no termina ninguna (2.6: con medio acelerador y el embrague real, 8 s; antes,
        // 6 s).
        for (int i = 0; i < 8 * rally::sim::kStepsPerSecond; ++i) {
            world.step(forward);
        }
        CHECK(world.lap()->lap_start > 0);
        CHECK(world.lap()->laps == 0);
        CHECK(world.lap()->last_lap == -1);
    }
}

TEST_CASE("test_zone: the blind track takes each surface and nothing else changes") {
    const TestZone reference(Surface::Gravel);
    std::vector<float> ref_heights;
    std::vector<std::uint8_t> ref_surfaces;
    reference.fill_ground(ref_heights, ref_surfaces);
    const int cells = zone::kGroundSamples - 1;
    for (const Surface surface : kSurfaces) {
        INFO("pista ciega de ", std::string(rally::sim::surface_name(surface)));
        const TestZone zone(surface);
        std::vector<float> heights;
        std::vector<std::uint8_t> surfaces;
        zone.fill_ground(heights, surfaces);
        CHECK(heights == ref_heights);
        int blind_cells = 0;
        int wrong = 0;
        for (int iz = 0; iz < cells; ++iz) {
            const double z = zone::kGroundOrigin + (iz + 0.5) * zone::kGroundSpacing;
            for (int ix = 0; ix < cells; ++ix) {
                const std::size_t i = static_cast<std::size_t>(iz) * cells + ix;
                const double x = zone::kGroundOrigin + (ix + 0.5) * zone::kGroundSpacing;
                const bool blind = zone.area_at(x, z) == static_cast<int>(AreaId::BlindTrack);
                blind_cells += blind ? 1 : 0;
                const std::uint8_t want =
                    blind ? static_cast<std::uint8_t>(surface) : ref_surfaces[i];
                wrong += surfaces[i] != want ? 1 : 0;
            }
        }
        CHECK(wrong == 0);
        // 785 m × 7 m de calzada en celdas de 0,75 × 0,75 m: unas 9800.
        CHECK(blind_cells > 9000);
        CHECK(blind_cells < 10500);
    }
}

TEST_CASE("test_zone: the blind series has 15 trials, 3 of each surface, same order per seed") {
    const auto a = rally::sim::blind_series(42);
    const auto b = rally::sim::blind_series(42);
    const auto c = rally::sim::blind_series(43);
    CHECK(a == b);
    CHECK(a != c);
    CHECK(a.size() == 15);
    for (const Surface surface : kSurfaces) {
        CHECK(std::count(a.begin(), a.end(), surface) == 3);
        CHECK(std::count(c.begin(), c.end(), surface) == 3);
    }
}

TEST_CASE("test_zone: step allocates nothing of ours on the zone ground") {
    const rally::sim::JoltRuntime runtime;
    SimWorld world;
    world.build(rally::sim::zone_world(Surface::Gravel, rally::tests::reference_car()));
    world.seal();
    // Con la captura de la telemetría (superficie bajo el centro de masas, cronómetro) tampoco.
    SUBCASE("without telemetry") {}
    SUBCASE("with telemetry") { world.set_telemetry(true); }
    SimInput in;
    in.throttle = kAxisMax;
    in.steer = kAxisMax / 3;
    for (int i = 0; i < 10; ++i) {
        world.step(in);
    }
    const rally::tests::JoltAllocationCounter jolt;
    int steps_other = 0;
    const std::uint64_t ours_before = rally::tests::global_new_count();
    for (int i = 0; i < kZoneCountedSteps; ++i) {
        const int before = jolt.count();
        world.step(in);
        const int step_count = jolt.count() - before;
        steps_other += step_count != 0 && step_count != kZoneJoltAllocationsPerStep ? 1 : 0;
    }
    const std::uint64_t ours = rally::tests::global_new_count() - ours_before;
    MESSAGE("reservas de Jolt en ", kZoneCountedSteps,
            " pasos sobre el suelo de la zona: ", jolt.count());
    CHECK(ours == 0);
    INFO("Si un cambio intencionado altera las reservas de Jolt, actualiza "
         "kExpectedZoneJoltAllocations (tests/test_test_zone.cpp) y dilo en el commit.");
    CHECK(steps_other == 0);
    CHECK(jolt.count() == kExpectedZoneJoltAllocations);
}
