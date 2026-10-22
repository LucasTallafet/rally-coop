#include "net/protocol.h"

#include <cstddef>
#include <limits>

namespace rally::net {

namespace {

class Out {
public:
    void u8(std::uint8_t v) { bytes_.push_back(static_cast<char>(v)); }
    void u16(std::uint16_t v) {
        u8(static_cast<std::uint8_t>(v & 0xFF));
        u8(static_cast<std::uint8_t>(v >> 8));
    }
    void u32(std::uint32_t v) {
        u16(static_cast<std::uint16_t>(v & 0xFFFF));
        u16(static_cast<std::uint16_t>(v >> 16));
    }
    void u64(std::uint64_t v) {
        u32(static_cast<std::uint32_t>(v & 0xFFFFFFFFu));
        u32(static_cast<std::uint32_t>(v >> 32));
    }
    void blob(const std::string& s) {
        u32(static_cast<std::uint32_t>(s.size()));
        bytes_.append(s);
    }
    std::string take() { return std::move(bytes_); }

private:
    std::string bytes_;
};

class In {
public:
    explicit In(std::span<const std::uint8_t> in) : in_(in) {}
    bool u8(std::uint8_t& v) {
        if (pos_ + 1 > in_.size()) {
            return false;
        }
        v = in_[pos_++];
        return true;
    }
    bool u16(std::uint16_t& v) {
        std::uint8_t lo = 0;
        std::uint8_t hi = 0;
        if (!u8(lo) || !u8(hi)) {
            return false;
        }
        v = static_cast<std::uint16_t>(lo | (hi << 8));
        return true;
    }
    bool u32(std::uint32_t& v) {
        std::uint16_t lo = 0;
        std::uint16_t hi = 0;
        if (!u16(lo) || !u16(hi)) {
            return false;
        }
        v = static_cast<std::uint32_t>(lo) | (static_cast<std::uint32_t>(hi) << 16);
        return true;
    }
    bool u64(std::uint64_t& v) {
        std::uint32_t lo = 0;
        std::uint32_t hi = 0;
        if (!u32(lo) || !u32(hi)) {
            return false;
        }
        v = static_cast<std::uint64_t>(lo) | (static_cast<std::uint64_t>(hi) << 32);
        return true;
    }
    bool blob(std::string& s) {
        std::uint32_t n = 0;
        if (!u32(n) || n > in_.size() - pos_) {
            return false;
        }
        s.assign(reinterpret_cast<const char*>(in_.data() + pos_), n);
        pos_ += n;
        return true;
    }
    bool at_end() const { return pos_ == in_.size(); }

private:
    std::span<const std::uint8_t> in_;
    std::size_t pos_ = 0;
};

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
    o.blob(world.world_bytes);
    o.blob(world.car_text);
    return o.take();
}

bool decode_world(std::span<const std::uint8_t> in, WorldInfo& out) {
    In r(in);
    WorldInfo world;
    if (!expect_type(r, kMsgWorld) || !r.u8(world.generation) || !r.u64(world.zone_hash) ||
        !r.blob(world.world_bytes) || !r.blob(world.car_text) || !r.at_end()) {
        return false;
    }
    out = std::move(world);
    return true;
}

} // namespace rally::net
