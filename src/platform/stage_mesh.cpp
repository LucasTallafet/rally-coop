#include "platform/stage_mesh.h"

#include <algorithm>
#include <cmath>

namespace rally::platform {

namespace {

struct StageVertex3 {
    float x, y, z;
};

constexpr int kSamples = sim::kStageTileSamples;
constexpr int kCells = sim::kStageTileCells;

void push_vertex(StageMeshBuffers& out, float x, float y, float z, unsigned char r, unsigned char g,
                 unsigned char b) {
    out.positions.push_back(x);
    out.positions.push_back(y);
    out.positions.push_back(z);
    out.colors.push_back(r);
    out.colors.push_back(g);
    out.colors.push_back(b);
    out.colors.push_back(255);
}

unsigned char shaded_channel(unsigned char c, float shade) {
    return static_cast<unsigned char>(static_cast<float>(c) * shade);
}

} // namespace

float stage_shade(float nx, float ny, float nz) {
    constexpr float kLightX = 0.35f;
    constexpr float kLightY = 1.0f;
    constexpr float kLightZ = 0.55f;
    const float light_length = std::sqrt(kLightX * kLightX + kLightY * kLightY + kLightZ * kLightZ);
    const float length = std::sqrt(nx * nx + ny * ny + nz * nz);
    const float dot = (nx * kLightX + ny * kLightY + nz * kLightZ) / (length * light_length);
    return 0.5f + 0.5f * std::max(0.0f, dot);
}

std::array<unsigned char, 3> stage_core_rgb(const StagePalette& palette,
                                            const StageLookStyle& style, sim::Surface surface,
                                            sim::StageLook look) {
    const int index = stage_color_index(surface, look);
    if (index == kStageColorRock) {
        return style.wall;
    }
    if (index == kStageColorDitch) {
        const auto& dirt = palette[static_cast<std::size_t>(sim::Surface::Dirt)];
        return {shaded_channel(dirt[0], style.ditch_darken),
                shaded_channel(dirt[1], style.ditch_darken),
                shaded_channel(dirt[2], style.ditch_darken)};
    }
    return palette[static_cast<std::size_t>(surface)];
}

void build_core_mesh(const sim::StageGround& ground, const std::uint32_t* triangles,
                     std::size_t count, const StagePalette& palette, StageMeshBuffers& out,
                     const StageLookStyle& style) {
    out.positions.clear();
    out.colors.clear();
    out.indices.clear();
    out.indexed = false;
    out.positions.reserve(count * 9);
    out.colors.reserve(count * 12);
    for (std::size_t n = 0; n < count; ++n) {
        const sim::StageTriangle& t = ground.triangles()[triangles[n]];
        const sim::StageVertex* a = &ground.vertices()[t.a];
        const sim::StageVertex* b = &ground.vertices()[t.b];
        const sim::StageVertex* c = &ground.vertices()[t.c];
        float nx = (b->y - a->y) * (c->z - a->z) - (b->z - a->z) * (c->y - a->y);
        float ny = (b->z - a->z) * (c->x - a->x) - (b->x - a->x) * (c->z - a->z);
        float nz = (b->x - a->x) * (c->y - a->y) - (b->y - a->y) * (c->x - a->x);
        if (ny < 0.0f) {
            std::swap(b, c);
            nx = -nx;
            ny = -ny;
            nz = -nz;
        }
        const float shade = stage_shade(nx, ny, nz);
        const std::array<unsigned char, 3> rgb = stage_core_rgb(palette, style, t.surface, t.look);
        for (const sim::StageVertex* v : {a, b, c}) {
            push_vertex(out, v->x, v->y, v->z, shaded_channel(rgb[0], shade),
                        shaded_channel(rgb[1], shade), shaded_channel(rgb[2], shade));
        }
    }
}

std::size_t drawn_tile_triangles(const sim::StageTile& tile) {
    std::size_t triangles = 0;
    for (int k = 0; k < kCells; ++k) {
        for (int i = 0; i < kCells; ++i) {
            const auto ok = [&](int a, int b) {
                return tile.heights[static_cast<std::size_t>(b * kSamples + a)] !=
                       sim::kStageNoCollision;
            };
            const bool c00 = ok(i, k);
            const bool c10 = ok(i + 1, k);
            const bool c01 = ok(i, k + 1);
            const bool c11 = ok(i + 1, k + 1);
            triangles += (c00 && c01 && c11 ? 1 : 0) + (c00 && c11 && c10 ? 1 : 0);
        }
    }
    return triangles;
}

void build_tile_mesh(const StageSurface& surface, std::size_t tile, const StagePalette& palette,
                     StageMeshBuffers& out) {
    const sim::StageGround& g = surface.ground();
    const sim::StageTile& t = g.tiles()[tile];
    out.positions.clear();
    out.colors.clear();
    out.indices.clear();
    out.indexed = true;
    const auto cell = static_cast<float>(g.cell());
    // Altura de la muestra (i, k) de la baldosa, o de la vecina si cae fuera; la propia si no
    // hay colisión.
    const auto height = [&](int i, int k, float fallback) {
        float h = 0.0f;
        if (i >= 0 && i < kSamples && k >= 0 && k < kSamples) {
            h = t.heights[static_cast<std::size_t>(k * kSamples + i)];
        } else if (!surface.terrain_sample(static_cast<std::int64_t>(t.tx) * kCells + i,
                                           static_cast<std::int64_t>(t.tz) * kCells + k, h)) {
            return fallback;
        }
        return h == sim::kStageNoCollision ? fallback : h;
    };
    const auto& dirt = palette[static_cast<std::size_t>(sim::Surface::Dirt)];
    out.positions.reserve(static_cast<std::size_t>(kSamples) * kSamples * 3);
    out.colors.reserve(static_cast<std::size_t>(kSamples) * kSamples * 4);
    for (int k = 0; k < kSamples; ++k) {
        for (int i = 0; i < kSamples; ++i) {
            float h = t.heights[static_cast<std::size_t>(k * kSamples + i)];
            const bool has = h != sim::kStageNoCollision;
            if (!has) {
                h = 0.0f;
            }
            const float x = static_cast<float>((t.tx * kCells + i) * g.cell());
            const float z = static_cast<float>((t.tz * kCells + k) * g.cell());
            const float shade =
                has ? stage_shade(height(i - 1, k, h) - height(i + 1, k, h), 2.0f * cell,
                                  height(i, k - 1, h) - height(i, k + 1, h))
                    : 1.0f;
            push_vertex(out, x, h, z, shaded_channel(dirt[0], shade),
                        shaded_channel(dirt[1], shade), shaded_channel(dirt[2], shade));
        }
    }
    const auto collides = [&](int a, int b) {
        return t.heights[static_cast<std::size_t>(b * kSamples + a)] != sim::kStageNoCollision;
    };
    for (int k = 0; k < kCells; ++k) {
        for (int i = 0; i < kCells; ++i) {
            const auto v00 = static_cast<std::uint16_t>(k * kSamples + i);
            const auto v10 = static_cast<std::uint16_t>(v00 + 1);
            const auto v01 = static_cast<std::uint16_t>(v00 + kSamples);
            const auto v11 = static_cast<std::uint16_t>(v01 + 1);
            const bool c00 = collides(i, k);
            const bool c10 = collides(i + 1, k);
            const bool c01 = collides(i, k + 1);
            const bool c11 = collides(i + 1, k + 1);
            // Hacia arriba (contrario a las agujas del reloj visto desde +y), diagonal 00–11,
            // cada triángulo si tiene colisión sus tres esquinas (como en Jolt).
            if (c00 && c01 && c11) {
                for (const std::uint16_t v : {v00, v01, v11}) {
                    out.indices.push_back(v);
                }
            }
            if (c00 && c11 && c10) {
                for (const std::uint16_t v : {v00, v11, v10}) {
                    out.indices.push_back(v);
                }
            }
        }
    }
}

std::vector<double> stage_line_positions(double length) {
    std::vector<double> s{0.0};
    for (int n = 1; n <= sim::kMaxSplits; ++n) {
        const double split = static_cast<double>(n) * sim::kSplitSpacing;
        if (split > length - sim::kSplitFinishGap) {
            break;
        }
        s.push_back(split);
    }
    s.push_back(length);
    return s;
}

void build_stage_lines(const sim::StageTrack& track, const StageSurface& surface,
                       const sim::TestZone& noise, StageMeshBuffers& out) {
    out.positions.clear();
    out.colors.clear();
    out.indices.clear();
    out.indexed = false;
    const sim::StageGround& g = surface.ground();
    constexpr unsigned char kWhite[3] = {224, 224, 217};
    for (const double s : stage_line_positions(track.axis().length())) {
        const sim::StagePose pose = track.pose(s);
        const double sh = std::sin(pose.heading);
        const double ch = std::cos(pose.heading);
        const double width = pose.half_width_left + pose.half_width_right;
        const int steps = std::max(1, static_cast<int>(std::ceil(width / kStageLineStep)));
        // Dos filas de vértices (detrás y delante de la línea), steps + 1 columnas, de izquierda
        // a derecha. +t es la derecha: (cos, sin); el avance, (sin, −cos).
        std::vector<StageVertex3> rows[2];
        for (int row = 0; row < 2; ++row) {
            const double a = row == 0 ? -0.5 * kStageLineWidth : 0.5 * kStageLineWidth;
            for (int n = 0; n <= steps; ++n) {
                const double t = -pose.half_width_left + width * n / steps;
                const double x = pose.x + t * ch + a * sh;
                const double z = pose.z + t * sh - a * ch;
                const StageHit hit = surface.query(x, z);
                const double base = hit.valid ? hit.y : pose.y;
                const sim::StageReliefFrame f = sim::stage_relief_frame(track, s, x, z);
                const double h = sim::stage_relief_height(f, noise, f.s, f.t, x, z);
                rows[row].push_back({static_cast<float>(x - g.origin_x()),
                                     static_cast<float>(base + h + kStageLineLift - g.origin_y()),
                                     static_cast<float>(z - g.origin_z())});
            }
        }
        for (int n = 0; n < steps; ++n) {
            const StageVertex3& a = rows[0][static_cast<std::size_t>(n)];
            const StageVertex3& b = rows[0][static_cast<std::size_t>(n) + 1];
            const StageVertex3& c = rows[1][static_cast<std::size_t>(n) + 1];
            const StageVertex3& d = rows[1][static_cast<std::size_t>(n)];
            // Orientados hacia arriba, comprobando la normal de cada triángulo.
            const auto emit = [&](const StageVertex3& p, const StageVertex3& q,
                                  const StageVertex3& r) {
                const float ny = (q.z - p.z) * (r.x - p.x) - (q.x - p.x) * (r.z - p.z);
                const StageVertex3* v[3] = {&p, ny >= 0.0f ? &q : &r, ny >= 0.0f ? &r : &q};
                for (const StageVertex3* w : v) {
                    push_vertex(out, w->x, w->y, w->z, kWhite[0], kWhite[1], kWhite[2]);
                }
            };
            emit(a, b, c);
            emit(a, c, d);
        }
    }
}

} // namespace rally::platform
