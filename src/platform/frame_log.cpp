#include "platform/frame_log.h"

#include "platform/fixed_step_clock.h"

#include <raylib.h>

#include <cinttypes>

namespace rally::platform {

namespace {

double units_to_ns(std::int64_t units) {
    return static_cast<double>(units) / static_cast<double>(kStepsPerSecond);
}

} // namespace

FrameLog::~FrameLog() { stop(); }

bool FrameLog::start() {
    stop();
    int number = 0;
    while (FileExists(TextFormat("%s/fotogramas_%03d.csv", GetApplicationDirectory(), number))) {
        ++number;
    }
    std::snprintf(name_, sizeof(name_), "fotogramas_%03d.csv", number);
    if (fopen_s(&file_, TextFormat("%s/%s", GetApplicationDirectory(), name_), "w") != 0) {
        file_ = nullptr;
        name_[0] = '\0';
        return false;
    }
    frame_ = 0;
    std::fputs("frame,measured_ns,used_ns,steps,snapped,alpha,debt_ns,snap_period_ns,"
               "frame_period_ns\n",
               file_);
    return true;
}

void FrameLog::stop() {
    if (file_ != nullptr) {
        std::fclose(file_);
        file_ = nullptr;
    }
    name_[0] = '\0';
}

void FrameLog::write(std::int64_t frame_ns, const FrameSteps& steps, std::int64_t debt,
                     std::int64_t snap_period, std::int64_t frame_period) {
    if (file_ == nullptr) {
        return;
    }
    std::fprintf(file_, "%" PRId64 ",%" PRId64 ",%.1f,%d,%d,%.4f,%.1f,%.1f,%.1f\n", frame_,
                 frame_ns, units_to_ns(steps.used), steps.count, steps.snapped ? 1 : 0, steps.alpha,
                 units_to_ns(debt), units_to_ns(snap_period), units_to_ns(frame_period));
    std::fflush(file_);
    ++frame_;
}

} // namespace rally::platform
