#include "net/enet_runtime.h"

#include "net/runtime.h"
#include "sim/check.h"

#include <enet/enet.h>

#include <cstdlib>

namespace rally::net {

namespace {

std::uint64_t g_allocations = 0;

void* counting_malloc(std::size_t size) {
    ++g_allocations;
    return std::malloc(size);
}

void counting_free(void* memory) {
    std::free(memory);
}

void no_memory() {
    sim::check_failed("net: ENet se quedó sin memoria", __FILE__, __LINE__);
}

struct EnetRuntime {
    EnetRuntime() {
        ENetCallbacks callbacks{};
        callbacks.malloc = counting_malloc;
        callbacks.free = counting_free;
        callbacks.no_memory = no_memory;
        RALLY_CHECK(enet_initialize_with_callbacks(ENET_VERSION, &callbacks) == 0,
                    "net: enet_initialize falló");
    }
    ~EnetRuntime() { enet_deinitialize(); }
    EnetRuntime(const EnetRuntime&) = delete;
    EnetRuntime& operator=(const EnetRuntime&) = delete;
};

} // namespace

void ensure_enet_initialized() {
    static EnetRuntime runtime;
    (void)runtime;
}

std::uint64_t enet_allocation_count() {
    return g_allocations;
}

} // namespace rally::net
