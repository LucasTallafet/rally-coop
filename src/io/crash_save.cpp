#include "io/crash_save.h"

#include "io/recording.h"

#include <cstdio>
#include <utility>

namespace rally::io {

namespace {

const Recorder* g_recorder = nullptr;
std::string g_path;
bool g_saving = false; // un RALLY_CHECK dentro del propio guardado no lo repite

} // namespace

void set_crash_recording(const Recorder* recorder, std::string path) {
    g_recorder = recorder;
    g_path = recorder != nullptr ? std::move(path) : std::string();
}

bool save_crash_recording() {
    if (g_saving || g_recorder == nullptr || !g_recorder->active()) {
        return false;
    }
    g_saving = true;
    Recording recording = g_recorder->recording();
    recording.footer.complete = false;
    recording.footer.final_hash = 0;
    std::string error;
    const bool ok = write_file(g_path, serialize(recording), error);
    if (ok) {
        std::fprintf(stderr, "grabación incompleta guardada: %s (%llu pasos)\n", g_path.c_str(),
                     static_cast<unsigned long long>(recording.footer.total_steps));
    } else {
        std::fprintf(stderr, "no se pudo guardar la grabación incompleta: %s\n", error.c_str());
    }
    std::fflush(stderr);
    g_saving = false;
    return ok;
}

} // namespace rally::io
