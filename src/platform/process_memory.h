#pragma once

#include <cstdint>

namespace rally::platform {

// Memoria del proceso para el contador de rendimiento, en bytes (0 si Windows no la da).
struct ProcessMemory {
    std::uint64_t working_set = 0;   // páginas en RAM ahora mismo
    std::uint64_t private_bytes = 0; // memoria privada comprometida (PrivateUsage)
};

// GetProcessMemoryInfo (K32GetProcessMemoryInfo, en kernel32). Es una llamada al sistema:
// el contador la hace cada 0,5 s, no en cada fotograma.
ProcessMemory query_process_memory();

} // namespace rally::platform
