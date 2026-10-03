#pragma once

#include "sim/jolt_config.h"

#include <Jolt/Core/Memory.h>

#include <cstddef>

namespace rally::tests {

// Cuenta las reservas que pasan por el asignador de Jolt mientras vive.
class JoltAllocationCounter {
public:
    JoltAllocationCounter()
        : allocate_(JPH::Allocate), reallocate_(JPH::Reallocate),
          aligned_allocate_(JPH::AlignedAllocate) {
        s_count = 0;
        s_allocate = allocate_;
        s_reallocate = reallocate_;
        s_aligned_allocate = aligned_allocate_;
        JPH::Allocate = [](std::size_t size) {
            ++s_count;
            return s_allocate(size);
        };
        JPH::Reallocate = [](void* block, std::size_t old_size, std::size_t new_size) {
            ++s_count;
            return s_reallocate(block, old_size, new_size);
        };
        JPH::AlignedAllocate = [](std::size_t size, std::size_t alignment) {
            ++s_count;
            return s_aligned_allocate(size, alignment);
        };
    }
    ~JoltAllocationCounter() {
        JPH::Allocate = allocate_;
        JPH::Reallocate = reallocate_;
        JPH::AlignedAllocate = aligned_allocate_;
    }
    JoltAllocationCounter(const JoltAllocationCounter&) = delete;
    JoltAllocationCounter& operator=(const JoltAllocationCounter&) = delete;

    int count() const { return s_count; }

private:
    static inline int s_count = 0;
    static inline JPH::AllocateFunction s_allocate = nullptr;
    static inline JPH::ReallocateFunction s_reallocate = nullptr;
    static inline JPH::AlignedAllocateFunction s_aligned_allocate = nullptr;

    JPH::AllocateFunction allocate_;
    JPH::ReallocateFunction reallocate_;
    JPH::AlignedAllocateFunction aligned_allocate_;
};

} // namespace rally::tests
