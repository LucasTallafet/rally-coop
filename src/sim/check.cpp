#include "sim/check.h"

#include <cstdio>
#include <cstdlib>

namespace rally::sim {

namespace {
CheckHandler g_handler = nullptr;
} // namespace

void set_check_handler(CheckHandler handler) { g_handler = handler; }

void check_failed(const char* message, const char* file, int line) {
    std::fprintf(stderr, "RALLY_CHECK: %s (%s:%d)\n", message, file, line);
    std::fflush(stderr);
    if (g_handler != nullptr) {
        g_handler(message);
    }
    // Sin el cuadro de abort() de la CRT ni el informe de errores de Windows: bloquearían
    // los tests de muerte de ctest y el mensaje ya se ha mostrado.
    _set_abort_behavior(0, _WRITE_ABORT_MSG | _CALL_REPORTFAULT);
    std::abort();
}

} // namespace rally::sim
