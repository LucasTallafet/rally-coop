#include "net/protocol.h"

#include "net/wire.h"

#include <cstddef>
#include <limits>

namespace rally::net {

namespace {

bool expect_type(In& in, std::uint8_t type) {
    std::uint8_t t = 0;
    return in.u8(t) && t == type;
}

} // namespace

std::string encode_hello(std::uint16_t version) {
    Out o;
    o.u8(kMsgHello);
    o.u16(version);
    return o.take();
}

bool decode_hello(std::span<const std::uint8_t> in, std::uint16_t& version) {
    In r(in);
    return expect_type(r, kMsgHello) && r.u16(version) && r.at_end();
}

std::string encode_reply(const Reply& reply) {
    Out o;
    o.u8(kMsgReply);
    o.u16(reply.version);
    o.u8(reply.accepted ? 1 : 0);
    o.blob(reply.reason);
    return o.take();
}

bool decode_reply(std::span<const std::uint8_t> in, Reply& out) {
    In r(in);
    Reply reply;
    std::uint8_t accepted = 0;
    if (!expect_type(r, kMsgReply) || !r.u16(reply.version) || !r.u8(accepted) ||
        !r.blob(reply.reason) || !r.at_end() || accepted > 1) {
        return false;
    }
    reply.accepted = accepted == 1;
    out = std::move(reply);
    return true;
}

std::string encode_world(const WorldInfo& world) {
    Out o;
    o.u8(kMsgWorld);
    o.u8(world.generation);
    o.u64(world.zone_hash);
    o.u64(world.stage_ground_hash);
    o.blob(world.world_bytes);
    o.blob(world.car_text);
    return o.take();
}

bool decode_world(std::span<const std::uint8_t> in, WorldInfo& out) {
    In r(in);
    WorldInfo world;
    if (!expect_type(r, kMsgWorld) || !r.u8(world.generation) || !r.u64(world.zone_hash) ||
        !r.u64(world.stage_ground_hash) || !r.blob(world.world_bytes) || !r.blob(world.car_text) ||
        !r.at_end()) {
        return false;
    }
    out = std::move(world);
    return true;
}

} // namespace rally::net
