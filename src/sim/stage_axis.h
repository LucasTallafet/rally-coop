#pragma once

#include "sim/surface.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace rally::sim {

// Eje de un tramo escrito en el formato de tramo 1 (.stage; mensajes/lote-3.md §2.3 y
// docs/02-arquitectura.md §6.1): planta por primitivas (recta, arco y clotoide), alzado por PIV
// con curvas verticales parabólicas, peralte y semianchos lineales entre puntos y superficie por
// tramos. Es el esqueleto del tramo: sin irregularidad, sin Jolt y sin dibujo.
//
// Ejes del mundo como en sim/zone_axis.h: x al este, z al sur, y arriba; rumbo 0 hacia −z,
// positivo a la derecha; curvatura y R positivos a la derecha. Todo se calcula al cargar; pose()
// solo busca e integra, y no reserva memoria.

inline constexpr int kStageFormatVersion = 1;

// Punto del eje a una distancia s de la salida.
struct StagePose {
    double s = 0.0;
    double x = 0.0, y = 0.0, z = 0.0; // y: altura del eje
    // Rumbo (rad), convención de zone_axis.h. Es la integral de la curvatura desde el rumbo de
    // salida: continuo en todo el tramo, sin envolver a [0, 2π).
    double heading = 0.0;
    double curvature = 0.0;          // 1/m, + a la derecha
    double grade = 0.0;              // dy/ds
    double vertical_curvature = 0.0; // d²y/ds²
    double bank = 0.0;               // dy/dt de la calzada (tanto por uno), + sube hacia la derecha
    double half_width_left = 0.0;
    double half_width_right = 0.0;
    Surface surface = Surface::Gravel;
};

// Primitiva de la planta: la curvatura va lineal de curvature0 a curvature1 en length metros.
// Recta: las dos 0; arco: las dos 1/R.
struct StagePrimitive {
    double s0 = 0.0;
    double length = 0.0;
    double curvature0 = 0.0;
    double curvature1 = 0.0;
};

struct StageParseResult {
    bool ok = false;
    std::string error; // "línea N: ..." si !ok
};

class StageAxis {
public:
    // Lee el texto de un .stage. Un error (con su número de línea) deja `out` como estaba.
    static StageParseResult parse(std::string_view text, StageAxis& out);

    double length() const { return length_; }
    // s se acota a [0, length()] (NaN da 0). Sin reservas de memoria.
    StagePose pose(double s) const;
    const std::vector<StagePrimitive>& primitives() const { return prims_; }
    // FNV-1a de los datos leídos, campo a campo (todo menos el nombre): el mismo tramo da el
    // mismo hash en cualquier PC.
    std::uint64_t hash() const;
    const std::string& name() const { return name_; }

private:
    // Inicio de cada primitiva y, en las clotoides, sus nodos de posición.
    struct PlanStart {
        double x0 = 0.0;
        double z0 = 0.0;
        double heading0 = 0.0;
        double radius = 0.0; // arcos: R con signo; 0 en el resto
        double cx = 0.0;     // arcos: centro
        double cz = 0.0;
        std::size_t node_first = 0; // clotoides: primer nodo en node_x_ / node_z_
        std::size_t node_count = 0; // clotoides: número de intervalos (nodos = node_count + 1)
        double node_step = 0.0;     // clotoides: metros entre nodos (≤ 1)
    };
    struct PlanPoint {
        double x = 0.0;
        double z = 0.0;
        double heading = 0.0;
        double curvature = 0.0;
    };
    struct Pvi {
        double s = 0.0;
        double height = 0.0;
        double curve_length = 0.0;
        double grade_after = 0.0; // pendiente hasta el PIV siguiente (en el último, la anterior)
    };
    struct BankPoint {
        double s = 0.0;
        double value = 0.0; // tanto por uno
    };
    struct WidthPoint {
        double s = 0.0;
        double left = 0.0;
        double right = 0.0;
    };
    struct SurfacePoint {
        double s = 0.0;
        Surface surface = Surface::Gravel;
    };

    // Primitiva i a u metros de su inicio (u acotada a [0, L]).
    PlanPoint plan_point(std::size_t i, double u) const;
    // Calcula los inicios de las primitivas, sus nodos y las pendientes del alzado.
    void build();

    std::string name_;
    double start_x_ = 0.0;
    double start_z_ = 0.0;
    double start_heading_ = 0.0; // rad
    double start_height_ = 0.0;
    double length_ = 0.0;
    std::vector<StagePrimitive> prims_;
    std::vector<PlanStart> starts_;
    std::vector<double> node_x_;
    std::vector<double> node_z_;
    std::vector<Pvi> pvi_;
    std::vector<BankPoint> bank_;
    std::vector<WidthPoint> width_;
    std::vector<SurfacePoint> surface_;
};

} // namespace rally::sim
