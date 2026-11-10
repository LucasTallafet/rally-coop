#pragma once

#include "sim/stage_axis.h"

#include <vector>

namespace rally::game {

// Cambios de dirección del eje del tramo (docs/01 §4 y §5.2): la cinta de ruta y el cursor de la
// tablet los usan. El criterio es solo geométrico y no dice qué curvas merecen nota: un cambio
// es un tramo de curvatura de un solo signo. Dos curvas del mismo sentido con una recta en medio
// son dos cambios; una clotoide de inflexión da dos, uno a cada lado de donde la curvatura pasa
// por 0.

// 5° en radianes (π/180 · 5).
inline constexpr double kDirectionChangeMinAngle = 5.0 * 3.14159265358979323846 / 180.0;

struct DirectionChange {
    double s_start; // m: donde empieza (la curvatura sale de 0 o cambia de signo)
    double s_end;   // m: donde acaba
    double angle;   // rad, con signo: + derecha (como el rumbo y R en el .stage)
};

// Función pura del esqueleto, calculada al cargar: reserva memoria. Ordenados por s_start; solo
// los de |angle| ≥ kDirectionChangeMinAngle.
std::vector<DirectionChange> direction_changes(const sim::StageAxis& axis);

// Sin reservas. El primero con s_start > s, o −1 si no hay.
int next_direction_change(const std::vector<DirectionChange>& changes, double s);
// Sin reservas. El último con s_start < s, o −1 si no hay.
int previous_direction_change(const std::vector<DirectionChange>& changes, double s);

} // namespace rally::game
