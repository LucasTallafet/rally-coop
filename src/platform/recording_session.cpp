#include "platform/recording_session.h"

#include "io/build_info.h"
#include "io/crash_save.h"
#include "io/replay.h"

#include <raylib.h>

#include <ctime>

namespace rally::platform {

namespace {

// Nombre libre en la carpeta: AAAAMMDD-HHMMSS.rrec, o con -2, -3... si ya existe (dos
// reinicios en el mismo segundo) o es el de la grabación anterior, que aún no se ha escrito.
std::string free_name(const std::string& folder, const std::string& previous) {
    const std::time_t now = std::time(nullptr);
    std::tm local{};
    localtime_s(&local, &now);
    char stamp[32];
    std::strftime(stamp, sizeof(stamp), "%Y%m%d-%H%M%S", &local);
    std::string name = std::string(stamp) + ".rrec";
    for (int n = 2; name == previous || FileExists((folder + name).c_str()); ++n) {
        name = std::string(stamp) + "-" + std::to_string(n) + ".rrec";
    }
    return name;
}

} // namespace

RecordingSession::~RecordingSession() {
    if (active()) {
        io::set_crash_recording(nullptr, {});
    }
}

void RecordingSession::start(const sim::WorldDesc& desc, const sim::SimWorld& world,
                             const std::string& car_text, const std::string& settings) {
    const std::string folder = std::string(GetApplicationDirectory()) + "grabaciones/";
    name_ = free_name(folder, name_);
    path_ = folder + name_;
    io::RecordingHeader header;
    header.commit = io::build_commit();
    header.world = io::serialize_world(desc);
    header.car_text = car_text;
    header.settings = settings;
    header.zone_hash = world.zone_hash();
    recorder_.begin(std::move(header), io::steps_for_minutes(kReservedMinutes));
    io::set_crash_recording(&recorder_, path_);
}

void RecordingSession::finish(const sim::SimWorld& world) {
    if (!active()) {
        return;
    }
    io::set_crash_recording(nullptr, {});
    const bool has_steps = recorder_.steps() > 0;
    const io::Recording recording =
        recorder_.finish(world.state_hash(), io::timed_steps(world), io::road_result(world));
    if (has_steps && !io::write_file(path_, io::serialize(recording), error_)) {
        TraceLog(LOG_WARNING, "grabación: %s", error_.c_str());
    }
}

} // namespace rally::platform
