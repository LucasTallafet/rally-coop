#include "net/host.h"

#include "net/enet_runtime.h"

#include <array>
#include <enet/enet.h>
#include <span>
#include <utility>

namespace rally::net {

namespace {

constexpr std::size_t kEnetPeers = 4; // a nivel de ENet; la aplicación acepta uno solo

void send_reliable(ENetPeer* peer, const std::string& bytes) {
    ENetPacket* packet = enet_packet_create(bytes.data(), bytes.size(), ENET_PACKET_FLAG_RELIABLE);
    if (packet == nullptr) {
        return;
    }
    if (enet_peer_send(peer, kChannelReliable, packet) != 0) {
        enet_packet_destroy(packet);
    }
}

} // namespace

struct Host::Impl {
    ENetHost* host = nullptr;
    std::uint16_t port = 0;
    ENetPeer* active = nullptr;

    bool has_world = false;
    std::string world_bytes;
    std::string car_text;
    std::uint64_t zone_hash = 0;
    std::uint8_t generation = 0;

    std::int64_t last_index = -1;
    std::uint8_t reset_count = 0; // recolocaciones desde el último mundo enviado
    std::uint64_t sent = 0;
    std::uint64_t received = 0;

    void send_world() {
        ++generation; // cada envío del mundo sube la generación (u8, con vuelta)
        WorldInfo info;
        info.generation = generation;
        info.zone_hash = zone_hash;
        info.world_bytes = world_bytes;
        info.car_text = car_text;
        send_reliable(active, encode_world(info));
        last_index = -1;
        reset_count = 0;
    }

    void reject(ENetPeer* peer, const std::string& reason) {
        Reply reply;
        reply.accepted = false;
        reply.reason = reason;
        send_reliable(peer, encode_reply(reply));
        enet_peer_disconnect_later(peer, 0);
    }

    void on_hello(ENetPeer* peer, std::uint16_t version) {
        if (version != kProtocolVersion) {
            reject(peer, "protocolo " + std::to_string(version) + ", este juego usa el " +
                             std::to_string(kProtocolVersion));
            return;
        }
        if (active == peer) {
            return; // saludo repetido
        }
        if (active != nullptr) {
            reject(peer, "ya hay un copiloto");
            return;
        }
        active = peer;
        // Los estados son datos que se renuevan 30 veces por segundo: por 127.0.0.1 no hay
        // congestión que regular, y la regulación de ENet tiraría estados por culpa de un RTT
        // alto momentáneo (el mundo en vuelo, un fotograma largo).
        enet_peer_throttle_configure(peer, ENET_PEER_PACKET_THROTTLE_INTERVAL,
                                     ENET_PEER_PACKET_THROTTLE_ACCELERATION, 0);
        Reply reply;
        reply.accepted = true;
        send_reliable(peer, encode_reply(reply));
        last_index = -1;
        if (has_world) {
            send_world();
        }
    }

    void on_packet(ENetPeer* peer, enet_uint8 channel, const ENetPacket* packet) {
        if (channel != kChannelReliable) {
            return;
        }
        const std::span<const std::uint8_t> bytes(packet->data, packet->dataLength);
        std::uint16_t version = 0;
        if (decode_hello(bytes, version)) {
            ++received;
            on_hello(peer, version);
        }
    }
};

Host::Host() : impl_(std::make_unique<Impl>()) {}

Host::~Host() {
    if (impl_->host == nullptr) {
        return;
    }
    // disconnect_now vacía la cola y avisa al otro lado: no hace falta esperar a un timeout.
    for (std::size_t i = 0; i < impl_->host->peerCount; ++i) {
        ENetPeer* peer = &impl_->host->peers[i];
        if (peer->state != ENET_PEER_STATE_DISCONNECTED) {
            enet_peer_disconnect_now(peer, 0);
        }
    }
    enet_host_flush(impl_->host);
    enet_host_destroy(impl_->host);
}

bool Host::start(std::uint16_t port, std::string& error) {
    if (impl_->host != nullptr) {
        error = "el host ya está iniciado";
        return false;
    }
    ensure_enet_initialized();
    ENetAddress address{};
    enet_address_set_host_ip(&address, "127.0.0.1");
    address.port = port;
    impl_->host = enet_host_create(&address, kEnetPeers, 2, 0, 0);
    if (impl_->host == nullptr) {
        error = "no se puede escuchar en 127.0.0.1:" + std::to_string(port);
        return false;
    }
    impl_->port = impl_->host->address.port;
    return true;
}

std::uint16_t Host::port() const { return impl_->port; }

void Host::set_world(std::string world_bytes, std::string car_text, std::uint64_t zone_hash) {
    impl_->world_bytes = std::move(world_bytes);
    impl_->car_text = std::move(car_text);
    impl_->zone_hash = zone_hash;
    impl_->has_world = true;
    if (impl_->active != nullptr) {
        impl_->send_world();
    }
}

void Host::after_steps(const sim::SimWorld& world, std::uint32_t step, bool reset_since_last) {
    Impl& s = *impl_;
    if (s.active == nullptr || !s.has_world) {
        return;
    }
    if (reset_since_last) {
        ++s.reset_count; // también en un paso que la cadencia no envía: el contador lo lleva
    }
    const std::int64_t index = step / static_cast<std::uint32_t>(kStateEverySteps);
    if (s.last_index >= 0 && index <= s.last_index) {
        return;
    }
    const CarState state = car_state_from(world, step, s.generation, s.reset_count);
    std::array<std::uint8_t, kCarStateBytes> bytes{};
    encode(state, bytes);
    // Sin secuencia: un estado que llega tarde y desordenado todavía sirve (el búfer ordena y
    // descarta duplicados), y la red simulada y la real se comportan igual.
    ENetPacket* packet =
        enet_packet_create(bytes.data(), bytes.size(), ENET_PACKET_FLAG_UNSEQUENCED);
    if (packet != nullptr && enet_peer_send(s.active, kChannelStates, packet) != 0) {
        enet_packet_destroy(packet);
        packet = nullptr;
    }
    if (packet != nullptr) {
        enet_host_flush(s.host);
        ++s.sent;
    }
    s.last_index = index;
}

void Host::service() {
    Impl& s = *impl_;
    if (s.host == nullptr) {
        return;
    }
    ENetEvent event;
    while (enet_host_service(s.host, &event, 0) > 0) {
        switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT:
            enet_peer_timeout(event.peer, 32, 3000, 6000);
            break;
        case ENET_EVENT_TYPE_RECEIVE:
            s.on_packet(event.peer, event.channelID, event.packet);
            enet_packet_destroy(event.packet);
            break;
        case ENET_EVENT_TYPE_DISCONNECT:
            if (event.peer == s.active) {
                s.active = nullptr;
            }
            break;
        case ENET_EVENT_TYPE_NONE:
            break;
        }
    }
}

bool Host::copilot_connected() const { return impl_->active != nullptr; }

LinkStats Host::stats() const {
    LinkStats stats;
    stats.connected = impl_->active != nullptr;
    stats.rtt_s = stats.connected ? impl_->active->roundTripTime / 1000.0 : 0.0;
    stats.sent = impl_->sent;
    stats.received = impl_->received;
    return stats;
}

} // namespace rally::net
