// El contador de recolocaciones de Host::after_steps llega por ENet tal como se pidió. Un cliente
// ENet crudo guarda los estados decodificados (el Copilot solo deja pasar la pose).

#include "car_worlds.h"
#include "net/car_state.h"
#include "net/host.h"
#include "net/protocol.h"
#include "pump.h"
#include "sim/jolt_runtime.h"
#include "sim/sim_world.h"

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <enet/enet.h>
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
