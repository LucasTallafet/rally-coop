#include "platform/process_memory.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
// Después de <windows.h>. Con PSAPI_VERSION 2 (por defecto desde Windows 7),
// GetProcessMemoryInfo es K32GetProcessMemoryInfo de kernel32: no hace falta psapi.lib.
#include <psapi.h>

namespace rally::platform {

ProcessMemory query_process_memory() {
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(GetCurrentProcess(),
                              reinterpret_cast<PROCESS_MEMORY_COUNTERS*>(&counters),
                              sizeof(counters))) {
        return ProcessMemory{};
    }
    return ProcessMemory{counters.WorkingSetSize, counters.PrivateUsage};
}

} // namespace rally::platform
