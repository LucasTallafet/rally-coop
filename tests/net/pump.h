#pragma once

// Reloj y bombeo de service() para los tests de ENet: el límite de 5 s por fase usa el reloj
// del test (src/net nunca lo lee: le llega como now_s).

#include "net/copilot.h"
#include "net/host.h"

#include <chrono>
#include <initializer_list>
#include <thread>

namespace rally::tests {

inline double wall_seconds() {
    static const auto origin = std::chrono::steady_clock::now();
    return std::chrono::duration<double>(std::chrono::steady_clock::now() - origin).count();
}

// Reparte service() entre el host (puede ser nullptr) y los copilotos hasta que `done()` o
// pasen `timeout` s. Devuelve si `done()` se cumplió.
template <typename Done>
bool pump(net::Host* host, std::initializer_list<net::Copilot*> copilots, Done done,
          double timeout = 5.0) {
    const double end = wall_seconds() + timeout;
    while (wall_seconds() < end) {
        if (host != nullptr) {
            host->service();
        }
        for (net::Copilot* c : copilots) {
            c->service(wall_seconds());
        }
        if (done()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return done();
}

} // namespace rally::tests
