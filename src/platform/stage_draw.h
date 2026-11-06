#pragma once

#include "platform/camera_space.h"
#include "platform/draw_time_stats.h"
#include "platform/near_grid.h"
#include "platform/stage_chunks.h"
#include "platform/stage_mesh.h"
#include "platform/stage_near.h"
#include "platform/stage_surface.h"
#include "sim/stage_ground.h"
#include "sim/stage_track.h"
#include "sim/test_zone.h"

#include <raylib.h>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rally::platform {

// Dibujo del tramo (tarea tramo-dibujo, lote 5; mensajes/lote-5.md §2.6) con la geometría de la
// física: la StageGround que construye es la de SimWorld (misma función, mismos argumentos) y su
// hash se compara con el de la simulación.
//
// - Lejos (a más de ZoneDraw::kNearRadius del ojo): el terreno, una malla por baldosa, y el
//   núcleo, una malla por tramo de ~64 m de s, con un color por superficie; solo los trozos a
//   menos de kStageDrawDistance del ojo. El núcleo va con desplazamiento de polígonos: gana al
//   terreno, que está 5 mm bajo su borde. Las muestras sin colisión no se dibujan.
// - Cerca: la rejilla en anillo de ZoneDraw (platform/near_grid) con la altura del suelo del tramo
//   (platform/stage_surface) y el microrrelieve de la rueda (stage_relief_frame y
//   stage_relief_height): lo que se ve es lo que se pisa (01 §7.3).
// - Líneas de salida, de cada parcial y de meta sobre el núcleo.
// El paisaje más allá de la franja del terreno es de la fase 6.
class StageDraw {
public:
    static constexpr double kNearRadius = 30.0;
    static constexpr double kNearFadeStart = 27.0;
    static constexpr double kNearSpacing = 0.25;

    // Necesita la ventana abierta (shaders).
    StageDraw();
    ~StageDraw();
    StageDraw(const StageDraw&) = delete;
    StageDraw& operator=(const StageDraw&) = delete;

    // Construye las mallas. `track` tiene que vivir mientras se dibuje. Calcula
    // StageGround::build(track) y comprueba su hash con `expected_ground_hash`
    // (SimWorld::stage_ground_hash()): si no coincide, false con los dos valores en `error` y
    // no dibuja nada. Tarda (unos segundos con lazadas): al cargar.
    bool build(const sim::StageTrack& track, std::uint64_t expected_ground_hash,
               std::string& error);
    bool built() const { return built_; }

    // Dentro de BeginMode3D, relativo a `origin`, como ZoneDraw::draw; `eye` es el ojo en el
    // mundo. No reserva memoria salvo la rejilla cercana la primera vez.
    void draw(const Vec3d& eye, const Vec3d& origin);

    // Altura del suelo del tramo (núcleo y terreno) en (x, z); false sin suelo.
    bool ground_height(double x, double z, double& y) const;

    // CPU del último draw() y estadísticas, en ms.
    double last_cpu_ms() const { return stats_.last(); }
    double mean_cpu_ms() const { return stats_.mean(); }
    double max_cpu_ms() const { return stats_.max(); }
    void reset_totals() { stats_.reset_totals(); }
    double total_mean_ms() const { return stats_.total_mean(); }
    double total_max_ms() const { return stats_.total_max(); }
    // Del último draw(): la rejilla cercana, los trozos y los triángulos dibujados.
    const NearStats& last_near() const { return near_stats_; }
    int last_chunks() const { return chunks_drawn_; }
    std::int64_t last_triangles() const { return triangles_drawn_; }
    // Totales de la construcción.
    int chunk_count() const { return static_cast<int>(core_meshes_.size() + tile_meshes_.size()); }
    std::int64_t triangle_total() const { return triangle_total_; }
    std::size_t gpu_bytes() const { return gpu_bytes_; }

    const StageSurface* surface() const { return surface_.get(); }

private:
    void unload();
    Mesh upload(const StageMeshBuffers& buffers);

    ClipShader clip_;
    NearGrid near_;
    PolygonOffset offset_;
    const sim::StageTrack* track_ = nullptr;
    sim::TestZone noise_;
    sim::StageGround ground_;
    std::unique_ptr<StageSurface> surface_;
    std::unique_ptr<StageNearSampler> sampler_;
    StageChunks chunks_;
    std::vector<Mesh> core_meshes_; // en el orden de chunks_.core()
    std::vector<Mesh> tile_meshes_; // en el orden de chunks_.tiles(); vacía sin celdas
    Mesh lines_{};
    std::vector<int> core_selected_;
    std::vector<int> tiles_selected_;
    bool built_ = false;

    DrawTimeStats stats_;
    NearStats near_stats_;
    int chunks_drawn_ = 0;
    std::int64_t triangles_drawn_ = 0;
    std::int64_t triangle_total_ = 0;
    std::size_t gpu_bytes_ = 0;
};

} // namespace rally::platform
