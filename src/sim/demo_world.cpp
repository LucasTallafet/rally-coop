#include "sim/demo_world.h"

namespace rally::sim {

WorldDesc demo_arena(double offset) {
    WorldDesc desc;
    desc.bodies = {
        {BodyMotion::Static, 0.0, -0.5, 0.0, 15.0f, 0.5f, 15.0f},
        {BodyMotion::Static, 0.0, 1.0, -15.5, 16.0f, 1.0f, 0.5f},
        {BodyMotion::Static, 0.0, 1.0, 15.5, 16.0f, 1.0f, 0.5f},
        {BodyMotion::Static, -15.5, 1.0, 0.0, 0.5f, 1.0f, 15.0f},
        {BodyMotion::Static, 15.5, 1.0, 0.0, 0.5f, 1.0f, 15.0f},
        {BodyMotion::Dynamic, 0.0, 0.5, 0.0, 0.5f, 0.5f, 0.5f},
        {BodyMotion::Dynamic, 4.0, 0.5, -3.0, 0.5f, 0.5f, 0.5f},
        {BodyMotion::Dynamic, -5.0, 0.75, 2.0, 0.75f, 0.75f, 0.75f},
    };
    for (BoxDesc& box : desc.bodies) {
        box.x += offset;
        box.z += offset;
    }
    desc.controlled = kDemoControlled;
    return desc;
}

} // namespace rally::sim
