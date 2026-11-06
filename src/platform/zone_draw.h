#pragma once

#include "platform/camera_space.h"
#include "platform/draw_time_stats.h"
#include "platform/near_grid.h"
#include "platform/tree_instances.h"
#include "sim/test_zone.h"

#include <raylib.h>

#include <cstdint>
#include <memory>
#include <vector>

namespace rally::platform {

// Dibujo de la zona de pruebas (tarea 2.2), en colores planos por superficie (01 §4).
//
// - Lejos: mallas estáticas construidas al construir el mundo. El fondo de tierra y las
//   franjas con relieve (baches, rampa y cresta, con la rejilla de 0,75 m del suelo de Jolt),
//   las áreas llanas encima con desplazamiento de polígonos (sin parpadeo a ninguna
//   distancia) y las líneas pintadas encima de todo, levantadas 3 cm.
// - Cerca (a menos de kNearRadius del ojo): una rejilla de kNearSpacing con la geometría más
//   el microrrelieve h, la misma función que usará la rueda, calculada en la CPU. Geometría,
//   h y superficie de cada vértice viven en una textura flotante anclada al mundo y
//   direccionada en anillo (índice del mundo módulo el tamaño): cada fotograma solo calcula y
//   sube las filas y columnas que entran en la rejilla. La rejilla es una malla fija en la
//   GPU; el shader de vértices lee su altura, apaga h entre kNearFadeStart y kNearRadius (el
//   borde no tiene escalón) y la sombrea con la normal de sus vecinos.
// - Un shader recorta cada parte por la distancia horizontal al ojo: lo lejano no se dibuja
//   dentro del círculo y lo cercano no se dibuja fuera.
// - Modo a ciegas: todas las superficies del mismo color neutro y sin h (ni rejilla cercana):
//   solo quedan la geometría y las líneas pintadas.
// - Carretera de prueba (2.6b): su caja sale del fondo llano y se dibuja por filas: las celdas
//   con relieve o con otra superficie que la tierra, con la rejilla de Jolt y el color de la
//   superficie de su centro (calzada, arcén, cuneta y talud); las demás, en tiras llanas. Líneas
//   de salida y meta y, en el modo a ciegas, los bordes de la calzada, como la pista ciega. Los
//   árboles (cuerpos de Jolt) van instanciados con su VBO persistente (platform/tree_instances).
// Los conos y los postes solo se dibujan: no son cuerpos de la simulación.
class ZoneDraw {
public:
    static constexpr double kNearRadius = 30.0;
    static constexpr double kNearFadeStart = 27.0;
    static constexpr double kNearSpacing = 0.25;

    // Necesita la ventana abierta (carga el shader).
    ZoneDraw();
    ~ZoneDraw();
    ZoneDraw(const ZoneDraw&) = delete;
    ZoneDraw& operator=(const ZoneDraw&) = delete;

    // Construye las mallas para `zone`. La zona tiene que vivir mientras se dibuje.
    void build(const sim::TestZone& zone, bool blind);
    bool built() const { return zone_ != nullptr; }

    // Dibuja el suelo, las líneas, los conos y los postes, dentro de BeginMode3D. Todo se
    // dibuja relativo a `origin` (el ojo, o 0 con el dibujo en float del mundo); `eye` es el
    // ojo en el mundo.
    void draw(const Vec3d& eye, const Vec3d& origin);

    // CPU del último draw() y estadísticas de los últimos kStatsFrames fotogramas, en ms.
    static constexpr int kStatsFrames = DrawTimeStats::kFrames;
    double last_cpu_ms() const { return stats_.last(); }
    // Desglose del último draw(): la rejilla cercana (celdas calculadas, subidas a la textura
    // y su tiempo) frente al total. Para --bench-zone.
    struct Breakdown {
        double near_ms = 0.0;   // update_near entero
        double upload_ms = 0.0; // dentro de él, rlUpdateTexture
        int cells = 0;
        int uploads = 0;
    };
    const Breakdown& last_breakdown() const { return breakdown_; }
    double mean_cpu_ms() const { return stats_.mean(); }
    double max_cpu_ms() const { return stats_.max(); }
    // Media y máximo desde reset_totals().
    void reset_totals() { stats_.reset_totals(); }
    double total_mean_ms() const { return stats_.total_mean(); }
    double total_max_ms() const { return stats_.total_max(); }

private:
    void unload_meshes();
    void draw_near(const Vec3d& eye, const Vec3d& origin);
    void draw_props(const Vec3d& origin) const;

    const sim::TestZone* zone_ = nullptr;
    bool blind_ = false;
    ClipShader clip_;
    PolygonOffset offset_;

    // Capas lejanas: suelo (fondo y franjas con relieve), áreas llanas y líneas pintadas.
    std::vector<Mesh> ground_;
    std::vector<Mesh> decals_;
    std::vector<Mesh> lines_;

    // Rejilla cercana (platform/near_grid): geometría, h y superficie en una textura en anillo.
    NearGrid near_;

    // Conos y postes.
    std::vector<sim::AxisPoint> cones_;
    std::vector<Vec3d> posts_;
    // Árboles de la carretera, relativos a trees_center_.
    std::unique_ptr<TreeInstances> trees_;
    Vec3d trees_center_{};

    Breakdown breakdown_;
    DrawTimeStats stats_;
};

// Color plano de una superficie (01 §4) y el neutro del modo a ciegas.
Color surface_color(sim::Surface surface);
inline constexpr Color kBlindColor{150, 144, 132, 255};

} // namespace rally::platform
