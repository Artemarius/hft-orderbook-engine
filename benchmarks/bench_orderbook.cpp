#include <benchmark/benchmark.h>

#include "core/types.h"

// Placeholder benchmarks — replaced with real order book benchmarks in Phase 2.

static void BM_PriceConversion(benchmark::State& state) {
    for (auto _ : state) {
        hft::Price price = 50000 * hft::PRICE_SCALE + hft::PRICE_SCALE / 2;
        benchmark::DoNotOptimize(price);
    }
}
BENCHMARK(BM_PriceConversion);

static void BM_SideComparison(benchmark::State& state) {
    hft::Side buy = hft::Side::Buy;
    hft::Side sell = hft::Side::Sell;
    for (auto _ : state) {
        bool result = (buy != sell);
        benchmark::DoNotOptimize(result);
    }
}
BENCHMARK(BM_SideComparison);

BENCHMARK_MAIN();
