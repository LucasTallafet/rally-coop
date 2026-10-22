#pragma once

// Host: el juego que conduce. Escucha en 127.0.0.1, acepta un solo copiloto y le manda el
// mundo (canal fiable) y el estado del coche a 30 Hz (canal no fiable, protocolo 1). No lee la
// simulación salvo en after_steps, y solo la lee. Sin enet.h en este encabezado.

#include "net/protocol.h"
#include "sim/sim_world.h"

#include <cstdint>
#include <memory>
#include <string>

namespace rally::net {

class Host {
public:
    Host();
    ~Host();
    Host(const Host&) = delete;
    Host& operator=(const Host&) = delete;

    // 127.0.0.1; port 0 = uno libre (tests; port() dice cuál).
    bool start(std::uint16_t port, std::string& error);
    std::uint16_t port() const;

    // Mundo nuevo (cada build()): la generación sube en 1 (u8, con vuelta de 255 a 0) y se manda
    // al copiloto conectado; si se conecta después, se manda al aceptarlo.
    void set_world(std::string world_bytes, std::string car_text, std::uint64_t zone_hash);

    // Tras los pasos de un fotograma; `step` = pasos desde el último build(). Manda un estado si
    // el paso cruza un múltiplo de 8 (el primero tras set_world, siempre). `reset_since_last`:
    // el coche se recolocó desde el último estado (se acumula hasta el siguiente envío). Solo
    // lee `world`, que tiene que tener la telemetría encendida (el juego la enciende en cada
    // reset()); sin copiloto aceptado no lee nada.
    void after_steps(const sim::SimWorld& world, std::uint32_t step, bool reset_since_last);

    // Una vez por fotograma: acepta, responde y recibe.
    void service();

    bool copilot_connected() const;
    LinkStats stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace rally::net
