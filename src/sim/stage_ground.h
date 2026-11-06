#pragma once

#include "sim/stage_track.h"
#include "sim/surface.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <vector>

namespace rally::sim {

// Geometría de colisión del tramo (tarea tramo-fisica, lote 4; mensajes/lote-4.md §2.6 y
// docs/02-arquitectura.md §6.6), sin Jolt: la usan el suelo de Jolt de SimWorld y, en el lote 5,
// el dibujo, para que lo que se ve sea lo que se pisa. Todo se calcula al cargar, en orden fijo,
// con rmath y sin hilos.
//
// - Núcleo (malla): calzada y arcén, 5 vértices por sección (borde exterior del arcén, borde de
//   la calzada y eje, a cada lado), un plano por sección con el peralte. Secciones en cada
//   StageAxis::breaks(), en las prolongaciones y, entre ellas, con el paso más largo que da un
//   error de cuerda ≤ kStageChordPlan en planta y ≤ kStageChordHeight en alzado (también el
//   alabeo del centro de cada cuadrilátero), y ≤ kStageMeshMaxStep.
// - Terreno del formato 1 (rejilla en baldosas de kStageTileSamples² muestras que comparten las
//   del borde): cada muestra junta los pies perpendiculares que tiene en el eje (un abanico por
//   segmento de ≤ 0,5 m y una semicircunferencia en cada extremo). Fuera de los núcleos, la
//   media de las alturas de los bordes cercanos − kStageTerrainDrop
//   con peso c·ψ·φ/d⁸ (d: distancia al borde del núcleo; c = clamp(1 − κ·t, 0, 1), con las dos
//   curvaturas en una junta donde salta, que se anula donde un pie nace o muere; ψ, que se anula
//   a kStageFanReach del eje; φ, que se anula si el borde está 16 m más lejos que el más
//   cercano). Si todos los pesos son 0 (el centro de un arco), la altura del pie más cercano.
//   Ese es el terreno natural N. Junto a cada núcleo (tercera respuesta del consultor, §1), con
//   d la distancia al borde del núcleo (+ fuera, − dentro, también hacia los extremos del
//   tramo) y S la superficie del eje prolongada con el peralte (y la pendiente más allá de los
//   extremos), cada muestra toma, sin mirar a qué celda pertenece:
//     d < −kStageEdgeBand              S − kStageCoreDrop (siempre bajo la malla);
//     |d| ≤ kStageEdgeBand             B = S − kStageTerrainDrop − e(s) (StageCellError);
//     kStageEdgeBand < d < +D          B + smoothstep((d − kStageEdgeBand)/D)·(N − B);
//     más allá                         N;
//   con D = kStageEdgeBlend. Dentro de un núcleo manda el suyo; fuera de todos, si una muestra
//   está en la zona de varias pasadas, su combinación continua (cuarta respuesta del consultor,
//   §1): la media de sus alturas con peso (1 − paso suave)/d⁸, que se anula al salir de la
//   zona, y como mucho la banda de cada una junto a su borde (la rejilla no asoma).
//   Solo tienen colisión las muestras a menos de kStageStripCollision del eje.

inline constexpr double kStageTerrainDrop = 0.005; // terreno bajo el plano del núcleo y del borde
inline constexpr double kStageCoreDrop = 0.10;     // rejilla bajo el núcleo, lejos del borde (m)
inline constexpr double kStageEdgeBand = 1.0607;   // d₁: la diagonal de una celda de 0,75 m (m)
// D: empalme de la banda al terreno natural (m). 4 m y no 3: con 3 m la ondulación a lo largo
// de s pasaba de 5 mm (tercera respuesta del consultor, §2).
inline constexpr double kStageEdgeBlend = 4.0;
inline constexpr double kStageMeshMaxStep = 2.0;   // paso máximo entre secciones (m)
inline constexpr double kStageChordPlan = 0.01;    // error de cuerda en planta (m)
inline constexpr double kStageChordHeight = 0.002; // error de cuerda y alabeo en alzado (m)
inline constexpr double kStageFanStep = 0.5;       // segmento máximo de los abanicos (m)
// Los abanicos llegan hasta kStageFanReach del eje y ψ cae de 1 a 0 en los últimos kStageFanFade.
// Llegan 3,5 m más allá de la colisión (kStageStripCollision): el peso relativo de dos pies que se
// desvanecen a la vez cambia como 1/(kStageFanReach − distancia), y así no hay taludes casi
// verticales en el borde de la franja.
inline constexpr double kStageFanReach = 64.0;
inline constexpr double kStageFanFade = 6.0;
// La rejilla tiene colisión en las muestras a < esto del eje (m). Una celda (dos triángulos)
// tiene colisión si sus cuatro esquinas la tienen, y su diagonal es de 1,06 m con 0,75 m: hay
// suelo siempre hasta 60,5 − 1,06 = 59,44 m del eje y nunca a partir de 60,5 m.
inline constexpr double kStageStripCollision = kStageStripHalfWidth + 0.5;
inline constexpr int kStageTileSamples = 64; // muestras por lado de una baldosa
inline constexpr int kStageTileCells = kStageTileSamples - 1;
inline constexpr double kStageCell = 0.75; // celda de la rejilla del diseño (m)
inline constexpr float kStageNoCollision = std::numeric_limits<float>::max();
inline constexpr int kStageSectionVertices = 5;

// Densidad de la malla del núcleo. La del juego son las constantes de arriba; la referencia del
// criterio 3 en curva (respuesta del consultor a tramo-fisica, §2) es 4 veces más densa.
struct StageMeshDensity {
    double max_step = kStageMeshMaxStep;
    double chord_plan = kStageChordPlan;
    double chord_height = kStageChordHeight;
};
inline constexpr StageMeshDensity kStageDenseMesh{0.5, 0.0025, 0.0005};

// Design: núcleo en malla y terreno en baldosas (§2.6). GridOnly: todo en rejilla, el núcleo con
// su superficie por celda y sin bajada; solo para la medida R de la parte A. NaturalOutside: como
// Design, pero fuera de los núcleos el terreno natural N sin la banda ni el empalme; solo para el
// diagnóstico del talud en los tests (quinta respuesta del consultor a tramo-fisica).
enum class StageGroundKind : std::uint8_t { Design, GridOnly, NaturalOutside };

// Vértice respecto al origen del suelo (StageGround::origin_*).
struct StageVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
};

// Triángulo con la normal hacia arriba (a, b, c en sentido antihorario visto desde arriba).
struct StageTriangle {
    std::uint32_t a = 0;
    std::uint32_t b = 0;
    std::uint32_t c = 0;
    Surface surface = Surface::Gravel;
};

// Baldosa (tx, tz): sus muestras son las globales (tx·63 + i, tz·63 + k), i, k en [0, 64), en
// x = origin_x + (tx·63 + i)·cell y z = origin_z + (tz·63 + k)·cell. Las de su borde son las
// mismas que las de la baldosa vecina.
struct StageTile {
    std::int32_t tx = 0;
    std::int32_t tz = 0;
    // kStageTileSamples² alturas respecto a origin_y, fila a fila (índice k·64 + i);
    // kStageNoCollision donde no hay colisión.
    std::vector<float> heights;
    // Solo GridOnly: kStageTileCells² superficies (la del centro de cada celda), fila a fila.
    std::vector<std::uint8_t> surfaces;
};

class TestZone;

// --- Microrrelieve del tramo (MicroRelief::Stage; respuesta del consultor a tramo-fisica, §1.1) --
//
// La h de cada superficie es su banda de ruido (TestZone::surface_noise, en x, z del mundo), como
// en las losas de los tests, pero mezclada linealmente en una franja de kStageReliefBlend centrada
// en cada borde: calzada–arcén en ±semiancho, arcén–terreno en ±(semiancho + arcén) y, a lo largo
// de s, en cada cambio de [surface]. La rueda proyecta su contacto una vez por paso
// (stage_relief_frame) y sus 9 muestras de la huella usan esa proyección más su desplazamiento.
// Sin reservas. El dibujo del lote 5 usa las mismas funciones.

inline constexpr double kStageReliefBlend = 0.5; // anchura de la franja de mezcla (m)

struct StageReliefFrame {
    double s = 0.0; // proyección del punto en el eje
    double t = 0.0;
    double tx = 0.0; // avance del eje en s (sin θ, −cos θ)
    double tz = -1.0;
    double half_left = 0.0; // semianchos de la calzada en s
    double half_right = 0.0;
    double surface_break = 0.0; // s del cambio de [surface] más cercano (+∞ sin cambios)
    Surface before = Surface::Gravel;
    Surface after = Surface::Gravel;
};

// Marco de (x, z): su proyección local en el eje cerca de s_hint (la circunferencia osculatriz de
// la pose en s_hint: exacta en rectas y arcos) y, si el punto queda fuera del núcleo de esa pasada
// y de su franja de mezcla y está en la calzada, el arcén o la franja de mezcla de otra, la de esa.
StageReliefFrame stage_relief_frame(const StageTrack& track, double s_hint, double x, double z);
// h en el punto (x, z) que está en (s, t) del marco.
double stage_relief_height(const StageReliefFrame& frame, const TestZone& noise, double s, double t,
                           double x, double z);

// e(s): cota del error de interpolar S en una celda de la rejilla de kStageCell junto al borde
// (tercera respuesta del consultor, §1), con el eje: ½·M·R², con M la norma de la hessiana de S
// en planta (curvatura, su variación, pendiente, curva vertical y rampa del peralte) en la banda
// del lado del núcleo y R = kStageCell/√2 el radio de la circunferencia circunscrita a medio
// cuadrado; y, en cada junta del eje donde salta la pendiente de S a lo largo de s (pendiente o
// rampa de peralte), J·kStageEdgeBand/4. Se muestrea cada kStageCellErrorStep y se suaviza con
// un máximo en meseta de ±kStageCellErrorPlateau y rampa de kStageCellErrorRamp: continua en s.
inline constexpr double kStageCellErrorStep = 0.25;
inline constexpr double kStageCellErrorPlateau = 2.0;
inline constexpr double kStageCellErrorRamp = 2.0;

class StageCellError {
public:
    StageCellError() = default;
    // Reserva memoria y tarda: al cargar.
    explicit StageCellError(const StageTrack& track);
    // e(s), con s en [s_begin, s_end] (fuera, el del extremo). Sin reservas.
    double at(double s) const;
    // Sin suavizar, en la muestra más cercana (solo para los tests).
    double raw_at(double s) const;

private:
    double s0_ = 0.0;
    std::vector<double> raw_;
    std::vector<double> e_;
};

// Una muestra con colisión en el centro de curvatura de un arco (a menos de
// kStageArcCenterTolerance del vértice de sus cuñas) con más pies de los que guarda: un punto
// singular del terreno del formato 1, donde todos los abanicos del arco tienen un pie a la misma
// distancia (respuesta del consultor a la parada de tramo-ajustes, lote 5). Solo diagnóstico.
struct StageCenterOverflow {
    double x = 0.0, z = 0.0;    // en el mundo
    double distance = 0.0;      // en planta, al vértice de las cuñas del arco (m)
    double height = 0.0;        // altura de la muestra, en el mundo
    double foot_min = 0.0;      // la menor y la mayor altura de sus pies (con peso y en zona)
    double foot_max = 0.0;
};

// Distancia en planta al centro de curvatura de un arco por debajo de la cual un desborde de pies
// se cuenta en StageGround::center_overflows() y no en candidate_overflows() (1 cm; el desborde
// del centro ocurre a ~1e-9 m del vértice).
inline constexpr double kStageArcCenterTolerance = 0.01;

class StageGround {
public:
    // `cell`: celda de la rejilla (m). Reserva memoria y tarda: al cargar.
    static StageGround build(const StageTrack& track, double cell = kStageCell,
                             StageGroundKind kind = StageGroundKind::Design,
                             const StageMeshDensity& density = StageMeshDensity{});

    // Origen del suelo: el centro de la caja del eje (con las prolongaciones), redondeado al
    // metro. Los vértices y las alturas van en float respecto a él.
    double origin_x() const { return origin_x_; }
    double origin_y() const { return origin_y_; }
    double origin_z() const { return origin_z_; }
    double cell() const { return cell_; }
    StageGroundKind kind() const { return kind_; }

    // s de cada sección del núcleo (vacío en GridOnly); sus vértices son los 5 que empiezan en
    // kStageSectionVertices · índice, de izquierda a derecha.
    const std::vector<double>& sections() const { return sections_; }
    const std::vector<StageVertex>& vertices() const { return vertices_; }
    const std::vector<StageTriangle>& triangles() const { return triangles_; }
    // Baldosas con alguna muestra con colisión, en orden de (tz, tx).
    const std::vector<StageTile>& tiles() const { return tiles_; }
    // Muestras de las baldosas (con y sin colisión).
    std::size_t sample_count() const {
        return tiles_.size() * kStageTileSamples * kStageTileSamples;
    }
    // Muestras con colisión que no tenían ningún peso y tomaron la altura del pie más cercano.
    std::size_t nearest_fallbacks() const { return nearest_fallbacks_; }
    // Muestras con colisión que tenían más pies con peso de los que guardan (se quedan los más
    // cercanos a su borde). Si alguna vez no es 0, la media de esas muestras no es continua. No
    // cuenta las del centro de curvatura de un arco: van en center_overflows().
    std::size_t candidate_overflows() const { return candidate_overflows_; }
    // Las muestras que desbordan en el centro de curvatura de un arco, en el orden de las
    // baldosas. No cambian ninguna altura: solo se apartan de candidate_overflows().
    const std::vector<StageCenterOverflow>& center_overflows() const { return center_overflows_; }
    // Diagnóstico (cuarta respuesta del consultor, §1): el mayor salto de la combinación de
    // varias pasadas cuando un pie sale de su zona, medido en su último centímetro (m), y dónde.
    double zone_jump() const { return zone_jump_; }
    double zone_jump_x() const { return zone_jump_x_; }
    double zone_jump_z() const { return zone_jump_z_; }

    // FNV-1a de la geometría de colisión: origen, celda, tipo, vértices, triángulos con su
    // superficie y baldosas con sus muestras (y superficies), campo a campo.
    std::uint64_t hash() const;

private:
    double origin_x_ = 0.0;
    double origin_y_ = 0.0;
    double origin_z_ = 0.0;
    double cell_ = kStageCell;
    StageGroundKind kind_ = StageGroundKind::Design;
    std::vector<double> sections_;
    std::vector<StageVertex> vertices_;
    std::vector<StageTriangle> triangles_;
    std::vector<StageTile> tiles_;
    std::size_t nearest_fallbacks_ = 0;
    std::size_t candidate_overflows_ = 0;
    std::vector<StageCenterOverflow> center_overflows_;
    double zone_jump_ = 0.0;
    double zone_jump_x_ = 0.0;
    double zone_jump_z_ = 0.0;
};

} // namespace rally::sim
