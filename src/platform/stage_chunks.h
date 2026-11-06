#pragma once

#include "sim/stage_ground.h"

#include <cstdint>
#include <vector>

namespace rally::platform {

// Trozos del dibujo del tramo (tarea tramo-dibujo, lote 5): el núcleo, partido en tramos de
// ~kStageCoreChunkLength metros de s, y el terreno, una baldosa por trozo. Cada triángulo y cada
// baldosa está en exactamente un trozo. Solo se dibujan los que caen cerca del ojo.
inline constexpr double kStageCoreChunkLength = 64.0; // de s por trozo del núcleo (m)
// Distancia de dibujo en planta, del ojo a la caja del trozo (m). Provisional: el tramo es una
// cinta de ±60 m y no hay nada más allá (el paisaje lejano es de la fase 6).
inline constexpr double kStageDrawDistance = 600.0;

struct StageChunk {
    // Caja en planta, en el mundo.
    double min_x = 0.0, max_x = 0.0, min_z = 0.0, max_z = 0.0;
    // Núcleo: los índices (en ground.triangles()) que empiezan en `first` en core_triangles();
    // baldosa: índice en ground.tiles().
    std::uint32_t first = 0;
    std::uint32_t count = 0;
};

class StageChunks {
public:
    static StageChunks build(const sim::StageGround& ground);

    const std::vector<StageChunk>& core() const { return core_; }
    const std::vector<StageChunk>& tiles() const { return tiles_; }
    // Triángulos de los trozos del núcleo, uno tras otro (cada trozo es [first, first + count)).
    const std::vector<std::uint32_t>& core_triangles() const { return core_triangles_; }

    // Distancia en planta de (x, z) a la caja del trozo (0 dentro).
    static double distance(const StageChunk& chunk, double x, double z);
    // Índices de los trozos a ≤ `distance` del ojo. `out` se limpia; con la capacidad de sobra
    // (reserve_selection) no reserva.
    void select_core(double eye_x, double eye_z, double distance, std::vector<int>& out) const;
    void select_tiles(double eye_x, double eye_z, double distance, std::vector<int>& out) const;
    // Reserva lo que cabe en el peor caso.
    void reserve_selection(std::vector<int>& core, std::vector<int>& tiles) const;

private:
    std::vector<StageChunk> core_;
    std::vector<StageChunk> tiles_;
    std::vector<std::uint32_t> core_triangles_;
};

} // namespace rally::platform
