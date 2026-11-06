#include "platform/stage_surface.h"

#include <algorithm>
#include <cmath>

namespace rally::platform {

namespace {

using sim::StageTile;
using sim::StageTriangle;
using sim::StageVertex;

// Cociente entero redondeado hacia −∞.
std::int64_t floor_div(std::int64_t a, std::int64_t b) {
    std::int64_t q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) {
        --q;
    }
    return q;
}

} // namespace

StageSurface::StageSurface(const sim::StageGround& ground) : ground_(&ground) {
    const std::vector<StageVertex>& vertices = ground.vertices();
    const std::vector<StageTriangle>& triangles = ground.triangles();
    if (!triangles.empty()) {
        double max_x = -1e300;
        double max_z = -1e300;
        double min_x = 1e300;
        double min_z = 1e300;
        for (const StageVertex& v : vertices) {
            min_x = std::min(min_x, static_cast<double>(v.x));
            max_x = std::max(max_x, static_cast<double>(v.x));
            min_z = std::min(min_z, static_cast<double>(v.z));
            max_z = std::max(max_z, static_cast<double>(v.z));
        }
        min_x_ = min_x;
        min_z_ = min_z;
        cells_x_ = static_cast<int>((max_x - min_x) / kCoreCell) + 1;
        cells_z_ = static_cast<int>((max_z - min_z) / kCoreCell) + 1;
        const std::size_t cells = static_cast<std::size_t>(cells_x_) * cells_z_;
        // Dos pasadas: cuenta y reparte.
        std::vector<std::uint32_t> counts(cells + 1, 0);
        const auto visit = [&](std::uint32_t t, auto&& f) {
            const StageTriangle& tri = triangles[t];
            const StageVertex* v[3] = {&vertices[tri.a], &vertices[tri.b], &vertices[tri.c]};
            const double x0 = std::min({v[0]->x, v[1]->x, v[2]->x});
            const double x1 = std::max({v[0]->x, v[1]->x, v[2]->x});
            const double z0 = std::min({v[0]->z, v[1]->z, v[2]->z});
            const double z1 = std::max({v[0]->z, v[1]->z, v[2]->z});
            const int i0 = std::max(0, static_cast<int>(std::floor((x0 - min_x_) / kCoreCell)));
            const int i1 =
                std::min(cells_x_ - 1, static_cast<int>(std::floor((x1 - min_x_) / kCoreCell)));
            const int k0 = std::max(0, static_cast<int>(std::floor((z0 - min_z_) / kCoreCell)));
            const int k1 =
                std::min(cells_z_ - 1, static_cast<int>(std::floor((z1 - min_z_) / kCoreCell)));
            for (int k = k0; k <= k1; ++k) {
                for (int i = i0; i <= i1; ++i) {
                    f(static_cast<std::size_t>(k) * cells_x_ + i);
                }
            }
        };
        for (std::uint32_t t = 0; t < triangles.size(); ++t) {
            visit(t, [&](std::size_t c) { ++counts[c + 1]; });
        }
        for (std::size_t c = 0; c < cells; ++c) {
            counts[c + 1] += counts[c];
        }
        cell_first_ = counts;
        cell_triangles_.resize(counts[cells]);
        std::vector<std::uint32_t> fill(counts.begin(), counts.end() - 1);
        for (std::uint32_t t = 0; t < triangles.size(); ++t) {
            visit(t, [&](std::size_t c) { cell_triangles_[fill[c]++] = t; });
        }
    }
    const std::vector<StageTile>& tiles = ground.tiles();
    if (!tiles.empty()) {
        int min_tx = tiles[0].tx;
        int max_tx = tiles[0].tx;
        int min_tz = tiles[0].tz;
        int max_tz = tiles[0].tz;
        for (const StageTile& t : tiles) {
            min_tx = std::min(min_tx, t.tx);
            max_tx = std::max(max_tx, t.tx);
            min_tz = std::min(min_tz, t.tz);
            max_tz = std::max(max_tz, t.tz);
        }
        min_tx_ = min_tx;
        min_tz_ = min_tz;
        tiles_x_ = max_tx - min_tx + 1;
        tiles_z_ = max_tz - min_tz + 1;
        tile_map_.assign(static_cast<std::size_t>(tiles_x_) * tiles_z_, -1);
        for (std::size_t i = 0; i < tiles.size(); ++i) {
            tile_map_[static_cast<std::size_t>(tiles[i].tz - min_tz) * tiles_x_ +
                      (tiles[i].tx - min_tx)] = static_cast<std::int32_t>(i);
        }
    }
}

bool StageSurface::core_at(double x, double z, double& y, sim::Surface& surface) const {
    if (cell_first_.empty()) {
        return false;
    }
    const double lx = x - ground_->origin_x();
    const double lz = z - ground_->origin_z();
    const int i = static_cast<int>(std::floor((lx - min_x_) / kCoreCell));
    const int k = static_cast<int>(std::floor((lz - min_z_) / kCoreCell));
    if (i < 0 || k < 0 || i >= cells_x_ || k >= cells_z_) {
        return false;
    }
    const std::size_t cell = static_cast<std::size_t>(k) * cells_x_ + i;
    const std::vector<StageVertex>& vertices = ground_->vertices();
    const std::vector<StageTriangle>& triangles = ground_->triangles();
    bool found = false;
    constexpr double kEps = 1e-9;
    for (std::uint32_t n = cell_first_[cell]; n < cell_first_[cell + 1]; ++n) {
        const StageTriangle& tri = triangles[cell_triangles_[n]];
        const StageVertex& a = vertices[tri.a];
        const StageVertex& b = vertices[tri.b];
        const StageVertex& c = vertices[tri.c];
        const double d = (static_cast<double>(b.z) - c.z) * (static_cast<double>(a.x) - c.x) +
                         (static_cast<double>(c.x) - b.x) * (static_cast<double>(a.z) - c.z);
        if (d == 0.0) {
            continue;
        }
        const double w0 = ((static_cast<double>(b.z) - c.z) * (lx - c.x) +
                           (static_cast<double>(c.x) - b.x) * (lz - c.z)) /
                          d;
        const double w1 = ((static_cast<double>(c.z) - a.z) * (lx - c.x) +
                           (static_cast<double>(a.x) - c.x) * (lz - c.z)) /
                          d;
        const double w2 = 1.0 - w0 - w1;
        if (w0 < -kEps || w1 < -kEps || w2 < -kEps) {
            continue;
        }
        const double h = w0 * a.y + w1 * b.y + w2 * c.y + ground_->origin_y();
        if (!found || h > y) {
            y = h;
            surface = tri.surface;
            found = true;
        }
    }
    return found;
}

bool StageSurface::terrain_sample(std::int64_t gi, std::int64_t gk, float& h) const {
    if (tile_map_.empty()) {
        return false;
    }
    // Una muestra del borde pertenece a dos baldosas: vale cualquiera; se busca la que la tiene
    // en [0, 63] y existe.
    const std::int64_t tx = floor_div(gi, sim::kStageTileCells);
    const std::int64_t tz = floor_div(gk, sim::kStageTileCells);
    for (std::int64_t dz = 0; dz >= -1; --dz) {
        for (std::int64_t dx = 0; dx >= -1; --dx) {
            const std::int64_t ux = tx + dx;
            const std::int64_t uz = tz + dz;
            const std::int64_t li = gi - ux * sim::kStageTileCells;
            const std::int64_t lk = gk - uz * sim::kStageTileCells;
            if (li < 0 || li >= sim::kStageTileSamples || lk < 0 || lk >= sim::kStageTileSamples) {
                continue;
            }
            const std::int64_t mx = ux - min_tx_;
            const std::int64_t mz = uz - min_tz_;
            if (mx < 0 || mz < 0 || mx >= tiles_x_ || mz >= tiles_z_) {
                continue;
            }
            const std::int32_t index = tile_map_[static_cast<std::size_t>(mz * tiles_x_ + mx)];
            if (index < 0) {
                continue;
            }
            const float value =
                ground_->tiles()[static_cast<std::size_t>(index)]
                    .heights[static_cast<std::size_t>(lk * sim::kStageTileSamples + li)];
            if (value == sim::kStageNoCollision) {
                // Otra baldosa puede tener la misma muestra con colisión: son la misma muestra,
                // así que no.
                return false;
            }
            h = value;
            return true;
        }
    }
    return false;
}

bool StageSurface::terrain_at(double x, double z, double& y) const {
    if (tile_map_.empty()) {
        return false;
    }
    const double cell = ground_->cell();
    const double u = (x - ground_->origin_x()) / cell;
    const double w = (z - ground_->origin_z()) / cell;
    const auto gi = static_cast<std::int64_t>(std::floor(u));
    const auto gk = static_cast<std::int64_t>(std::floor(w));
    float h00 = 0.0f;
    float h10 = 0.0f;
    float h01 = 0.0f;
    float h11 = 0.0f;
    const bool c00 = terrain_sample(gi, gk, h00);
    const bool c10 = terrain_sample(gi + 1, gk, h10);
    const bool c01 = terrain_sample(gi, gk + 1, h01);
    const bool c11 = terrain_sample(gi + 1, gk + 1, h11);
    const double fu = u - static_cast<double>(gi);
    const double fv = w - static_cast<double>(gk);
    // Jolt quita cada triángulo por separado: (x,y),(x,y+1),(x+1,y+1) y (x,y),(x+1,y+1),(x+1,y)
    // existen si tienen colisión sus tres esquinas.
    double h;
    if (fu <= fv) {
        if (!(c00 && c01 && c11)) {
            return false;
        }
        h = h00 + fu * (static_cast<double>(h11) - h01) + fv * (static_cast<double>(h01) - h00);
    } else {
        if (!(c00 && c11 && c10)) {
            return false;
        }
        h = h00 + fu * (static_cast<double>(h10) - h00) + fv * (static_cast<double>(h11) - h10);
    }
    y = h + ground_->origin_y();
    return true;
}

StageHit StageSurface::query(double x, double z) const {
    StageHit hit;
    double core_y = 0.0;
    sim::Surface core_surface = sim::Surface::Dirt;
    double terrain_y = 0.0;
    const bool core = core_at(x, z, core_y, core_surface);
    const bool terrain = terrain_at(x, z, terrain_y);
    if (core && (!terrain || core_y >= terrain_y)) {
        hit.valid = true;
        hit.y = core_y;
        hit.surface = core_surface;
        hit.core = true;
    } else if (terrain) {
        hit.valid = true;
        hit.y = terrain_y;
        hit.surface = sim::Surface::Dirt;
    }
    return hit;
}

} // namespace rally::platform
