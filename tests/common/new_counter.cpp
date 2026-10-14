// Sustituye los operator new/delete globales de rally_tests por versiones que cuentan las
// reservas. Basta con las formas simple y alineada: en la CRT de MSVC, las de array, nothrow
// y con tamaño llaman a estas.

#include "new_counter.h"

#include <atomic>
#include <cstdlib>
#include <new>

namespace {
std::atomic<std::uint64_t> g_new_count{0};
} // namespace

namespace rally::tests {
std::uint64_t global_new_count() { return g_new_count.load(std::memory_order_relaxed); }
} // namespace rally::tests

void* operator new(std::size_t size) {
    g_new_count.fetch_add(1, std::memory_order_relaxed);
    void* block = std::malloc(size != 0 ? size : 1);
    if (block == nullptr) {
        std::abort();
    }
    return block;
}

void* operator new(std::size_t size, std::align_val_t alignment) {
    g_new_count.fetch_add(1, std::memory_order_relaxed);
    void* block = _aligned_malloc(size != 0 ? size : 1, static_cast<std::size_t>(alignment));
    if (block == nullptr) {
        std::abort();
    }
    return block;
}

void operator delete(void* block) noexcept { std::free(block); }

void operator delete(void* block, std::align_val_t) noexcept { _aligned_free(block); }
