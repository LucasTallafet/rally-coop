#pragma once

#include "platform/stage_look.h"
#include "platform/stage_surface.h"
#include "sim/stage_ground.h"
#include "sim/stage_track.h"
#include "sim/test_zone.h"

#include <array>
#include <cstdint>
#include <vector>

namespace rally::platform {

// Mallas del dibujo del tramo en buffers de CPU (tarea tramo-dibujo, lote 5), sin raylib: el
// dibujo las sube a la GPU y los tests las comparan con StageGround. Las posiciones van en float
// relativo al origen del suelo (origin_*), como los vértices de la física.
struct StageMeshBuffers {
    std::vector<float> positions;       // 3 por vértice
    std::vector<unsigned char> colors;  // 4 por vértice
    std::vector<std::uint16_t> indices; // si `indexed`; si no, tres vértices por triángulo
    bool indexed = false;
    std::size_t vertex_count() const { return positions.size() / 3; }
    std::size_t triangle_count() const { return indexed ? indices.size() / 3 : vertex_count() / 3; }
};

// Color RGB de cada superficie (índice de sim::Surface).
using StagePalette = std::array<std::array<unsigned char, 3>, sim::kSurfaceCount>;

// Colores del aspecto del triángulo (`look`) sobre la paleta de superficies. Por defecto, los de
// stage_look.h, los mismos que usa la rejilla cercana; los tests pasan otros para que el color
// no coincida por casualidad. La tierra de la cuneta y el talud sale de palette[Dirt] x
// ditch_darken.
struct StageLookStyle {
    std::array<unsigned char, 3> wall = kStageRockRgb;
    float ditch_darken = kStageDitchDarken;
};

// Color sin sombrear de un triángulo del núcleo: roca si es muro, tierra oscurecida si es cuneta o
// talud de tierra, y si no, el de su superficie.
std::array<unsigned char, 3> stage_core_rgb(const StagePalette& palette,
                                            const StageLookStyle& style, sim::Surface surface,
                                            sim::StageLook look);

// Sombreado como el de ZoneDraw: 0,5 + 0,5·max(0, n·luz) con la luz de la demo.
float stage_shade(float nx, float ny, float nz);

// Los triángulos `triangles` (índices en ground.triangles()) del núcleo, sin índices, con el
// color de su superficie (o del aspecto: muro, cuneta, talud) y el sombreado plano. Cada triángulo
// se orienta hacia arriba.
void build_core_mesh(const sim::StageGround& ground, const std::uint32_t* triangles,
                     std::size_t count, const StagePalette& palette, StageMeshBuffers& out,
                     const StageLookStyle& style = StageLookStyle{});

// La baldosa `tile`: kStageTileSamples² vértices (sombreado liso por el gradiente, color de
// tierra) e índices de los triángulos con colisión (los tres vértices con colisión, como en
// Jolt), con la diagonal (i, k)–(i+1, k+1). Los vértices sin colisión van a altura 0 y no se usan.
void build_tile_mesh(const StageSurface& surface, std::size_t tile, const StagePalette& palette,
                     StageMeshBuffers& out);
// Triángulos con colisión de la baldosa, contados aparte (para los tests).
std::size_t drawn_tile_triangles(const sim::StageTile& tile);

// Líneas de salida (s = 0), de cada parcial y de meta, a lo ancho de la calzada, a ground + h +
// kStageLineLift: tiras de kStageLineWidth subdivididas cada kStageLineStep.
inline constexpr double kStageLineLift = 0.03;
inline constexpr double kStageLineWidth = 0.6;
inline constexpr double kStageLineStep = 0.25;
void build_stage_lines(const sim::StageTrack& track, const StageSurface& surface,
                       const sim::TestZone& noise, StageMeshBuffers& out);
// s de las líneas: 0, los parciales (kSplitSpacing, salvo a menos de kSplitFinishGap de la meta)
// y la meta.
std::vector<double> stage_line_positions(double length);

} // namespace rally::platform
