#pragma once

#include <array>
#include <cstdint>
#include <span>

namespace rally::platform {

// Escena de medida "bosque denso" (fase 1, tarea 6): solo para medir el coste de dibujar
// mucha vegetación opaca; no es la vegetación de la fase 6.

// Un árbol respecto al centro del recinto, sobre el suelo (y = 0).
struct TreePlacement {
    float x = 0.0f;
    float z = 0.0f;
    float scale = 1.0f;
    float yaw = 0.0f; // giro alrededor de y, en radianes
};

constexpr int kForestTrees = 20'000;
constexpr float kForestInnerRadius = 20.0f;  // fuera del recinto de 30 × 30 m
constexpr float kForestOuterRadius = 250.0f; // ~1 árbol cada 10 m²
constexpr float kTreeMinScale = 0.7f;
constexpr float kTreeMaxScale = 1.3f;

// Reparte los árboles uniformemente por área en el anillo [inner, outer) alrededor del
// centro, con escala en [kTreeMinScale, kTreeMaxScale) y giro aleatorio. PCG32 con la
// semilla dada: el mismo bosque en cada arranque.
void place_trees(std::span<TreePlacement> trees, std::uint64_t seed, float inner_radius,
                 float outer_radius);

// Vértice del árbol: posición en metros y color con el sombreado plano de su cara ya
// aplicado (la misma luz fija que las cajas de la demo).
struct TreeVertex {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;
};

// Árbol low-poly sin índices: dos conos de 10 lados con base (copa) y un tronco de 5 lados
// sin tapas. Triángulos en sentido antihorario vistos desde fuera.
constexpr int kTreeConeSides = 10;
constexpr int kTreeTrunkSides = 5;
constexpr int kTreeTriangles = 2 * 2 * kTreeConeSides + 2 * kTreeTrunkSides; // 50
constexpr int kTreeVertices = 3 * kTreeTriangles;
constexpr float kTreeHeight = 8.5f; // con escala 1

std::array<TreeVertex, kTreeVertices> build_tree();

} // namespace rally::platform
