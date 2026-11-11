#pragma once

#include "sim/stage_ground.h"
#include "sim/surface.h"

#include <cstdint>
#include <vector>

namespace rally::platform {

// Consultas del suelo del tramo en el mundo (tarea tramo-dibujo, lote 5), sobre la misma
// StageGround que la física: el núcleo (malla) y el terreno (baldosas). Es lo que necesita el
// dibujo para dar la altura y la superficie en un punto cualquiera sin Jolt: la rejilla cercana,
// las líneas y las baldosas tapadas por el núcleo. Se construye al cargar; las consultas no
// reservan memoria. La StageGround tiene que vivir mientras se use.
struct StageHit {
    bool valid = false; // hay suelo (núcleo o terreno con colisión)
    double y = 0.0;     // altura en el mundo (la mayor entre el núcleo y el terreno)
    sim::Surface surface = sim::Surface::Dirt;
    bool core = false; // la superficie de arriba es el núcleo
    // Aspecto del triángulo de arriba (tramo-dibujo-2): el del núcleo; del terreno, Road (sin uso).
    sim::StageLook look = sim::StageLook::Road;
};

class StageSurface {
public:
    explicit StageSurface(const sim::StageGround& ground);

    StageHit query(double x, double z) const;
    // Solo el núcleo: si (x, z) cae en un triángulo, su altura (la mayor si hay varios) y su
    // superficie.
    // `look`, si no es nulo, recibe el aspecto de ese triángulo.
    bool core_at(double x, double z, double& y, sim::Surface& surface,
                 sim::StageLook* look = nullptr) const;
    // Solo el terreno: la altura de la celda (con las diagonales de Jolt: (i, k)–(i+1, k+1)).
    bool terrain_at(double x, double z, double& y) const;
    // Altura (respecto a origin_y) de la muestra global (gi, gk) de las baldosas; false sin baldosa
    // o sin colisión.
    bool terrain_sample(std::int64_t gi, std::int64_t gk, float& h) const;

    const sim::StageGround& ground() const { return *ground_; }

private:
    static constexpr double kCoreCell = 4.0; // rejilla del índice de triángulos del núcleo (m)

    const sim::StageGround* ground_;
    // Índice del núcleo: celdas de kCoreCell en coordenadas locales (respecto al origen), con la
    // lista de triángulos de cada una en forma CSR.
    double min_x_ = 0.0;
    double min_z_ = 0.0;
    int cells_x_ = 0;
    int cells_z_ = 0;
    std::vector<std::uint32_t> cell_first_;
    std::vector<std::uint32_t> cell_triangles_;
    // Mapa denso de baldosas: índice en ground.tiles() o −1.
    int min_tx_ = 0;
    int min_tz_ = 0;
    int tiles_x_ = 0;
    int tiles_z_ = 0;
    std::vector<std::int32_t> tile_map_;
};

} // namespace rally::platform
