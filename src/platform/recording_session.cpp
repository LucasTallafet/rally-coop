#include "platform/recording_session.h"

#include "io/build_info.h"
#include "io/crash_save.h"
#include "io/replay.h"
#include "platform/recording_header.h"

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
                             const std::string& car_text, const std::string& settings,
                             const std::string& actions_text) {
    const std::string folder = std::string(GetApplicationDirectory()) + "grabaciones/";
    name_ = free_name(folder, name_);
    path_ = folder + name_;
    recorder_.begin(make_recording_header(desc, world, car_text, settings, actions_text),
                    io::steps_for_minutes(kReservedMinutes));
    io::set_crash_recording(&recorder_, path_);
}

void RecordingSession::finish(const sim::SimWorld& world, std::uint64_t actions_hash) {
    if (!active()) {
        return;
    }
    io::set_crash_recording(nullptr, {});
    const bool has_steps = recorder_.steps() > 0;
    const io::Recording recording = finish_recording(recorder_, world, actions_hash);
    if (has_steps && !io::write_file(path_, io::serialize(recording), error_)) {
        TraceLog(LOG_WARNING, "grabación: %s", error_.c_str());
    }
}

} // namespace rally::platform
