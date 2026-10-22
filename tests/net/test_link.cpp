// ENet de verdad por 127.0.0.1, en un solo proceso. Cada fase tiene un máximo de 5 s.

#include "car_worlds.h"
#include "net/copilot.h"
#include "net/host.h"
#include "net/protocol.h"
#include "net/runtime.h"
#include "new_counter.h"
#include "pump.h"
#include "sim/jolt_runtime.h"
#include "sim/pcg32.h"
#include "sim/sim_world.h"

#include <doctest/doctest.h>
#include <enet/enet.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <memory>
#include <string>

namespace {

using namespace rally::net;
using rally::tests::pump;
using rally::tests::wall_seconds;

std::string binary_blob(std::size_t size, std::uint64_t seed) {
    rally::sim::Pcg32 rng(seed, 3);
    std::string s(size, '\0');
    for (char& c : s) {
        c = static_cast<char>(rng.next_u32() & 0xFF);
    }
    return s;
}

std::unique_ptr<Host> start_host(std::uint16_t port = 0) {
    auto host = std::make_unique<Host>();
    std::string error;
    REQUIRE_MESSAGE(host->start(port, error), error);
    return host;
}

std::unique_ptr<Copilot> start_copilot(std::uint16_t port) {
    auto copilot = std::make_unique<Copilot>();
    std::string error;
    REQUIRE_MESSAGE(copilot->connect("127.0.0.1", port, error), error);
    return copilot;
}

} // namespace

TEST_CASE("net_link: without a host the copilot keeps retrying") {
    std::uint16_t closed_port = 0;
    {
        Host temp;
        std::string error;
        REQUIRE(temp.start(0, error));
        closed_port = temp.port();
    }
    auto copilot = start_copilot(closed_port);
    const double end = wall_seconds() + 2.5;
    while (wall_seconds() < end) {
        copilot->service(wall_seconds());
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(copilot->status() == "sin conexión, reintentando");
    CHECK_FALSE(copilot->stats().connected);
    CHECK_FALSE(copilot->take_world().has_value());
    Pose pose;
    CHECK_FALSE(copilot->buffer().sample(wall_seconds(), pose));
}

TEST_CASE("net_link: handshake, world, 300 states in order") {
    const rally::sim::JoltRuntime runtime;
    auto host = start_host();
    REQUIRE(host->port() != 0);
    auto copilot = start_copilot(host->port());

    // Conectado sin mundo.
    REQUIRE(pump(host.get(), {copilot.get()}, [&] { return host->copilot_connected() && copilot->stats().connected; }));
    CHECK(copilot->status() == "conectado");
    CHECK_FALSE(copilot->take_world().has_value());
    Pose pose;
    CHECK_FALSE(copilot->buffer().sample(wall_seconds(), pose));

    // Mundo de 200 kB (fragmentado por ENet): bytes y zone_hash exactos.
    const std::string world_bytes = binary_blob(200 * 1024, 11);
    const std::string car_text = "name = prueba\n" + binary_blob(3000, 12);
    const std::uint64_t zone_hash = 0x0123456789abcdefull;
    host->set_world(world_bytes, car_text, zone_hash);
    std::optional<WorldInfo> info;
    REQUIRE(pump(host.get(), {copilot.get()}, [&] {
        info = copilot->take_world();
        return info.has_value();
    }));
    CHECK(info->world_bytes == world_bytes);
    CHECK(info->car_text == car_text);
    CHECK(info->zone_hash == zone_hash);
    CHECK(info->generation == 1);
    CHECK_FALSE(copilot->take_world().has_value()); // una sola vez
    // Con mundo y sin estados.
    CHECK_FALSE(copilot->buffer().sample(wall_seconds(), pose));

    // 2400 pasos de un SimWorld real, un estado cada 8: 300 estados.
    rally::sim::SimWorld world;
    world.build(rally::tests::flat_world(rally::sim::Surface::Asphalt, false, 20.0f));
    world.seal();
    world.set_telemetry(true);
    rally::sim::SimInput input;
    input.throttle = rally::sim::kAxisMax / 2;
    std::uint32_t last_newest = 0;
    bool in_order = true;
    std::uint64_t host_allocs = 0;
    for (std::uint32_t frame = 0; frame < 600; ++frame) {
        for (int i = 0; i < 4; ++i) {
            world.step(input);
        }
        const std::uint32_t step = (frame + 1) * 4;
        const std::uint64_t a0 = rally::tests::global_new_count() + enet_allocation_count();
        host->after_steps(world, step, false);
        host->service();
        host_allocs += rally::tests::global_new_count() + enet_allocation_count() - a0;
        copilot->service(wall_seconds());
        const std::uint32_t newest = copilot->buffer().newest_step();
        in_order = in_order && newest >= last_newest;
        last_newest = newest;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(pump(host.get(), {copilot.get()}, [&] {
        return copilot->stats().received + 3 >= host->stats().sent;
    }));
    const BufferStats stats = copilot->buffer().stats(wall_seconds());
    std::printf("net_link: enviados %llu, recibidos %llu, perdidos %llu, tarde %llu, rtt %.3f ms; "
                "reservas del host (new + ENet) %llu en 10 s de simulacion = %.1f por segundo\n",
                static_cast<unsigned long long>(host->stats().sent),
                static_cast<unsigned long long>(stats.received),
                static_cast<unsigned long long>(stats.missing),
                static_cast<unsigned long long>(stats.late), host->stats().rtt_s * 1000.0,
                static_cast<unsigned long long>(host_allocs), static_cast<double>(host_allocs) / 10.0);
    CHECK(host->stats().sent >= 300);
    CHECK(in_order);
    CHECK(stats.late == 0);
    CHECK(stats.missing <= 3);
    CHECK(stats.received + 3 >= host->stats().sent);
    // El estado más nuevo del copiloto es el del host (hasta la cuantización).
    CHECK(copilot->buffer().newest_step() >= 2392);
}

TEST_CASE("net_link: another protocol version is answered with its reason") {
    auto host = start_host();
    ENetAddress local{};
    enet_address_set_host_ip(&local, "127.0.0.1");
    local.port = 0;
    ENetHost* client = enet_host_create(&local, 1, 2, 0, 0);
    REQUIRE(client != nullptr);
    ENetAddress server{};
    enet_address_set_host_ip(&server, "127.0.0.1");
    server.port = host->port();
    ENetPeer* peer = enet_host_connect(client, &server, 2, 0);
    REQUIRE(peer != nullptr);

    bool replied = false;
    Reply reply;
    const double end = wall_seconds() + 5.0;
    while (wall_seconds() < end && !replied) {
        host->service();
        ENetEvent event;
        while (enet_host_service(client, &event, 0) > 0) {
            if (event.type == ENET_EVENT_TYPE_CONNECT) {
                const std::string hello = encode_hello(2);
                ENetPacket* packet =
                    enet_packet_create(hello.data(), hello.size(), ENET_PACKET_FLAG_RELIABLE);
                enet_peer_send(peer, 0, packet);
            } else if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                replied = decode_reply(std::span<const std::uint8_t>(event.packet->data,
                                                                      event.packet->dataLength),
                                       reply);
                enet_packet_destroy(event.packet);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    enet_peer_disconnect_now(peer, 0);
    enet_host_flush(client);
    enet_host_destroy(client);
    REQUIRE(replied);
    CHECK(reply.version == 1);
    CHECK_FALSE(reply.accepted);
    CHECK(reply.reason == "protocolo 2, este juego usa el 1");
    CHECK_FALSE(host->copilot_connected());
}

TEST_CASE("net_link: a second copilot is rejected and takes the slot when the first leaves") {
    auto host = start_host();
    auto first = start_copilot(host->port());
    REQUIRE(pump(host.get(), {first.get()}, [&] { return first->stats().connected; }));
    auto second = start_copilot(host->port());
    REQUIRE(pump(host.get(), {first.get(), second.get()},
                 [&] { return second->status() == "ya hay un copiloto"; }));
    CHECK(first->stats().connected);
    CHECK_FALSE(second->stats().connected);
    CHECK(host->copilot_connected());

    // El primero se va (cierra el enlace): el segundo, que reintenta cada segundo, entra.
    first.reset();
    REQUIRE(pump(host.get(), {second.get()}, [&] { return second->stats().connected; }));
    CHECK(second->status() == "conectado");
}

TEST_CASE("net_link: closing the copilot and connecting again is accepted again") {
    auto host = start_host();
    host->set_world("mundo", "coche", 99);
    auto first = start_copilot(host->port());
    std::optional<WorldInfo> info;
    REQUIRE(pump(host.get(), {first.get()}, [&] {
        if (!info) {
            info = first->take_world();
        }
        return info.has_value();
    }));
    CHECK(info->world_bytes == "mundo");
    const std::uint8_t first_generation = info->generation;
    first.reset();
    REQUIRE(pump(host.get(), {}, [&] { return !host->copilot_connected(); }));

    auto again = start_copilot(host->port());
    info.reset();
    REQUIRE(pump(host.get(), {again.get()}, [&] {
        if (!info) {
            info = again->take_world();
        }
        return info.has_value();
    }));
    CHECK(again->status() == "conectado");
    CHECK(info->zone_hash == 99);
    // Cada envío del mundo sube la generación en 1.
    CHECK(info->generation == static_cast<std::uint8_t>(first_generation + 1));
}

TEST_CASE("net_link: when the host leaves the copilot falls back to retrying and reconnects") {
    auto host = start_host();
    const std::uint16_t port = host->port();
    auto copilot = start_copilot(port);
    REQUIRE(pump(host.get(), {copilot.get()}, [&] { return copilot->stats().connected; }));

    host.reset(); // cierra el enlace: el copiloto ve la desconexión, no un timeout
    REQUIRE(pump(nullptr, {copilot.get()}, [&] { return !copilot->stats().connected; }, 2.0));
    CHECK(copilot->status() == "sin conexión, reintentando");

    host = start_host(port);
    REQUIRE(pump(host.get(), {copilot.get()}, [&] { return copilot->stats().connected; }));
    CHECK(copilot->status() == "conectado");
}
