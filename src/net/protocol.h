#pragma once

// Protocolo de red 1 (lote 3 §2.5): constantes y mensajes de control. Little-endian, campo a
// campo; cada mensaje empieza por un u8 con su tipo. El estado del coche (tipo 1) está en
// net/car_state.h. Sin ENet.

#include "net/car_state.h"

#include <cstdint>
#include <span>
#include <string>

namespace rally::net {

inline constexpr std::uint16_t kProtocolVersion = 1;
inline constexpr std::uint16_t kDefaultPort = 29470;
inline constexpr int kStateEverySteps = 8; // 30 Hz a 240 Hz
inline constexpr double kStepRate = 240.0; // pasos por segundo de la simulación
inline constexpr double kInterpolationDelay = 0.100; // s (24 pasos)
inline constexpr double kMaxExtrapolation = 0.100;   // s; después, quieto

inline constexpr std::uint8_t kMsgWorld = 2;
inline constexpr std::uint8_t kMsgHello = 3;
inline constexpr std::uint8_t kMsgReply = 4;

// Canales de ENet: 0 fiable (control y mundo), 1 no fiable (estados).
inline constexpr int kChannelReliable = 0;
inline constexpr int kChannelStates = 1;

// Mundo que el host manda al copiloto para construirlo. `world_bytes` es lo que da
// io::serialize_world y `car_text` el archivo del coche entero; la plataforma los llama.
struct WorldInfo {
    std::uint8_t generation = 0;
    std::uint64_t zone_hash = 0;
    std::string world_bytes;
    std::string car_text;
};

struct Reply {
    std::uint16_t version = kProtocolVersion;
    bool accepted = false;
    std::string reason; // UTF-8; vacío si se acepta
};

struct LinkStats {
    bool connected = false;
    double rtt_s = 0.0;
    std::uint64_t sent = 0;     // mensajes de estado enviados (host) o 0 (copiloto)
    std::uint64_t received = 0; // estados recibidos (copiloto) o mensajes de control (host)
};

// Los `decode_*` devuelven false si el tamaño no cuadra con lo que dicen las longitudes o el
// tipo es otro; no leen fuera del mensaje.
std::string encode_hello(std::uint16_t version);
bool decode_hello(std::span<const std::uint8_t> in, std::uint16_t& version);
std::string encode_reply(const Reply& reply);
bool decode_reply(std::span<const std::uint8_t> in, Reply& out);
std::string encode_world(const WorldInfo& world);
bool decode_world(std::span<const std::uint8_t> in, WorldInfo& out);

} // namespace rally::net
