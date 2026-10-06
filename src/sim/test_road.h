#pragma once

#include "sim/zone_axis.h"

#include <cstdint>
#include <vector>

namespace rally::sim {

// Carretera de prueba de la zona (tarea 2.6b): ~1 km de punto a punto, estrecha, con arcén de
// arena, cuneta, taludes y árboles, construida en código a partir de las constantes de este
// archivo. Es una pieza más de la zona de pruebas (sim/test_zone.h), no el formato de tramo de
// la fase 3: no hay primitivas en archivo, ni irregularidad, ni seguimiento s/t en el juego.
//
// La s de la carretera se mide en el eje desde la línea de salida (s = 0) hasta la de meta
// (s = length()); el eje empieza kRoadLeadIn antes de la salida y acaba kRoadRunOff después de
// la meta. El lateral t es positivo a la derecha del sentido de la marcha.
//
// Sección (de dentro afuera, a cada lado): calzada, arcén de arena (siguen el plano del
// peralte), cuneta en U de tierra (fondo plano y laderas en coseno, bajo ese plano), berma y
// talud hasta la altura 0 de la zona, que acaba a kRoadFootprint metros del eje (30 m del borde
// de la cuneta). Junto a la horquilla, la bajada y la subida se juntan (TestRoad::height). Los
// anchos son los mínimos que la rejilla de 0,75 m del suelo representa en cualquier ángulo
// (hallazgos de la 2.6b): arena de al menos una celda en la normal y cuneta con más del 85 % de su
// fondo.
namespace zone {

// Eje: salida en (kRoadStartX, kRoadStartZ) mirando al norte.
inline constexpr double kRoadStartX = -390.0;
inline constexpr double kRoadStartZ = 300.0;
inline constexpr double kRoadLeadIn = 20.0;  // recta antes de la salida (sin cronometrar)
inline constexpr double kRoadRunOff = 100.0; // frenada tras la meta (sin cronometrar)
// Sección, desde el eje.
inline constexpr double kRoadHalfWidth = 3.0;  // calzada de 6 m
inline constexpr double kShoulderWidth = 1.6;  // arcén de arena
inline constexpr double kDitchFlat = 0.75;     // fondo plano de la cuneta
inline constexpr double kDitchSide = 1.25;     // cada ladera (coseno)
inline constexpr double kDitchDepth = 0.4;     // bajo el plano de la calzada
inline constexpr double kRoadBerm = 1.0;       // llano tras la cuneta
inline constexpr double kRoadTalud = 28.0;     // tramo recto del talud
inline constexpr double kRoadTaludRound = 1.0; // redondeo parabólico en cada extremo
inline constexpr double kShoulderEdge = kRoadHalfWidth + kShoulderWidth;            // 4,6
inline constexpr double kDitchEdge = kShoulderEdge + kDitchFlat + 2.0 * kDitchSide; // 7,85
inline constexpr double kRoadFootprint =
    kDitchEdge + kRoadBerm + kRoadTalud + kRoadTaludRound; // 37,85: altura 0 desde aquí
// Altura del eje en la salida: centra el trazado en el 0 de la zona (+7,05 en la cresta, −7,05
// en la horquilla).
inline constexpr double kRoadBaseHeight = 5.55;
// Curvas verticales: parábola de esta longitud dentro de cada tramo con pendiente.
inline constexpr double kGradeTransition = 20.0;
inline constexpr double kDescentGrade = 0.07;
inline constexpr double kClimbGrade = 0.05;
// Cresta: altura y longitud; vaguadas parabólicas de kRoadCrestSag en cada extremo y cumbre
// parabólica entre ellas.
inline constexpr double kRoadCrestHeight = 1.5;
inline constexpr double kRoadCrestLength = 40.0;
inline constexpr double kRoadCrestSag = 6.0;
// Peralte (tanto por uno hacia el interior; negativo hacia fuera) y su transición lineal,
// centrada en cada extremo del arco.
inline constexpr double kBankTransition = 20.0;
// Líneas de salida y meta: margen a cada lado de la calzada.
inline constexpr double kRoadLineMargin = 1.0;
// Puntos de recolocación: cada kRoadRespawnSpacing desde la salida, y uno kRoadRespawnBack antes.
inline constexpr double kRoadRespawnSpacing = 100.0;
inline constexpr double kRoadRespawnBack = 10.0;
// Recolocar a menos de esta distancia del eje lleva hacia atrás por la carretera, nunca adelante.
inline constexpr double kRoadRespawnBand = 40.0;
// Árboles: cilindros estáticos de Jolt.
inline constexpr std::uint64_t kTreeSeed = 0x7e3e'5eed'2026'1006ull;
inline constexpr double kTreeRadius = 0.25;
inline constexpr double kTreeHalfHeight = 2.5;
inline constexpr double kTreeSink = 0.5;                             // la base, bajo el suelo
inline constexpr double kTreeInner = kDitchEdge + 0.5 + kTreeRadius; // 8,6 desde el eje
inline constexpr double kTreeBand = 5.0; // fila de kTreeInner a kTreeInner + kTreeBand
inline constexpr double kTreeGapMin = 6.0;
inline constexpr double kTreeGapMax = 15.0;
inline constexpr double kTreeDenseGapMin = 4.0;
inline constexpr double kTreeDenseGapMax = 8.0;
inline constexpr double kTreeDenseMargin = 10.0; // la zona densa sigue esto antes y después

} // namespace zone

// Punto del eje a una s de la carretera: posición, rumbo, altura del eje y peralte (pendiente
// dy/dt de la calzada; positiva si sube hacia la derecha).
struct RoadPoint {
    double s = 0.0;
    double x = 0.0;
    double z = 0.0;
    double psi = 0.0;
    double height = 0.0;
    double bank = 0.0;
};

// Árbol: centro en el suelo (y, la altura del suelo en su centro).
struct TreeSpot {
    double x = 0.0;
    double z = 0.0;
    double y = 0.0;
};

// Pieza del trazado, en el orden de la marcha.
enum class RoadPiece : std::uint8_t {
    LeadIn,
    Start,        // 1. recta de salida
    FastRight,    // 2. derecha rápida
    Straight80,   // 3.
    Tightening50, // 4. izquierda que se cierra: R 50...
    Tightening35, //    ...que pasa a R 35
    SRight,       // 5. S
    SLeft,
    Crest,     // 6. cresta
    CrestLeft, //    izquierda tras la cresta
    Descent,   // 7. bajada del 7 %
    Hairpin,   // 8. horquilla
    ClimbLeft, // 9. subida con izquierda
    Finish,    // 10. recta hasta la meta
    RunOff,
};
inline constexpr int kRoadPieceCount = 15;

struct RoadSegment {
    RoadPiece piece = RoadPiece::LeadIn;
    const char* key = "";
    double s0 = 0.0;   // s de la carretera al inicio del tramo
    double bank = 0.0; // peralte pleno hacia el interior (tanto por uno), 0 sin peralte
    AxisSegment axis;
};

// Tramo de la carretera con árboles más densos: lado (−1 izquierda, +1 derecha) y s.
struct DenseRange {
    int side = 0;
    double s0 = 0.0;
    double s1 = 0.0;
};

class TestRoad {
public:
    TestRoad();

    const std::vector<RoadSegment>& segments() const { return segments_; }
    // Los tramos del eje, para las áreas de la zona (calzada y arcén).
    std::vector<AxisSegment> axis() const;
    // Tramo que contiene s (el primero antes del eje, el último después).
    const RoadSegment& segment_at(double s) const;
    const RoadSegment& segment(RoadPiece piece) const {
        return segments_[static_cast<std::size_t>(piece)];
    }
    // De la línea de salida a la de meta, en el eje.
    double length() const { return length_; }
    double first_s() const { return -zone::kRoadLeadIn; }
    double last_s() const { return length_ + zone::kRoadRunOff; }

    RoadPoint point(double s) const;
    // Altura del eje (sin peralte) y peralte en s.
    double axis_height(double s) const;
    double bank(double s) const;

    // Altura del suelo de la carretera en (x, z): calzada, arcén, cuneta, berma y talud, y 0
    // exacto a kRoadFootprint o más del eje. Se suma a la geometría de la zona.
    double height(double x, double z) const;

    // s del punto del eje (muestreado cada metro) más cercano a (x, z) y su distancia.
    double nearest_s(double x, double z, double& distance) const;

    const std::vector<TreeSpot>& trees() const { return trees_; }
    const std::vector<DenseRange>& dense_ranges() const { return dense_; }

    // Caja del suelo con relieve: el eje más kRoadFootprint.
    double min_x() const { return min_x_; }
    double max_x() const { return max_x_; }
    double min_z() const { return min_z_; }
    double max_z() const { return max_z_; }

private:
    struct Candidate {
        double distance = 0.0;
        double s = 0.0;
        double t = 0.0;
        bool at_start = false; // el punto más cercano es el inicio del tramo
        bool at_end = false;   // ...o su final
    };
    Candidate project(const RoadSegment& seg, double x, double z) const;
    double section_height(double s, double t) const;
    void place_trees();

    std::vector<RoadSegment> segments_;
    double length_ = 0.0;
    std::vector<RoadPoint> samples_; // cada metro de first_s() a last_s()
    std::vector<TreeSpot> trees_;
    std::vector<DenseRange> dense_;
    double min_x_ = 0.0;
    double max_x_ = 0.0;
    double min_z_ = 0.0;
    double max_z_ = 0.0;
};

} // namespace rally::sim
