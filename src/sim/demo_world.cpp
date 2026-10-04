#include "sim/demo_world.h"

#include "sim/test_zone.h"

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

namespace rally::sim {

WorldDesc zone_world(Surface blind_surface, ZoneStart start_at) {
    const TestZone zone(blind_surface);
    WorldDesc desc;
    const RespawnPoint& start =
        start_at == ZoneStart::BlindTrack ? zone.blind_start() : zone.start();
    BoxDesc box;
    box.motion = BodyMotion::Dynamic;
    box.x = start.x;
    box.y = zone.ground_height(start.x, start.z) + box.half_y;
    box.z = start.z;
    desc.bodies = {box};
    desc.controlled = kZoneControlled;
    desc.test_zone = true;
    desc.blind_surface = blind_surface;
    return desc;
}

} // namespace rally::sim
