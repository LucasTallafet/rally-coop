#include "platform/recording_header.h"

#include "io/build_info.h"
#include "io/replay.h"

namespace rally::platform {

io::RecordingHeader make_recording_header(const sim::WorldDesc& desc, const sim::SimWorld& world,
                                          const std::string& car_text, const std::string& settings,
                                          const std::string& actions_text) {
    io::RecordingHeader header;
    header.commit = io::build_commit();
    header.world = io::serialize_world(desc);
    header.car_text = car_text;
    header.settings = settings;
    header.actions_text = actions_text;
    header.zone_hash = world.zone_hash();
    header.stage_ground_hash = world.stage_ground_hash();
    return header;
}

io::Recording finish_recording(io::Recorder& recorder, const sim::SimWorld& world,
                               std::uint64_t actions_hash) {
    return recorder.finish(world.state_hash(), io::timed_steps(world), io::road_result(world),
                           actions_hash, io::stage_result(world));
}

} // namespace rally::platform
