#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>

namespace rally::sim {

// FNV-1a de 64 bits, incremental. Los enteros se añaden en little-endian, byte a byte, para
// que el hash no dependa de la representación en memoria.
class Fnv1a64 {
public:
    void add_byte(std::uint8_t byte) {
        hash_ ^= byte;
        hash_ *= 0x0000'0100'0000'01b3ull;
    }
    void add_bytes(std::string_view bytes) {
        for (const char c : bytes) {
            add_byte(static_cast<std::uint8_t>(c));
        }
    }
    void add_u16(std::uint16_t value) {
        add_byte(static_cast<std::uint8_t>(value));
        add_byte(static_cast<std::uint8_t>(value >> 8));
    }
    void add_u32(std::uint32_t value) {
        for (int i = 0; i < 4; ++i) {
            add_byte(static_cast<std::uint8_t>(value >> (8 * i)));
        }
    }
    std::uint64_t value() const { return hash_; }

private:
    std::uint64_t hash_ = 0xcbf2'9ce4'8422'2325ull;
};

inline std::uint64_t fnv1a64(std::string_view bytes) {
    Fnv1a64 hash;
    hash.add_bytes(bytes);
    return hash.value();
}

} // namespace rally::sim
