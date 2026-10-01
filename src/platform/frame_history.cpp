#include "platform/frame_history.h"

#include "platform/fixed_step_clock.h"

#include <cstddef>

namespace rally::platform {

void FrameHistory::push(const FrameRecord& record) {
    records_[static_cast<std::size_t>(next_)] = record;
    next_ = (next_ + 1) % kCapacity;
    if (size_ < kCapacity) {
        ++size_;
    }
}

const FrameRecord& FrameHistory::at(int index) const {
    const int oldest = size_ < kCapacity ? 0 : next_;
    return records_[static_cast<std::size_t>((oldest + index) % kCapacity)];
}

bool FrameHistory::is_dropped(const FrameRecord& record, std::int64_t frame_period) {
    return frame_period > 0 && 2 * record.measured_ns * kStepsPerSecond >= 3 * frame_period;
}

FrameCounts FrameHistory::count(std::int64_t frame_period) const {
    FrameCounts counts;
    counts.frames = size_;
    if (frame_period <= 0 || frame_period % kStepUnits != 0) {
        return counts;
    }
    counts.expected_steps = static_cast<int>(frame_period / kStepUnits);
    for (int i = 0; i < size_; ++i) {
        const FrameRecord& record = at(i);
        if (record.steps == counts.expected_steps) {
            continue;
        }
        ++counts.off_steps;
        if (is_dropped(record, frame_period)) {
            ++counts.dropped;
        } else {
            ++counts.noise;
        }
    }
    return counts;
}

} // namespace rally::platform
