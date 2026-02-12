#pragma once

// latency_histogram.h — Collect individual latency samples and compute percentiles
//
// Cold-path utility: uses std::vector and std::sort.
// Records raw TSC tick deltas, converts to nanoseconds on compute().

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace hft {

/// Percentile latency statistics in nanoseconds.
struct LatencyStats {
    double min_ns = 0.0;
    double max_ns = 0.0;
    double mean_ns = 0.0;
    double p50_ns = 0.0;
    double p90_ns = 0.0;
    double p99_ns = 0.0;
    double p99_9_ns = 0.0;
    size_t sample_count = 0;
};

/// Collects individual latency samples (in TSC ticks) and computes
/// percentile statistics. Cold-path utility — uses std::vector and std::sort.
class LatencyHistogram {
public:
    explicit LatencyHistogram(size_t reserve_count = 1'000'000)
        : tsc_per_ns_(1.0) {
        samples_.reserve(reserve_count);
    }

    /// Set TSC-to-nanosecond conversion factor (from calibrate_tsc_frequency()).
    void set_tsc_frequency(double tsc_per_ns) { tsc_per_ns_ = tsc_per_ns; }

    /// Set rdtsc overhead to subtract from each sample (from measure_rdtsc_overhead()).
    void set_overhead(uint64_t overhead_ticks) { overhead_ = overhead_ticks; }

    /// Record a single latency sample in raw TSC ticks.
    /// Overhead is subtracted automatically if set via set_overhead().
    void record(uint64_t tsc_ticks) {
        uint64_t adjusted = (tsc_ticks > overhead_) ? tsc_ticks - overhead_ : 0;
        samples_.push_back(adjusted);
    }

    /// Sort samples and compute percentile statistics.
    /// Returns all values converted to nanoseconds.
    [[nodiscard]] LatencyStats compute() {
        LatencyStats stats{};
        stats.sample_count = samples_.size();
        if (samples_.empty()) return stats;

        std::sort(samples_.begin(), samples_.end());

        stats.min_ns = to_ns(samples_.front());
        stats.max_ns = to_ns(samples_.back());
        stats.p50_ns = to_ns(percentile(0.50));
        stats.p90_ns = to_ns(percentile(0.90));
        stats.p99_ns = to_ns(percentile(0.99));
        stats.p99_9_ns = to_ns(percentile(0.999));

        double sum = 0.0;
        for (auto s : samples_) sum += to_ns(s);
        stats.mean_ns = sum / static_cast<double>(stats.sample_count);

        return stats;
    }

    /// Discard all recorded samples.
    void clear() { samples_.clear(); }

    /// Number of recorded samples.
    [[nodiscard]] size_t size() const { return samples_.size(); }

private:
    [[nodiscard]] double to_ns(uint64_t ticks) const {
        return static_cast<double>(ticks) / tsc_per_ns_;
    }

    [[nodiscard]] uint64_t percentile(double p) const {
        size_t idx = static_cast<size_t>(
            std::floor(p * static_cast<double>(samples_.size() - 1)));
        return samples_[idx];
    }

    std::vector<uint64_t> samples_;
    double tsc_per_ns_;
    uint64_t overhead_ = 0;
};

}  // namespace hft
