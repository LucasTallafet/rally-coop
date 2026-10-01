#pragma once

#include "sim/jolt_config.h"

#include <Jolt/Physics/Collision/BroadPhase/BroadPhaseLayer.h>
#include <Jolt/Physics/Collision/ObjectLayer.h>

// Capas de colisión: una estática y una móvil. Cada capa de objeto tiene su propia capa
// de la broad phase. Lo estático solo choca con lo móvil.
namespace rally::sim::layers {

inline constexpr JPH::ObjectLayer kStatic = 0;
inline constexpr JPH::ObjectLayer kMoving = 1;
inline constexpr JPH::uint kCount = 2;

class BroadPhaseLayers final : public JPH::BroadPhaseLayerInterface {
public:
    JPH::uint GetNumBroadPhaseLayers() const override { return kCount; }
    JPH::BroadPhaseLayer GetBroadPhaseLayer(JPH::ObjectLayer layer) const override {
        return JPH::BroadPhaseLayer(static_cast<JPH::BroadPhaseLayer::Type>(layer));
    }
};

class ObjectVsBroadPhase final : public JPH::ObjectVsBroadPhaseLayerFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer layer, JPH::BroadPhaseLayer bp) const override {
        return layer == kMoving ||
               bp.GetValue() == static_cast<JPH::BroadPhaseLayer::Type>(kMoving);
    }
};

class ObjectPairs final : public JPH::ObjectLayerPairFilter {
public:
    bool ShouldCollide(JPH::ObjectLayer a, JPH::ObjectLayer b) const override {
        return a == kMoving || b == kMoving;
    }
};

} // namespace rally::sim::layers
