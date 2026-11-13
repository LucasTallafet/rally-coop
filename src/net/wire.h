#pragma once

// Escritor y lector little-endian con límites, y la cuantización a milímetros: lo comparten
// protocol.cpp (control y mundo) y notes_wire.cpp (notas). Interno de src/net; sin ENet.

#include "sim/rmath.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <utility>

namespace rally::net {

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
    void i32(std::int32_t v) { u32(static_cast<std::uint32_t>(v)); }
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
    bool i32(std::int32_t& v) {
        std::uint32_t u = 0;
        if (!u32(u)) {
            return false;
        }
        v = static_cast<std::int32_t>(u);
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

// Metros a milímetros: redondeo al entero más cercano (la mitad hacia arriba) con saturación.
inline std::int32_t to_mm(double meters) {
    const double r = rmath::floor(meters * 1000.0 + 0.5);
    return static_cast<std::int32_t>(rmath::clamp(r, -2147483648.0, 2147483647.0));
}

inline double from_mm(std::int32_t mm) { return static_cast<double>(mm) / 1000.0; }

} // namespace rally::net
