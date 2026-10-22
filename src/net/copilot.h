#pragma once

// Copiloto: la segunda instancia. Se conecta al host (127.0.0.1), recibe el mundo y los
// estados del coche, y los deja en un StateBuffer para interpolar. Sin reloj: el tiempo llega
// en service(now_s). Sin enet.h en este encabezado.

#include "net/protocol.h"
#include "net/state_buffer.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

namespace rally::net {

class Copilot {
public:
    Copilot();
    ~Copilot();
    Copilot(const Copilot&) = delete;
    Copilot& operator=(const Copilot&) = delete;

    // `address`: una IP (127.0.0.1). El copiloto se ata a 127.0.0.1, puerto libre.
    bool connect(std::string_view address, std::uint16_t port, std::string& error);

    // Recibe y, sin conexión, reintenta cada 1 s.
    void service(double now_s);

    // Mundo nuevo para construir (una vez cada uno).
    std::optional<WorldInfo> take_world();

    const StateBuffer& buffer() const;

    // Red simulada (PCG32 sembrado): solo afecta a los estados recibidos.
    void set_impairment(const Impairment& impairment, std::uint64_t seed);

    LinkStats stats() const;
    // «conectado», «sin conexión, reintentando» o el motivo del rechazo del host.
    std::string_view status() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rally::net
