#pragma once

// clock.h — High-resolution timing utilities for latency measurement
//
// Provides multiple rdtsc variants for different accuracy/overhead tradeoffs:
//   rdtsc()        — raw, ~1 cycle overhead, may reorder
//   rdtsc_start()  — LFENCE + RDTSC, ~5 cycle overhead, prevents reorder before
//   rdtsc_end()    — RDTSCP variant, prevents reorder after
//   rdtsc_fenced() — CPUID + RDTSC, ~150 cycle overhead, full serialization
//
// measure_rdtsc_overhead() — calibrate the cost of the timing instrumentation
// calibrate_tsc_frequency() — measure TSC ticks per nanosecond
//
// Hot-path compatible: no STL, no exceptions, no allocations (except calibration).

#include <cstdint>

#ifdef _MSC_VER
#include <intrin.h>
#include <immintrin.h>
#else
#include <x86intrin.h>
#endif

#include <chrono>

namespace hft {

/// Read the CPU timestamp counter (raw, no serialization).
/// Minimal overhead (~1 cycle) but may be reordered by the CPU.
inline uint64_t rdtsc() noexcept {
    return __rdtsc();
}

/// Lightweight serializing rdtsc for the START of a measurement window.
/// Issues LFENCE before RDTSC to drain the store buffer and prevent
/// earlier instructions from being measured.
/// Overhead: ~5-10 cycles (vs ~150 for CPUID-based rdtsc_fenced).
inline uint64_t rdtsc_start() noexcept {
    _mm_lfence();
    return __rdtsc();
}

/// RDTSCP for the END of a measurement window.
/// RDTSCP waits until all prior instructions have executed before reading
/// the timestamp counter. Paired with rdtsc_start() for low-overhead timing.
inline uint64_t rdtsc_end() noexcept {
    unsigned int aux;
#ifdef _MSC_VER
    uint64_t tsc = __rdtscp(&aux);
#else
    uint64_t tsc;
    __asm__ __volatile__("rdtscp" : "=A"(tsc), "=c"(aux));
#endif
    _mm_lfence();  // Prevent subsequent instructions from reordering before
    return tsc;
}

/// Heavy serializing rdtsc — CPUID before the read.
/// Most accurate for very short measurements, but ~150 cycle overhead.
/// Use rdtsc_start/rdtsc_end for lower overhead when sufficient.
inline uint64_t rdtsc_fenced() noexcept {
#ifdef _MSC_VER
    int cpu_info[4];
    __cpuid(cpu_info, 0);
#else
    unsigned int eax, ebx, ecx, edx;
    __asm__ __volatile__("cpuid"
                         : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx)
                         : "a"(0));
#endif
    return __rdtsc();
}

/// Measure the overhead of the rdtsc_start/rdtsc_end pair itself.
/// Call once during startup. Subtract from measured samples for accuracy.
/// Returns median overhead in TSC ticks over 10K samples.
inline uint64_t measure_rdtsc_overhead() {
    constexpr int N = 10'000;
    uint64_t samples[N];

    // Warmup
    for (int i = 0; i < 1000; ++i) {
        uint64_t t0 = rdtsc_start();
        uint64_t t1 = rdtsc_end();
        (void)(t1 - t0);
    }

    // Collect
    for (int i = 0; i < N; ++i) {
        uint64_t t0 = rdtsc_start();
        uint64_t t1 = rdtsc_end();
        samples[i] = t1 - t0;
    }

    // Simple insertion sort on first 1000 elements to find median
    // (we only need approximate median, not full sort of 10K)
    constexpr int SORT_N = 1000;
    for (int i = 1; i < SORT_N; ++i) {
        uint64_t key = samples[i];
        int j = i - 1;
        while (j >= 0 && samples[j] > key) {
            samples[j + 1] = samples[j];
            --j;
        }
        samples[j + 1] = key;
    }
    return samples[SORT_N / 2];  // Median of first 1000
}

/// Calibrate TSC frequency by measuring against std::chrono over a short interval.
/// Returns approximate TSC ticks per nanosecond.
///
/// Cold-path only — call once during startup, NOT on the hot path.
/// Uses multiple samples with warmup for stable results.
inline double calibrate_tsc_frequency() {
    using clock = std::chrono::high_resolution_clock;
    constexpr int WARMUP = 5;
    constexpr int SAMPLES = 10;
    constexpr auto DURATION = std::chrono::milliseconds(10);

    // Warmup — stabilize CPU frequency and caches
    for (int i = 0; i < WARMUP; ++i) {
        auto start = clock::now();
        uint64_t tsc_start = __rdtsc();
        while (clock::now() - start < DURATION) {
        }
        (void)(tsc_start);
    }

    double total_ratio = 0.0;
    for (int i = 0; i < SAMPLES; ++i) {
        auto start = clock::now();
        uint64_t tsc_start = __rdtsc();
        while (clock::now() - start < DURATION) {
        }
        uint64_t tsc_end = __rdtsc();
        auto end = clock::now();

        double ns = static_cast<double>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
                .count());
        double ticks = static_cast<double>(tsc_end - tsc_start);
        total_ratio += ticks / ns;
    }
    return total_ratio / SAMPLES;  // ticks per nanosecond
}

}  // namespace hft
