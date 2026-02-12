// test_utils.cpp — Unit tests for clock.h and latency_histogram.h utilities

#include <gtest/gtest.h>

#include "utils/clock.h"
#include "utils/latency_histogram.h"

// ===========================================================================
// clock.h tests
// ===========================================================================

TEST(Clock, RdtscReturnsNonZero) {
    uint64_t tsc = hft::rdtsc();
    EXPECT_GT(tsc, 0u);
}

TEST(Clock, RdtscFencedReturnsNonZero) {
    uint64_t tsc = hft::rdtsc_fenced();
    EXPECT_GT(tsc, 0u);
}

TEST(Clock, RdtscIsMonotonic) {
    uint64_t a = hft::rdtsc_fenced();
    uint64_t b = hft::rdtsc_fenced();
    EXPECT_GE(b, a);
}

TEST(Clock, RdtscAdvancesOverTime) {
    uint64_t a = hft::rdtsc_fenced();
    // Busy-wait for a tiny bit to ensure TSC advances
    volatile int x = 0;
    for (int i = 0; i < 1000; ++i) x += i;
    (void)x;
    uint64_t b = hft::rdtsc_fenced();
    EXPECT_GT(b, a);
}

TEST(Clock, CalibrateTscFrequency) {
    double freq = hft::calibrate_tsc_frequency();
    // Expect between 0.5 and 8.0 ticks/ns (500 MHz to 8 GHz TSC)
    EXPECT_GT(freq, 0.5);
    EXPECT_LT(freq, 8.0);
}

TEST(Clock, CalibrationIsStable) {
    double freq1 = hft::calibrate_tsc_frequency();
    double freq2 = hft::calibrate_tsc_frequency();
    // Two calibrations should agree within 5%
    double ratio = freq1 / freq2;
    EXPECT_GT(ratio, 0.95);
    EXPECT_LT(ratio, 1.05);
}

// ===========================================================================
// latency_histogram.h tests
// ===========================================================================

TEST(LatencyHistogram, EmptyHistogram) {
    hft::LatencyHistogram hist;
    hist.set_tsc_frequency(3.0);
    auto stats = hist.compute();
    EXPECT_EQ(stats.sample_count, 0u);
    EXPECT_DOUBLE_EQ(stats.min_ns, 0.0);
    EXPECT_DOUBLE_EQ(stats.max_ns, 0.0);
    EXPECT_DOUBLE_EQ(stats.mean_ns, 0.0);
}

TEST(LatencyHistogram, SingleSample) {
    hft::LatencyHistogram hist;
    hist.set_tsc_frequency(3.0);  // 3 ticks/ns
    hist.record(300);             // 300 ticks = 100 ns
    auto stats = hist.compute();
    EXPECT_EQ(stats.sample_count, 1u);
    EXPECT_DOUBLE_EQ(stats.p50_ns, 100.0);
    EXPECT_DOUBLE_EQ(stats.min_ns, 100.0);
    EXPECT_DOUBLE_EQ(stats.max_ns, 100.0);
    EXPECT_DOUBLE_EQ(stats.mean_ns, 100.0);
}

TEST(LatencyHistogram, TwoSamples) {
    hft::LatencyHistogram hist;
    hist.set_tsc_frequency(1.0);  // 1 tick = 1 ns
    hist.record(100);
    hist.record(200);
    auto stats = hist.compute();
    EXPECT_EQ(stats.sample_count, 2u);
    EXPECT_DOUBLE_EQ(stats.min_ns, 100.0);
    EXPECT_DOUBLE_EQ(stats.max_ns, 200.0);
    EXPECT_DOUBLE_EQ(stats.mean_ns, 150.0);
}

TEST(LatencyHistogram, KnownDistribution) {
    hft::LatencyHistogram hist(1000);
    hist.set_tsc_frequency(1.0);  // 1 tick = 1 ns

    // Insert 1000 samples: 1, 2, 3, ..., 1000
    for (uint64_t i = 1; i <= 1000; ++i) {
        hist.record(i);
    }

    auto stats = hist.compute();
    EXPECT_EQ(stats.sample_count, 1000u);
    EXPECT_DOUBLE_EQ(stats.min_ns, 1.0);
    EXPECT_DOUBLE_EQ(stats.max_ns, 1000.0);

    // p50 of 1..1000: index = floor(0.50 * 999) = 499 -> value 500
    EXPECT_NEAR(stats.p50_ns, 500.0, 1.0);
    // p90: index = floor(0.90 * 999) = 899 -> value 900
    EXPECT_NEAR(stats.p90_ns, 900.0, 1.0);
    // p99: index = floor(0.99 * 999) = 989 -> value 990
    EXPECT_NEAR(stats.p99_ns, 990.0, 1.0);
    // p99.9: index = floor(0.999 * 999) = 998 -> value 999
    EXPECT_NEAR(stats.p99_9_ns, 999.0, 1.0);
    // mean of 1..1000 = 500.5
    EXPECT_NEAR(stats.mean_ns, 500.5, 0.1);
}

TEST(LatencyHistogram, UnsortedInput) {
    hft::LatencyHistogram hist(100);
    hist.set_tsc_frequency(2.0);  // 2 ticks/ns

    // Insert out of order
    hist.record(600);  // 300 ns
    hist.record(200);  // 100 ns
    hist.record(400);  // 200 ns

    auto stats = hist.compute();
    EXPECT_DOUBLE_EQ(stats.min_ns, 100.0);
    EXPECT_DOUBLE_EQ(stats.max_ns, 300.0);
}

TEST(LatencyHistogram, ClearResetsState) {
    hft::LatencyHistogram hist;
    hist.set_tsc_frequency(1.0);
    hist.record(100);
    hist.record(200);
    EXPECT_EQ(hist.size(), 2u);

    hist.clear();
    EXPECT_EQ(hist.size(), 0u);

    auto stats = hist.compute();
    EXPECT_EQ(stats.sample_count, 0u);
}

TEST(LatencyHistogram, SizeTracking) {
    hft::LatencyHistogram hist;
    EXPECT_EQ(hist.size(), 0u);
    hist.record(1);
    EXPECT_EQ(hist.size(), 1u);
    hist.record(2);
    hist.record(3);
    EXPECT_EQ(hist.size(), 3u);
}
