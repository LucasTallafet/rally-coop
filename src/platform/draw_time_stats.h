#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace rally::platform {

// CPU de un dibujo por fotograma, en ms: el último, la media y el máximo de los últimos
// kFrames fotogramas, y la media y el máximo desde reset_totals(). Lo usan ZoneDraw y StageDraw.
class DrawTimeStats {
public:
    static constexpr int kFrames = 240;

    DrawTimeStats() : history_(kFrames, 0.0) {}

    void record(double ms) {
        last_ = ms;
        history_[next_] = ms;
        next_ = (next_ + 1) % history_.size();
        double sum = 0.0;
        double peak = 0.0;
        for (const double v : history_) {
            sum += v;
            peak = std::max(peak, v);
        }
        mean_ = sum / static_cast<double>(history_.size());
        max_ = peak;
        total_ += ms;
        ++total_frames_;
        total_max_ = std::max(total_max_, ms);
    }
    void reset_totals() {
        total_ = 0.0;
        total_frames_ = 0;
        total_max_ = 0.0;
    }

    double last() const { return last_; }
    double mean() const { return mean_; }
    double max() const { return max_; }
    double total_mean() const {
        return total_frames_ > 0 ? total_ / static_cast<double>(total_frames_) : 0.0;
    }
    double total_max() const { return total_max_; }

private:
    double last_ = 0.0;
    double mean_ = 0.0;
    double max_ = 0.0;
    std::vector<double> history_;
    std::size_t next_ = 0;
    double total_ = 0.0;
    std::int64_t total_frames_ = 0;
    double total_max_ = 0.0;
};

} // namespace rally::platform
