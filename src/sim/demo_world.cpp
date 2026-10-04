#include "sim/demo_world.h"

#include "sim/test_zone.h"

namespace rally::sim {

WorldDesc demo_arena(double offset, const CarData& car) {
    WorldDesc desc;
    desc.bodies = {
        {BodyMotion::Static, 0.0, -0.5, 0.0, 15.0f, 0.5f, 15.0f},
        {BodyMotion::Static, 0.0, 1.0, -15.5, 16.0f, 1.0f, 0.5f},
        {BodyMotion::Static, 0.0, 1.0, 15.5, 16.0f, 1.0f, 0.5f},
        {BodyMotion::Static, -15.5, 1.0, 0.0, 0.5f, 1.0f, 15.0f},
        {BodyMotion::Static, 15.5, 1.0, 0.0, 0.5f, 1.0f, 15.0f},
        {BodyMotion::Dynamic, 4.0, 0.5, -3.0, 0.5f, 0.5f, 0.5f},
        {BodyMotion::Dynamic, -5.0, 0.75, 2.0, 0.75f, 0.75f, 0.75f},
    };
    for (BoxDesc& box : desc.bodies) {
        box.x += offset;
        box.z += offset;
    }
    desc.car = true;
    desc.car_spawn.x = offset;
    desc.car_spawn.z = offset;
    desc.car_data = car;
    return desc;
}

WorldDesc zone_world(Surface blind_surface, const CarData& car, ZoneStart start_at) {
    const TestZone zone(blind_surface);
    WorldDesc desc;
    const RespawnPoint& start =
        start_at == ZoneStart::BlindTrack ? zone.blind_start() : zone.start();
    desc.car = true;
    desc.car_spawn.x = start.x;
    desc.car_spawn.y = zone.ground_height(start.x, start.z);
    desc.car_spawn.z = start.z;
    desc.car_spawn.yaw = static_cast<float>(start.yaw);
    desc.car_data = car;
    desc.test_zone = true;
    desc.blind_surface = blind_surface;
    return desc;
}

} // namespace rally::sim
