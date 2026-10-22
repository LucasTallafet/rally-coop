#include "net/copilot.h"

#include "net/enet_runtime.h"

#include <enet/enet.h>

#include <span>
#include <utility>

namespace rally::net {

namespace {

constexpr double kRetrySeconds = 1.0;
constexpr const char* kStatusConnected = "conectado";
constexpr const char* kStatusRetrying = "sin conexión, reintentando";

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

struct Copilot::Impl {
    enum class State { Idle, Connecting, Handshake, Active };

    ENetHost* host = nullptr;
    ENetPeer* peer = nullptr;
    ENetAddress server{};
    State state = State::Idle;
    double since = 0.0;
    bool since_valid = false;
    double retry_at = 0.0;
    std::string rejected;
    std::string status = kStatusRetrying;

    StateBuffer buffer;
    LinkSimulator link;
    std::optional<WorldInfo> pending;
    std::uint64_t received = 0;

    void update_status() {
        if (state == State::Active) {
            status = kStatusConnected;
        } else if (!rejected.empty()) {
            status = rejected;
        } else {
            status = kStatusRetrying;
        }
    }

    void begin_attempt() {
        peer = enet_host_connect(host, &server, 2, 0);
        state = State::Connecting;
        since_valid = false;
        if (peer == nullptr) {
            state = State::Idle;
        }
    }

    void on_packet(enet_uint8 channel, const ENetPacket* packet, double now_s) {
        const std::span<const std::uint8_t> bytes(packet->data, packet->dataLength);
        if (channel == kChannelStates) {
            CarState carstate;
            if (decode(bytes, carstate)) {
                ++received;
                link.submit(carstate, now_s);
            }
            return;
        }
        Reply reply;
        WorldInfo world;
        if (decode_reply(bytes, reply)) {
            if (reply.accepted) {
                state = State::Active;
                rejected.clear();
            } else {
                rejected = reply.reason.empty() ? std::string("rechazado") : reply.reason;
            }
        } else if (decode_world(bytes, world)) {
            buffer.reset(world.generation);
            link.clear();
            pending = std::move(world);
        }
    }
};

Copilot::Copilot() : impl_(std::make_unique<Impl>()) {}

Copilot::~Copilot() {
    Impl& s = *impl_;
    if (s.host == nullptr) {
        return;
    }
    if (s.peer != nullptr && s.peer->state != ENET_PEER_STATE_DISCONNECTED) {
        enet_peer_disconnect_now(s.peer, 0);
    }
    enet_host_flush(s.host);
    enet_host_destroy(s.host);
}

bool Copilot::connect(std::string_view address, std::uint16_t port, std::string& error) {
    Impl& s = *impl_;
    if (s.host != nullptr) {
        error = "el copiloto ya está iniciado";
        return false;
    }
    ensure_enet_initialized();
    const std::string ip(address);
    if (enet_address_set_host_ip(&s.server, ip.c_str()) != 0) {
        error = "dirección no válida: " + ip;
        return false;
    }
    s.server.port = port;
    // Atado a 127.0.0.1 (y no a todas las interfaces) para que Windows no pregunte por el
    // cortafuegos.
    ENetAddress local{};
    enet_address_set_host_ip(&local, "127.0.0.1");
    local.port = 0;
    s.host = enet_host_create(&local, 1, 2, 0, 0);
    if (s.host == nullptr) {
        error = "no se puede abrir el socket del copiloto";
        return false;
    }
    s.begin_attempt();
    s.update_status();
    return true;
}

void Copilot::service(double now_s) {
    Impl& s = *impl_;
    if (s.host == nullptr) {
        return;
    }
    ENetEvent event;
    while (enet_host_service(s.host, &event, 0) > 0) {
        switch (event.type) {
        case ENET_EVENT_TYPE_CONNECT:
            enet_peer_timeout(event.peer, 32, 3000, 6000);
            s.state = Impl::State::Handshake;
            send_reliable(event.peer, encode_hello(kProtocolVersion));
            break;
        case ENET_EVENT_TYPE_RECEIVE:
            s.on_packet(event.channelID, event.packet, now_s);
            enet_packet_destroy(event.packet);
            break;
        case ENET_EVENT_TYPE_DISCONNECT:
            if (s.state == Impl::State::Active) {
                s.rejected.clear();
            }
            s.peer = nullptr;
            s.state = Impl::State::Idle;
            s.retry_at = now_s + kRetrySeconds;
            s.link.clear();
            break;
        case ENET_EVENT_TYPE_NONE:
            break;
        }
    }

    if (s.state == Impl::State::Idle && now_s >= s.retry_at) {
        s.begin_attempt();
    } else if (s.state == Impl::State::Connecting) {
        // Por UDP a un puerto cerrado ENet no avisa de nada: si no conecta en 1 s, se reintenta.
        if (!s.since_valid) {
            s.since = now_s;
            s.since_valid = true;
        } else if (now_s - s.since > kRetrySeconds) {
            enet_peer_reset(s.peer);
            s.peer = nullptr;
            s.state = Impl::State::Idle;
            s.retry_at = now_s;
        }
    }

    CarState carstate;
    double release = 0.0;
    while (s.link.pop_ready(now_s, carstate, release)) {
        s.buffer.push(carstate, release);
    }
    s.update_status();
}

std::optional<WorldInfo> Copilot::take_world() {
    std::optional<WorldInfo> out = std::move(impl_->pending);
    impl_->pending.reset();
    return out;
}

const StateBuffer& Copilot::buffer() const {
    return impl_->buffer;
}

void Copilot::set_impairment(const Impairment& impairment, std::uint64_t seed) {
    impl_->link.set(impairment, seed);
}

LinkStats Copilot::stats() const {
    LinkStats stats;
    stats.connected = impl_->state == Impl::State::Active;
    stats.rtt_s = (stats.connected && impl_->peer != nullptr) ? impl_->peer->roundTripTime / 1000.0 : 0.0;
    stats.received = impl_->received;
    return stats;
}

std::string_view Copilot::status() const {
    return impl_->status;
}

} // namespace rally::net
