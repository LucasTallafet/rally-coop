#pragma once

// Estado del coche que viaja del host al copiloto (57 B, protocolo 3, lote 6 §2.2; 50 B en los
// protocolos 1 y 2) y su codificación. Sin ENet.

#include "sim/sim_world.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace rally::net {

inline constexpr std::size_t kCarStateBytes = 57;
inline constexpr std::uint8_t kMsgCarState = 1;

// Decodificado. Posición del cuerpo del coche (SimWorld::car_pose) respecto al origen del mundo.
struct CarState {
    std::uint8_t generation = 0;
    std::uint32_t step = 0; // pasos desde el último build() (0 = recién construido)
    double x = 0, y = 0, z = 0;
    double qx = 0, qy = 0, qz = 0, qw = 1;
    float vx = 0, vy = 0, vz = 0;                         // m/s
    std::array<float, 4> compression{};                   // m, ruedas FL, FR, RL, RR
    std::array<std::int8_t, 4> surface{{-1, -1, -1, -1}}; // sim::Surface; -1 en el aire
    std::int8_t gear = 0;
    float rpm = 0;
    float steer = 0;     // dirección aplicada, [-1, 1], + derecha (CarTelemetry::steer)
    float boost_bar = 0; // presión del turbo sobre la atmosférica, bar
    // Recolocaciones desde el build() (módulo 256). Dos estados con contadores distintos no se
    // mezclan nunca, se hayan perdido los paquetes de en medio o no.
    std::uint8_t reset_count = 0;
    // Del StageRun del host (SimWorld::stage_run), nunca de una proyección del copiloto: las
    // notas se anclan a esta s. Sin tramo, los tres valen 0.
    double s = 0;         // m a lo largo del eje
    double t = 0;         // m, lateral
    bool on_road = false; // StageRunState::on_road
};

// Solo lee el mundo. Necesita la telemetría encendida (RALLY_CHECK): el juego la enciende en
// cada reset().
CarState car_state_from(const sim::SimWorld& world, std::uint32_t step, std::uint8_t generation,
                        std::uint8_t reset_count);

void encode(const CarState& state, std::span<std::uint8_t, kCarStateBytes> out);
// false si el tamaño, el tipo, la componente omitida, una superficie o un bit reservado de los
// indicadores (todos menos el 0) no cuadran.
bool decode(std::span<const std::uint8_t> in, CarState& out);

} // namespace rally::net
