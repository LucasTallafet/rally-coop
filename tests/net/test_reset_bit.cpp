// El contador de recolocaciones de Host::after_steps llega por ENet tal como se pidió. Un cliente
// ENet crudo guarda los estados decodificados (el Copilot solo deja pasar la pose).

#include "car_worlds.h"
#include "net/car_state.h"
#include "net/host.h"
#include "net/protocol.h"
#include "pump.h"
#include "sim/jolt_runtime.h"
#include "sim/sim_world.h"
#include "stage_net.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <enet/enet.h>
#include <memory>
#include <span>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace rally::net;
using rally::tests::wall_seconds;

class RawClient {
public:
    explicit RawClient(std::uint16_t port) {
        ENetAddress local{};
        enet_address_set_host_ip(&local, "127.0.0.1");
        local.port = 0;
        host_ = enet_host_create(&local, 1, 2, 0, 0);
        REQUIRE(host_ != nullptr);
        ENetAddress server{};
        enet_address_set_host_ip(&server, "127.0.0.1");
        server.port = port;
        peer_ = enet_host_connect(host_, &server, 2, 0);
        REQUIRE(peer_ != nullptr);
    }
    ~RawClient() {
        enet_peer_disconnect_now(peer_, 0);
        enet_host_flush(host_);
        enet_host_destroy(host_);
    }
    RawClient(const RawClient&) = delete;
    RawClient& operator=(const RawClient&) = delete;

    void service() {
        ENetEvent event;
        while (enet_host_service(host_, &event, 0) > 0) {
            if (event.type == ENET_EVENT_TYPE_CONNECT) {
                const std::string hello = encode_hello(kProtocolVersion);
                enet_peer_send(
                    peer_, 0,
                    enet_packet_create(hello.data(), hello.size(), ENET_PACKET_FLAG_RELIABLE));
            } else if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                CarState state;
                if (event.channelID == 1 &&
                    decode(
                        std::span<const std::uint8_t>(event.packet->data, event.packet->dataLength),
                        state)) {
                    states.push_back(state);
                }
                enet_packet_destroy(event.packet);
            }
        }
    }

    std::vector<CarState> states;

private:
    ENetHost* host_ = nullptr;
    ENetPeer* peer_ = nullptr;
};

} // namespace

TEST_CASE("net_reset: the recolocated bit of after_steps reaches the client") {
    const rally::sim::JoltRuntime runtime;
    rally::sim::SimWorld world;
    world.build(rally::tests::flat_world(rally::sim::Surface::Asphalt));
    world.seal();
    world.set_telemetry(true);
    world.step(rally::sim::SimInput{});

    Host host;
    std::string error;
    REQUIRE_MESSAGE(host.start(0, error), error);
    host.set_world("mundo", "coche", 1);
    RawClient client(host.port());
    const auto wait_for = [&](std::size_t count) {
        const double end = wall_seconds() + 5.0;
        while (wall_seconds() < end && client.states.size() < count) {
            host.service();
            client.service();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return client.states.size() >= count;
    };
    {
        const double end = wall_seconds() + 5.0;
        while (wall_seconds() < end && !host.copilot_connected()) {
            host.service();
            client.service();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }
    REQUIRE(host.copilot_connected());

    SUBCASE("reset_since_last = true adds 1 and the counter stays in the next states") {
        host.after_steps(world, 8, false);
        host.after_steps(world, 16, true);
        host.after_steps(world, 24, false);
        host.after_steps(world, 32, false);
        REQUIRE(wait_for(4));
        CHECK(client.states[0].reset_count == 0);
        CHECK(client.states[1].step == 16);
        CHECK(client.states[1].reset_count == 1);
        CHECK(client.states[2].reset_count == 1);
        CHECK(client.states[3].reset_count == 1);
    }

    SUBCASE("two resets in steps skipped by the cadence count 2") {
        host.after_steps(world, 8, false); // índice 1: se envía
        REQUIRE(wait_for(1));
        host.after_steps(world, 12, true); // mismo índice: no se envía, pero cuenta
        host.after_steps(world, 15, true);
        host.after_steps(world, 16, false); // índice 2: lleva 2
        REQUIRE(wait_for(2));
        host.after_steps(world, 24, false);
        REQUIRE(wait_for(3));
        CHECK(client.states[0].reset_count == 0);
        CHECK(client.states[1].step == 16);
        CHECK(client.states[1].reset_count == 2);
        CHECK(client.states[2].reset_count == 2);
    }

    SUBCASE("the counter wraps from 255 to 0") {
        for (int i = 0; i < 255; ++i) {
            host.after_steps(world, 4, true); // índice 0: solo la primera vez envía
        }
        host.after_steps(world, 8, false);
        host.after_steps(world, 12, true); // la 256: vuelve a 0
        host.after_steps(world, 16, false);
        REQUIRE(wait_for(3));
        CHECK(client.states[0].reset_count == 1);
        CHECK(client.states[1].reset_count == 255);
        CHECK(client.states[2].reset_count == 0);
    }

    SUBCASE("a new world starts the counter at 0") {
        host.after_steps(world, 8, true);
        REQUIRE(wait_for(1));
        CHECK(client.states[0].reset_count == 1);
        host.set_world("mundo2", "coche", 1);
        host.after_steps(world, 8, false);
        const double end = wall_seconds() + 5.0;
        while (wall_seconds() < end && client.states.size() < 2) {
            host.service();
            client.service();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        REQUIRE(client.states.size() >= 2);
        CHECK(client.states[1].reset_count == 0);
        CHECK(client.states[1].generation != client.states[0].generation);
    }
}

namespace {

// Atajo de hairpin_flat (la versión corta de test_stage_world): sale fuera del arcén en s 165,
// t 5, mirando al este, hacia el otro brazo, y atraviesa el terreno hasta que el StageRun lo
// recoloca en exit_s. Con host o sin él; devuelve state_hash.
struct ShortcutRun {
    std::uint64_t hash = 0;
    int shortcuts = 0;
    std::vector<CarState> states;
};

ShortcutRun run_shortcut(bool with_host) {
    namespace sim = rally::sim;
    const std::string text = rally::tests::stage_file("hairpin_flat.stage");
    sim::StageTrack track;
    REQUIRE(sim::StageTrack::build(text, track).ok);
    sim::WorldDesc desc;
    desc.stage_text = text;
    desc.car = true;
    desc.car_data = rally::tests::reference_car();
    desc.car_spawn = sim::stage_spawn(track, 165.0, static_cast<float>(30.0 / 3.6));
    const sim::StagePose p = track.pose(165.0);
    constexpr double kEast = 0.5 * 3.14159265358979323846;
    desc.car_spawn.x = p.x + 5.0 * std::cos(p.heading);
    desc.car_spawn.z = p.z + 5.0 * std::sin(p.heading);
    desc.car_spawn.y = p.y + p.bank * 5.0;
    desc.car_spawn.yaw = static_cast<float>(-kEast);

    sim::SimWorld world;
    world.build(desc);
    world.seal();
    world.set_telemetry(true);
    Host host;
    std::string error;
    RawClient* client = nullptr;
    std::unique_ptr<RawClient> client_owner;
    if (with_host) {
        REQUIRE_MESSAGE(host.start(0, error), error);
        host.set_world("mundo", "coche", 1);
        client_owner = std::make_unique<RawClient>(host.port());
        client = client_owner.get();
        const double end = wall_seconds() + 5.0;
        while (wall_seconds() < end && !host.copilot_connected()) {
            host.service();
            client->service();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        REQUIRE(host.copilot_connected());
    }
    std::uint32_t step = 0;
    for (int frame = 0; frame < 20 * 60 && world.stage_run()->shortcuts == 0; ++frame) {
        for (int i = 0; i < 4; ++i) {
            const sim::BodyPose pose = world.car_pose();
            const sim::BodyVelocity v = world.car_velocity();
            sim::SimInput in;
            in.throttle = static_cast<std::int16_t>(
                std::clamp(0.5 * (30.0 / 3.6 - std::hypot(v.vx, v.vz)), 0.0, 1.0) * sim::kAxisMax);
            in.steer = static_cast<std::int16_t>(
                std::clamp(2.0 * rally::tests::wrap_angle(kEast - rally::tests::car_heading(pose)),
                           -1.0, 1.0) *
                sim::kAxisMax);
            world.step(in);
            ++step;
        }
        if (with_host) {
            host.after_steps(world, step, false); // el botón no se pulsa: solo el atajo
            host.service();
            client->service();
        }
    }
    if (with_host) {
        for (int i = 0; i < 4; ++i) { // un par de estados más tras el atajo
            for (int k = 0; k < 8; ++k) {
                world.step(sim::SimInput{});
                ++step;
            }
            host.after_steps(world, step, false);
        }
        const double end = wall_seconds() + 2.0;
        while (wall_seconds() < end) {
            host.service();
            client->service();
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    } else {
        for (int i = 0; i < 4 * 8; ++i) {
            world.step(sim::SimInput{});
        }
    }
    ShortcutRun run;
    run.shortcuts = world.stage_run()->shortcuts;
    run.hash = world.state_hash();
    if (client != nullptr) {
        run.states = client->states;
    }
    return run;
}

} // namespace

TEST_CASE("net_reset: a shortcut of the stage run raises the counter without the button") {
    const rally::sim::JoltRuntime runtime;
    const ShortcutRun with = run_shortcut(true);
    const ShortcutRun without = run_shortcut(false);
    REQUIRE(with.shortcuts == 1);
    REQUIRE_FALSE(with.states.empty());
    // El contador sube una vez, en el estado del atajo, y las muestras del recolocado no se
    // mezclan con las de antes.
    int changes = 0;
    for (std::size_t i = 1; i < with.states.size(); ++i) {
        changes += with.states[i].reset_count != with.states[i - 1].reset_count ? 1 : 0;
    }
    std::printf("net_reset: atajo: %zu estados, el contador cambia %d vez, ultimo contador %d\n",
                with.states.size(), changes, with.states.back().reset_count);
    CHECK(changes == 1);
    CHECK(with.states.front().reset_count == 0);
    CHECK(with.states.back().reset_count == 1);
    // Criterio 7 con tramo: el host solo lee.
    CHECK(with.hash == without.hash);
}
