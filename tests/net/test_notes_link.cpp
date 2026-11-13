// Notas por ENet de verdad, por 127.0.0.1 y en un solo proceso (protocolo 4). Cada fase tiene un
// máximo de 5 s. La cola vive fuera de net::Host: aquí la aplica el test, como hará la plataforma.

#include "car_worlds.h"
#include "game/notes.h"
#include "net/copilot.h"
#include "net/host.h"
#include "net/notes_wire.h"
#include "net/protocol.h"
#include "net/wire.h"
#include "pump.h"
#include "sim/jolt_runtime.h"
#include "sim/pcg32.h"
#include "sim/sim_world.h"

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <enet/enet.h>
#include <memory>
#include <string>
#include <vector>

namespace {

using namespace rally::net;
using namespace rally::game;
using rally::tests::pump;
using rally::tests::wall_seconds;

std::unique_ptr<Host> start_host() {
    auto host = std::make_unique<Host>();
    std::string error;
    REQUIRE_MESSAGE(host->start(0, error), error);
    return host;
}

std::unique_ptr<Copilot> start_copilot(std::uint16_t port) {
    auto copilot = std::make_unique<Copilot>();
    std::string error;
    REQUIRE_MESSAGE(copilot->connect("127.0.0.1", port, error), error);
    return copilot;
}

NoteOrder random_order(rally::sim::Pcg32& rng) {
    const std::uint32_t pick = rng.next_u32() % 100;
    const double s = static_cast<double>(rng.next_u32() % 2000000) / 1000.0;
    if (pick < 50) {
        return corner_order(s, static_cast<NoteGrade>(1 + rng.next_u32() % 7),
                            rng.next_u32() % 2 == 0 ? NoteSide::Left : NoteSide::Right);
    }
    if (pick < 58) {
        const NoteMark hazards[] = {NoteMark::Crest, NoteMark::Jump, NoteMark::Caution};
        return hazard_order(s, hazards[rng.next_u32() % 3]);
    }
    if (pick < 70) {
        return mark_order(static_cast<NoteMark>(rng.next_u32() % kNoteMarkCount));
    }
    if (pick < 85) {
        return rng.next_u32() % 2 == 0
                   ? distance_order(wheel_distance(static_cast<int>(rng.next_u32() % 8)))
                   : distance_order(counter_distance(static_cast<int>(rng.next_u32() % 100) * 10));
    }
    if (pick < 96) {
        return erase_last_order();
    }
    return clear_pending_order();
}

NoteOrder arrived(NoteOrder o) {
    if (o.kind == NoteOrderKind::Corner || o.kind == NoteOrderKind::Hazard) {
        o.s = from_mm(to_mm(o.s));
    }
    return o;
}

// El lado del host de un fotograma: recoge las órdenes, las aplica a la cola y publica.
struct HostSide {
    Host* host = nullptr;
    NoteQueue queue;
    std::vector<NoteOrder> taken;
    double car_s = 0.0;

    void frame() {
        host->service();
        NoteOrder o;
        while (host->take_note_order(o)) {
            taken.push_back(o);
            queue.apply(o, car_s);
        }
        host->publish_notes(queue);
    }
};

// Conecta un copiloto y le manda un mundo; devuelve su generación.
std::uint8_t connect_with_world(HostSide& side, Copilot& copilot) {
    side.host->set_world("mundo", "coche", 1);
    std::optional<WorldInfo> info;
    REQUIRE(pump(side.host, {&copilot}, [&] {
        side.frame();
        if (!info) {
            info = copilot.take_world();
        }
        return info.has_value();
    }));
    return info->generation;
}

} // namespace

TEST_CASE(
    "net_notes: 200 mixed orders arrive, all and in order, and the copilot sees the anchors") {
    auto host = start_host();
    HostSide side;
    side.host = host.get();
    side.queue.reset(2000.0);
    auto copilot = start_copilot(host->port());
    const std::uint8_t generation = connect_with_world(side, *copilot);

    rally::sim::Pcg32 rng(42, 7);
    std::vector<NoteOrder> sent;
    for (int i = 0; i < 200; ++i) {
        const NoteOrder o = random_order(rng);
        REQUIRE(copilot->send_note_order(o));
        sent.push_back(arrived(o));
    }
    REQUIRE(pump(host.get(), {copilot.get()}, [&] {
        side.frame();
        return side.taken.size() == 200 && copilot->notes().revision == 200;
    }));
    REQUIRE(side.taken.size() == 200);
    CHECK(side.taken == sent); // todas, iguales y en el mismo orden
    CHECK(host->stats().orders_dropped == 0);

    // El copiloto muestra lo que dice el host, nada más.
    const NotesState expected = notes_state_from(side.queue, generation);
    CHECK(copilot->notes() == expected);
    CHECK(copilot->notes().count == side.queue.size());
    for (int i = 0; i < side.queue.size(); ++i) {
        CHECK(notes_has_anchor_near(copilot->notes(), side.queue.at(i).s, kNoteAnchorStepM));
    }
    CHECK_FALSE(notes_has_anchor_near(copilot->notes(), 2500.0, kNoteAnchorStepM));
    CHECK(notes_ahead(copilot->notes(), 0.0) == side.queue.view(0.0).ahead);
    if (side.queue.has_last()) {
        CHECK(copilot->notes().has_last);
        CHECK(copilot->notes().last.id == side.queue.last().id);
    }
    // Una orden que no es válida no sale.
    CHECK_FALSE(copilot->send_note_order(corner_order(1.0, NoteGrade::None, NoteSide::Left)));
}

TEST_CASE("net_notes: with the bad link the orders still arrive, because they go reliable") {
    const rally::sim::JoltRuntime runtime;
    auto host = start_host();
    HostSide side;
    side.host = host.get();
    side.queue.reset(2000.0);
    auto copilot = start_copilot(host->port());
    connect_with_world(side, *copilot);
    // 250 ± 80 ms y 20 % de pérdida. set_impairment solo actúa sobre los ESTADOS del coche que
    // recibe el copiloto (LinkSimulator): el canal 0 no pasa por ella. Lo que esta prueba
    // comprueba es que las órdenes llegan todas mientras los estados se pierden y se retrasan,
    // no que se pierdan paquetes de órdenes.
    copilot->set_impairment({0.250, 0.080, 0.20}, 99);

    rally::sim::SimWorld world;
    world.build(rally::tests::flat_world(rally::sim::Surface::Asphalt, false, 20.0f));
    world.seal();
    world.set_telemetry(true);
    rally::sim::SimInput input;
    input.throttle = rally::sim::kAxisMax / 2;
    rally::sim::Pcg32 rng(7, 7);
    std::vector<NoteOrder> sent;
    for (std::uint32_t frame = 0; frame < 900; ++frame) {
        for (int i = 0; i < 4; ++i) {
            world.step(input);
        }
        host->after_steps(world, (frame + 1) * 4, false);
        side.frame();
        if (frame % 4 == 0 && sent.size() < 200) {
            const NoteOrder o = random_order(rng);
            REQUIRE(copilot->send_note_order(o));
            sent.push_back(arrived(o));
        }
        copilot->service(wall_seconds());
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    REQUIRE(pump(host.get(), {copilot.get()}, [&] {
        side.frame();
        return side.taken.size() == 200 && copilot->notes().revision == 200;
    }));
    CHECK(side.taken == sent);
    CHECK(copilot->notes() == notes_state_from(side.queue, copilot->notes().generation));
    const BufferStats stats = copilot->buffer().stats(wall_seconds());
    std::printf("net_notes: con la red mala, estados perdidos %llu, tarde %llu; órdenes 200/200\n",
                static_cast<unsigned long long>(stats.missing),
                static_cast<unsigned long long>(stats.late));
    CHECK(stats.missing > 0); // la red simulada hizo su efecto sobre los estados
}

TEST_CASE("net_notes: a copilot that connects late gets the notes, and a new world empties them") {
    auto host = start_host();
    HostSide side;
    side.host = host.get();
    side.queue.reset(2000.0);
    host->set_world("mundo", "coche", 1);
    // Notas aplicadas antes de que haya copiloto: publish_notes no hace nada y no falla.
    for (int i = 0; i < 5; ++i) {
        side.queue.apply(corner_order(100.0 * (i + 1), NoteGrade::G3, NoteSide::Left), 0.0);
    }
    side.frame();
    auto copilot = start_copilot(host->port());
    REQUIRE(pump(host.get(), {copilot.get()}, [&] {
        side.frame();
        return copilot->notes().count == 5;
    }));
    CHECK(copilot->notes().revision == 5);
    CHECK(copilot->notes() == notes_state_from(side.queue, copilot->notes().generation));

    // Mundo nuevo: la cola y las notas del copiloto vuelven a empezar.
    side.queue.reset(2000.0);
    host->set_world("otro mundo", "coche", 2);
    std::optional<WorldInfo> info;
    REQUIRE(pump(host.get(), {copilot.get()}, [&] {
        side.frame();
        if (!info) {
            info = copilot->take_world();
        }
        return info.has_value() && copilot->notes().generation == info->generation;
    }));
    CHECK(copilot->notes().count == 0);
    CHECK_FALSE(copilot->notes().has_last);
    CHECK(copilot->notes().generation == info->generation);

    // Al desconectarse, las notas del copiloto se vacían y no se puede mandar nada.
    side.queue.apply(corner_order(10.0, NoteGrade::G1, NoteSide::Right), 0.0);
    REQUIRE(pump(host.get(), {copilot.get()}, [&] {
        side.frame();
        return copilot->notes().count == 1;
    }));
    host.reset();
    REQUIRE(pump(nullptr, {copilot.get()}, [&] { return !copilot->stats().connected; }, 2.0));
    CHECK(copilot->notes().count == 0);
    CHECK_FALSE(copilot->send_note_order(erase_last_order()));
}

TEST_CASE("net_notes: an order from the old world is dropped and counted") {
    auto host = start_host();
    HostSide side;
    side.host = host.get();
    side.queue.reset(2000.0);
    auto copilot = start_copilot(host->port());
    connect_with_world(side, *copilot);
    // Orden buena del mundo actual.
    REQUIRE(copilot->send_note_order(corner_order(10.0, NoteGrade::G2, NoteSide::Left)));
    REQUIRE(pump(host.get(), {copilot.get()}, [&] {
        side.frame();
        return side.taken.size() == 1;
    }));
    CHECK(host->stats().orders_dropped == 0);

    // El host reconstruye el mundo (la generación sube) y el copiloto manda una orden antes de
    // enterarse: se descarta y se cuenta, no desaparece sin rastro.
    host->set_world("otro", "coche", 2);
    REQUIRE(copilot->send_note_order(corner_order(20.0, NoteGrade::G5, NoteSide::Right)));
    REQUIRE(pump(host.get(), {copilot.get()}, [&] {
        side.frame();
        return host->stats().orders_dropped == 1;
    }));
    CHECK(side.taken.size() == 1);
    // Cuando el copiloto recibe el mundo nuevo, sus órdenes valen otra vez.
    std::optional<WorldInfo> info;
    REQUIRE(pump(host.get(), {copilot.get()}, [&] {
        side.frame();
        if (!info) {
            info = copilot->take_world();
        }
        return info.has_value();
    }));
    REQUIRE(copilot->send_note_order(corner_order(30.0, NoteGrade::G4, NoteSide::Left)));
    REQUIRE(pump(host.get(), {copilot.get()}, [&] {
        side.frame();
        return side.taken.size() == 2;
    }));
    CHECK(host->stats().orders_dropped == 1);
}

TEST_CASE("net_notes: a rejected second copilot cannot put orders in the queue") {
    auto host = start_host();
    HostSide side;
    side.host = host.get();
    side.queue.reset(2000.0);
    auto first = start_copilot(host->port());
    const std::uint8_t generation = connect_with_world(side, *first);

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
    bool sent = false;
    const double end = wall_seconds() + 3.0;
    while (wall_seconds() < end) {
        side.frame();
        first->service(wall_seconds());
        ENetEvent event;
        while (enet_host_service(client, &event, 0) > 0) {
            if (event.type == ENET_EVENT_TYPE_CONNECT) {
                const std::string hello = encode_hello(kProtocolVersion);
                enet_peer_send(
                    peer, 0,
                    enet_packet_create(hello.data(), hello.size(), ENET_PACKET_FLAG_RELIABLE));
                // Sin esperar la respuesta: una orden con la generación buena.
                const std::string order =
                    encode_note_order(corner_order(5.0, NoteGrade::G1, NoteSide::Left), generation);
                enet_peer_send(
                    peer, 0,
                    enet_packet_create(order.data(), order.size(), ENET_PACKET_FLAG_RELIABLE));
                sent = true;
            } else if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                enet_packet_destroy(event.packet);
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    enet_peer_disconnect_now(peer, 0);
    enet_host_flush(client);
    enet_host_destroy(client);
    REQUIRE(sent);
    CHECK(side.taken.empty());
    CHECK(side.queue.size() == 0);
    // El primero sigue conectado y sus órdenes valen.
    CHECK(host->copilot_connected());
    REQUIRE(first->send_note_order(corner_order(6.0, NoteGrade::G2, NoteSide::Right)));
    REQUIRE(pump(host.get(), {first.get()}, [&] {
        side.frame();
        return side.taken.size() == 1;
    }));
}

TEST_CASE("net_notes: cost of the notes in a host frame") {
    // Un fotograma del host: service() + recoger y aplicar órdenes + publish_notes. 600
    // fotogramas (10 s a 60 fps) con 10 órdenes por segundo frente a 600 sin ninguna. Sin umbral:
    // se informa.
    struct Run {
        double mean_ms = 0, max_ms = 0, bytes_per_s = 0;
    };
    auto run = [](int orders_per_second) {
        auto host = start_host();
        HostSide side;
        side.host = host.get();
        side.queue.reset(5000.0);
        auto copilot = start_copilot(host->port());
        connect_with_world(side, *copilot);
        rally::sim::Pcg32 rng(5, 5);
        double total = 0.0;
        double worst = 0.0;
        std::uint64_t bytes = 0;
        std::uint32_t last_revision = 0;
        for (int frame = 0; frame < 600; ++frame) {
            if (orders_per_second > 0 && frame % (60 / orders_per_second) == 0) {
                copilot->send_note_order(random_order(rng));
            }
            copilot->service(wall_seconds());
            const auto t0 = std::chrono::steady_clock::now();
            side.frame();
            const auto t1 = std::chrono::steady_clock::now();
            const double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
            total += ms;
            worst = std::max(worst, ms);
            if (side.queue.revision() != last_revision) {
                last_revision = side.queue.revision();
                bytes += encode_notes_state(notes_state_from(side.queue, 0)).size();
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        return Run{total / 600.0, worst, static_cast<double>(bytes) / 10.0};
    };
    const Run quiet = run(0);
    const Run busy = run(10);
    std::printf("net_notes: fotograma del host, sin órdenes %.4f ms de media (máx. %.3f); con 10 "
                "órdenes/s %.4f ms de media (máx. %.3f); estado de notas %.0f B/s\n",
                quiet.mean_ms, quiet.max_ms, busy.mean_ms, busy.max_ms, busy.bytes_per_s);
    CHECK(busy.mean_ms >= 0.0);
}
