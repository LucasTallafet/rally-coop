#pragma once

#include "platform/camera_space.h"
#include "sim/test_zone.h"

#include <raylib.h>

#include <cstdint>
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
    static constexpr int kStatsFrames = 240;
    double last_cpu_ms() const { return last_ms_; }
    // Desglose del último draw(): la rejilla cercana (celdas calculadas, subidas a la textura
    // y su tiempo) frente al total. Para --bench-zone.
    struct Breakdown {
        double near_ms = 0.0;   // update_near entero
        double upload_ms = 0.0; // dentro de él, rlUpdateTexture
        int cells = 0;
        int uploads = 0;
    };
    const Breakdown& last_breakdown() const { return breakdown_; }
    double mean_cpu_ms() const { return mean_ms_; }
    double max_cpu_ms() const { return max_ms_; }
    // Media y máximo desde reset_totals().
    void reset_totals();
    double total_mean_ms() const;
    double total_max_ms() const { return total_max_ms_; }

private:
    void unload_meshes();
    void update_near(const Vec3d& eye);
    void draw_mesh(const Mesh& mesh, const Vec3d& origin, float radius, float side,
                   const Vec3d& eye);
    void draw_near(const Vec3d& eye, const Vec3d& origin);
    void draw_props(const Vec3d& origin) const;
    void record_time(double ms);

    const sim::TestZone* zone_ = nullptr;
    bool blind_ = false;
    Shader shader_{};
    Material material_{};
    int loc_radius_ = -1;
    int loc_side_ = -1;
    int loc_eye_ = -1;

    // Capas lejanas: suelo (fondo y franjas con relieve), áreas llanas y líneas pintadas.
    std::vector<Mesh> ground_;
    std::vector<Mesh> decals_;
    std::vector<Mesh> lines_;

    // Rejilla cercana: near_cells_² vértices fijos y su textura RGBA32F (geometría, h,
    // superficie) en anillo.
    int near_cells_ = 0;
    Mesh near_{};
    Shader near_shader_{};
    Material near_material_{};
    unsigned int near_texture_ = 0;
    int loc_near_anchor_ = -1;
    int loc_near_eye_ = -1;
    std::vector<float> texels_; // 4 por celda, en el orden de la textura
    std::vector<float> column_; // una columna de la textura, para subirla
    bool near_valid_ = false;
    std::int64_t anchor_x_ = 0; // índice de la rejilla del mundo del vértice (0, 0)
    std::int64_t anchor_z_ = 0;

    // Conos y postes.
    std::vector<sim::AxisPoint> cones_;
    std::vector<Vec3d> posts_;

    double last_ms_ = 0.0;
    Breakdown breakdown_;
    double mean_ms_ = 0.0;
    double max_ms_ = 0.0;
    std::vector<double> history_;
    std::size_t history_next_ = 0;
    double total_ms_ = 0.0;
    std::int64_t total_frames_ = 0;
    double total_max_ms_ = 0.0;
};

// Color plano de una superficie (01 §4) y el neutro del modo a ciegas.
Color surface_color(sim::Surface surface);
inline constexpr Color kBlindColor{150, 144, 132, 255};

} // namespace rally::platform
