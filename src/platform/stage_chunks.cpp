#include "platform/stage_chunks.h"

#include <algorithm>
#include <cmath>

namespace rally::platform {

StageChunks StageChunks::build(const sim::StageGround& ground) {
    StageChunks chunks;
    const std::vector<sim::StageVertex>& vertices = ground.vertices();
    const std::vector<sim::StageTriangle>& triangles = ground.triangles();
    const std::vector<double>& sections = ground.sections();
    // Núcleo: el trozo de un triángulo es el de la s de su sección menor.
    const auto chunk_of = [&](const sim::StageTriangle& t) {
        const std::uint32_t section =
            std::min({t.a, t.b, t.c}) / static_cast<std::uint32_t>(sim::kStageSectionVertices);
        const double s = sections.empty() ? 0.0 : sections[std::min<std::size_t>(
                                                      section, sections.size() - 1)];
        return static_cast<std::int64_t>(std::floor((s - sections.front()) / kStageCoreChunkLength));
    };
    if (!triangles.empty() && !sections.empty()) {
        std::int64_t count = 1;
        for (const sim::StageTriangle& t : triangles) {
            count = std::max(count, chunk_of(t) + 1);
        }
        std::vector<std::vector<std::uint32_t>> lists(static_cast<std::size_t>(count));
        for (std::uint32_t i = 0; i < triangles.size(); ++i) {
            const std::int64_t c = std::max<std::int64_t>(chunk_of(triangles[i]), 0);
            lists[static_cast<std::size_t>(c)].push_back(i);
        }
        for (const std::vector<std::uint32_t>& list : lists) {
            if (list.empty()) {
                continue;
            }
            StageChunk chunk;
            chunk.first = static_cast<std::uint32_t>(chunks.core_triangles_.size());
            chunk.count = static_cast<std::uint32_t>(list.size());
            chunk.min_x = chunk.min_z = 1e300;
            chunk.max_x = chunk.max_z = -1e300;
            for (const std::uint32_t i : list) {
                chunks.core_triangles_.push_back(i);
                const sim::StageTriangle& t = triangles[i];
                for (const std::uint32_t v : {t.a, t.b, t.c}) {
                    chunk.min_x = std::min(chunk.min_x, ground.origin_x() + vertices[v].x);
                    chunk.max_x = std::max(chunk.max_x, ground.origin_x() + vertices[v].x);
                    chunk.min_z = std::min(chunk.min_z, ground.origin_z() + vertices[v].z);
                    chunk.max_z = std::max(chunk.max_z, ground.origin_z() + vertices[v].z);
                }
            }
            chunks.core_.push_back(chunk);
        }
    }
    // Terreno: una baldosa, un trozo.
    const double span = sim::kStageTileCells * ground.cell();
    for (std::size_t i = 0; i < ground.tiles().size(); ++i) {
        const sim::StageTile& tile = ground.tiles()[i];
        StageChunk chunk;
        chunk.first = static_cast<std::uint32_t>(i);
        chunk.count = 1;
        chunk.min_x = ground.origin_x() + tile.tx * span;
        chunk.max_x = chunk.min_x + span;
        chunk.min_z = ground.origin_z() + tile.tz * span;
        chunk.max_z = chunk.min_z + span;
        chunks.tiles_.push_back(chunk);
    }
    return chunks;
}

double StageChunks::distance(const StageChunk& chunk, double x, double z) {
    const double dx = std::max({chunk.min_x - x, 0.0, x - chunk.max_x});
    const double dz = std::max({chunk.min_z - z, 0.0, z - chunk.max_z});
    return std::sqrt(dx * dx + dz * dz);
}

namespace {

void select_from(const std::vector<StageChunk>& chunks, double x, double z, double distance,
                 std::vector<int>& out) {
    out.clear();
    for (std::size_t i = 0; i < chunks.size(); ++i) {
        if (StageChunks::distance(chunks[i], x, z) <= distance) {
            out.push_back(static_cast<int>(i));
        }
    }
}

} // namespace

void StageChunks::select_core(double eye_x, double eye_z, double distance,
                              std::vector<int>& out) const {
    select_from(core_, eye_x, eye_z, distance, out);
}

void StageChunks::select_tiles(double eye_x, double eye_z, double distance,
                               std::vector<int>& out) const {
    select_from(tiles_, eye_x, eye_z, distance, out);
}

void StageChunks::reserve_selection(std::vector<int>& core, std::vector<int>& tiles) const {
    core.reserve(core_.size());
    tiles.reserve(tiles_.size());
}

} // namespace rally::platform
