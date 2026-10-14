#include "platform/forest_layout.h"

#include <doctest/doctest.h>

#include <cmath>
#include <set>
#include <vector>

using rally::platform::build_tree;
using rally::platform::kForestInnerRadius;
using rally::platform::kForestOuterRadius;
using rally::platform::kForestTrees;
using rally::platform::kTreeHeight;
using rally::platform::kTreeMaxScale;
using rally::platform::kTreeMinScale;
using rally::platform::kTreeTriangles;
using rally::platform::kTreeVertices;
using rally::platform::place_trees;
using rally::platform::TreePlacement;
using rally::platform::TreeVertex;

namespace {

std::vector<TreePlacement> forest(std::uint64_t seed) {
    std::vector<TreePlacement> trees(kForestTrees);
    place_trees(trees, seed, kForestInnerRadius, kForestOuterRadius);
    return trees;
}

} // namespace

TEST_CASE("forest layout: trees fill the ring around the arena, same seed same forest") {
    const std::vector<TreePlacement> trees = forest(42);
    int inner_half = 0; // árboles con r² por debajo del punto medio de r²
    const float middle2 =
        0.5f * (kForestInnerRadius * kForestInnerRadius + kForestOuterRadius * kForestOuterRadius);
    for (const TreePlacement& tree : trees) {
        const float r2 = tree.x * tree.x + tree.z * tree.z;
        CHECK(std::sqrt(r2) >= kForestInnerRadius - 1e-3f);
        CHECK(std::sqrt(r2) < kForestOuterRadius + 1e-3f);
        CHECK(tree.scale >= kTreeMinScale);
        CHECK(tree.scale < kTreeMaxScale);
        inner_half += r2 < middle2 ? 1 : 0;
    }
    // Uniforme por área: la mitad interior en área tiene la mitad de los árboles (±2 %).
    CHECK(std::abs(inner_half - kForestTrees / 2) < kForestTrees / 50);

    const std::vector<TreePlacement> again = forest(42);
    const std::vector<TreePlacement> other = forest(43);
    int same = 0;
    for (std::size_t i = 0; i < trees.size(); ++i) {
        CHECK(trees[i].x == again[i].x);
        CHECK(trees[i].z == again[i].z);
        CHECK(trees[i].yaw == again[i].yaw);
        same += trees[i].x == other[i].x ? 1 : 0;
    }
    CHECK(same == 0);
}

TEST_CASE("forest layout: the tree has 50 outward-facing triangles of one color each") {
    const auto tree = build_tree();
    CHECK(kTreeTriangles == 50);
    CHECK(tree.size() == static_cast<std::size_t>(kTreeVertices));

    std::set<int> colors;
    int down_faces = 0;
    for (int t = 0; t < kTreeTriangles; ++t) {
        const TreeVertex& a = tree[static_cast<std::size_t>(3 * t)];
        const TreeVertex& b = tree[static_cast<std::size_t>(3 * t + 1)];
        const TreeVertex& c = tree[static_cast<std::size_t>(3 * t + 2)];
        // Sombreado plano: los tres vértices con el mismo color.
        CHECK(a.r == b.r);
        CHECK(a.g == c.g);
        CHECK(b.b == c.b);
        colors.insert(a.r * 65536 + a.g * 256 + a.b);
        for (const TreeVertex* v : {&a, &b, &c}) {
            CHECK(v->y >= 0.0f);
            CHECK(v->y <= kTreeHeight);
        }

        // Normal en sentido antihorario: hacia fuera del eje y (lados) o hacia abajo (bases).
        const float ux = b.x - a.x;
        const float uy = b.y - a.y;
        const float uz = b.z - a.z;
        const float vx = c.x - a.x;
        const float vy = c.y - a.y;
        const float vz = c.z - a.z;
        const float nx = uy * vz - uz * vy;
        const float ny = uz * vx - ux * vz;
        const float nz = ux * vy - uy * vx;
        const bool flat = std::abs(a.y - b.y) < 1e-6f && std::abs(a.y - c.y) < 1e-6f;
        if (flat) {
            CHECK(ny < 0.0f);
            ++down_faces;
        } else {
            const float cx = (a.x + b.x + c.x) / 3.0f;
            const float cz = (a.z + b.z + c.z) / 3.0f;
            CHECK(nx * cx + nz * cz > 0.0f);
        }
    }
    CHECK(down_faces == 20); // las bases de los dos conos
    // Más de un tono por material: las caras tienen sombreado distinto según su normal.
    CHECK(colors.size() > 3);
}
