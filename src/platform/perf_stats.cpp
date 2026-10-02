#include "platform/perf_stats.h"

#include <algorithm>

namespace rally::platform {

void RunningTotal::add(std::int64_t group_sum, std::int64_t group_count, std::int64_t group_max) {
    if (group_count <= 0) {
        return;
    }
    max = count == 0 ? group_max : std::max(max, group_max);
    sum += group_sum;
    count += group_count;
}

double RunningTotal::mean() const {
    return count == 0 ? 0.0 : static_cast<double>(sum) / static_cast<double>(count);
}

void RollingMax::add(std::int64_t now_ns, std::int64_t value) {
    const std::int64_t index = now_ns / kBucketNs;
    Bucket& bucket = buckets_[static_cast<std::size_t>(index % kBuckets)];
    if (bucket.index != index) {
        bucket.index = index;
        bucket.max = value;
    } else {
        bucket.max = std::max(bucket.max, value);
    }
}

std::int64_t RollingMax::max(std::int64_t now_ns) const {
    const std::int64_t newest = now_ns / kBucketNs;
    bool found = false;
    std::int64_t result = 0;
    for (const Bucket& bucket : buckets_) {
        if (bucket.index >= 0 && bucket.index > newest - kBuckets && bucket.index <= newest) {
            result = found ? std::max(result, bucket.max) : bucket.max;
            found = true;
        }
    }
    return result;
}

void PerfChannel::add(std::int64_t now_ns, std::int64_t group_sum, std::int64_t group_count,
                      std::int64_t group_max) {
    if (group_count <= 0) {
        return;
    }
    period_.add(group_sum, group_count, group_max);
    rolling_.add(now_ns, group_max);
}

void PerfChannel::publish(std::int64_t now_ns) {
    if (period_.count > 0) {
        average_ = period_.mean();
        has_data_ = true;
    }
    max_ = rolling_.max(now_ns);
    period_ = RunningTotal{};
}

bool PerfStats::push(std::int64_t now_ns, const FrameSample& sample) {
    frame_.add(now_ns, sample.frame_ns);
    sim_.add(now_ns, sample.sim_ns);
    step_.add(now_ns, sample.sim_ns, sample.steps, sample.step_max_ns);
    steps_.add(now_ns, sample.steps);
    render_.add(now_ns, sample.render_ns);
    present_.add(now_ns, sample.present_ns);
    other_.add(now_ns, sample.other_ns());
    if (sample.gpu_ns >= 0) {
        gpu_.add(now_ns, sample.gpu_ns);
    }
    draw_calls_.add(now_ns, sample.draw_calls);
    instanced_calls_.add(now_ns, sample.instanced_calls);
    instances_.add(now_ns, sample.instances);

    if (period_start_ns_ < 0) {
        // El primer fotograma abre el periodo: los fps cuentan los que acaban después.
        period_start_ns_ = now_ns;
        return false;
    }
    ++period_frames_;
    const std::int64_t elapsed = now_ns - period_start_ns_;
    if (elapsed < kPeriodNs) {
        return false;
    }
    fps_ = period_frames_ * 1e9 / static_cast<double>(elapsed);
    for (PerfChannel* channel : {&frame_, &sim_, &step_, &steps_, &render_, &present_, &other_,
                                 &gpu_, &draw_calls_, &instanced_calls_, &instances_}) {
        channel->publish(now_ns);
    }
    period_start_ns_ = now_ns;
    period_frames_ = 0;
    return true;
}

void PerfTotals::add(const FrameSample& sample) {
    ++frames;
    frame.add(sample.frame_ns);
    sim.add(sample.sim_ns);
    step.add(sample.sim_ns, sample.steps, sample.step_max_ns);
    render.add(sample.render_ns);
    present.add(sample.present_ns);
    other.add(sample.other_ns());
    if (sample.gpu_ns >= 0) {
        gpu.add(sample.gpu_ns);
    }
    draw_calls.add(sample.draw_calls);
    instances.add(sample.instances);
}

} // namespace rally::platform
