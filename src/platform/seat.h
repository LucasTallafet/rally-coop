#pragma once

#include "platform/interpolation.h"
#include "sim/car_data.h"

namespace rally::platform {

// Asiento del jugador: desplazamiento del ojo del conductor respecto al del archivo del coche,
// en centímetros enteros y en ejes del coche. Solo mueve la cámara del ojo: no entra en la
// simulación ni en la grabación (se guarda en ajustes.txt, platform/player_settings).
inline constexpr int kSeatLimitCm = 15;

struct SeatOffsetCm {
    int up = 0;      // positivo hacia arriba
    int forward = 0; // positivo hacia delante
    int right = 0;   // positivo hacia la derecha
    bool operator==(const SeatOffsetCm&) const = default;
};

enum class SeatAxis { Up, Forward, Right };

// El asiento con delta_cm más en un eje, recortado a ± kSeatLimitCm.
SeatOffsetCm nudge_seat(SeatOffsetCm seat, SeatAxis axis, int delta_cm);
bool seat_in_limits(int cm);

// Ejes del coche: x a la derecha, y arriba, −z hacia delante, con el origen en el suelo bajo el
// centro de la batalla.
// Ojo del archivo del coche: a eye_left a la izquierda, a eye_height del suelo y a
// eye_behind_front_axle detrás del eje delantero.
Vec3d car_eye(const sim::CarData& car);
// Ojo del conductor = ojo del archivo + asiento. La cabeza de la 2.7 se sumará a esto.
Vec3d driver_eye(const sim::CarData& car, const SeatOffsetCm& seat);

} // namespace rally::platform
