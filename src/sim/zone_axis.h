#pragma once

#include <vector>

namespace rally::sim {

// Ejes de las calzadas de la zona de pruebas (fase 2): rectas y arcos encadenados. Ejes del
// mundo como en sim/test_zone.h: x al este, z al sur; rumbo psi 0 hacia -z, positivo a la
// derecha.

// Tramo del eje de una calzada: recta o arco de circunferencia.
struct AxisSegment {
    double x0 = 0.0; // inicio
    double z0 = 0.0;
    double psi0 = 0.0;   // rumbo al inicio
    double length = 0.0; // a lo largo del eje
    double radius = 0.0; // 0 en las rectas
    double turn = 0.0;   // giro total con signo (positivo a la derecha); 0 en las rectas
    double cx = 0.0;     // centro del arco
    double cz = 0.0;
    // Caja del tramo (solo el eje), para no calcular la distancia a los tramos lejanos.
    double min_x = 0.0;
    double max_x = 0.0;
    double min_z = 0.0;
    double max_z = 0.0;
};

struct AxisPoint {
    double x = 0.0;
    double z = 0.0;
    double psi = 0.0;
};

// Constructor de ejes: encadena rectas y arcos desde una posición y un rumbo.
class AxisBuilder {
public:
    AxisBuilder(double x, double z, double psi) : x_(x), z_(z), psi_(psi) {}

    void straight(double length);
    // turn > 0 gira a la derecha.
    void arc(double radius, double turn);

    double x() const { return x_; }
    double z() const { return z_; }
    std::vector<AxisSegment> take();

private:
    double x_;
    double z_;
    double psi_;
    std::vector<AxisSegment> segments_;
};

// Punto del tramo a una distancia s de su inicio (0 ≤ s ≤ length).
AxisPoint segment_point(const AxisSegment& seg, double s);
// Distancia de (x, z) al tramo (al punto más cercano, extremos incluidos).
double segment_distance(const AxisSegment& seg, double x, double z);
// Caja de cada tramo por muestreo del eje cada metro (y los extremos), con 1 cm de holgura.
void fit_segment_boxes(std::vector<AxisSegment>& axis);
// Ángulo llevado a [low, low + 2π).
double wrap_angle(double a, double low);

} // namespace rally::sim
