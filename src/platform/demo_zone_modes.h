#pragma once

#include "platform/demo_part.h"
#include "sim/test_zone.h"

#include <array>
#include <cstdint>

namespace rally::platform {

// Modos de la zona de la demo: K alterna la zona y el recinto (y vuelve al ojo), y en el recinto O
// lo reconstruye a 20 km del origen (o lo devuelve). En la zona, G cambia la superficie de la
// pista ciega, U la calzada de la carretera de prueba, Y la salida (recta o carretera) y M el modo
// a ciegas: todas las superficies del mismo color, sin h y sin nombres en pantalla; ahí G pasa a
// la siguiente prueba de la serie de 15 (--blind-seed). Todos piden un mundo nuevo.
class DemoZoneModes final : public DemoPart {
public:
    // Semilla de la serie de pruebas a ciegas (--blind-seed).
    void set_blind_seed(std::uint64_t seed) { blind_seed_ = seed; }

    void handle_keys(DemoContext& ctx, FramePacer& pacer, PerfCounter& perf) override;

private:
    std::uint64_t blind_seed_ = 1;
    std::array<sim::Surface, sim::kBlindTrials> blind_series_{};
};

} // namespace rally::platform
