# hft-orderbook-engine

A high-performance limit order book and matching engine in C++17 with lock-free concurrency, zero-allocation hot path, and market microstructure analytics. Designed for sub-microsecond latency.

## Motivation

I wanted to understand market microstructure from the ground up — not by reading about it, but by building the core infrastructure myself. A limit order book is where price discovery actually happens: every bid, every ask, every match. Building one forces you to think about cache lines, memory layout, and nanosecond budgets while simultaneously learning the financial mechanics of how markets work.

## What's Implemented

**Matching Engine**
- Price-time priority (FIFO) matching
- Order types: Limit, Market, Immediate-or-Cancel, Fill-or-Kill, Good-Till-Cancel, Iceberg (hidden quantity)
- Self-trade prevention by participant ID
- Full order lifecycle: New → Ack → Partial Fill → Fill/Cancel, with event generation at each transition

**Low-Latency Architecture**
- Zero heap allocations on the hot path — pre-allocated memory pool / slab allocator for all Order objects
- Cache-optimized price level storage (contiguous sorted array, not `std::map`)
- O(1) best bid/ask access
- No virtual functions, no exceptions, no RTTI on the hot path
- Lock-free SPSC ring buffer for inter-thread communication, cache-line padded to prevent false sharing
- Fixed-size binary POD messages — no serialization overhead

**Data Replay**
- Ingest historical L3 (order-by-order) data from Binance CSV dumps
- Reconstruct full order book state at any point in time
- Validate matching behavior against known exchange sequences

**Market Microstructure Analytics**
- Bid-ask spread and effective spread over time
- Microprice: size-weighted midpoint as fair value estimator
- Order flow imbalance (buy vs sell volume)
- Realized volatility (tick-level and time-bar)
- Price impact curves and Kyle's Lambda estimation
- Order book depth and shape analysis
- Output: JSON summary + CSV time series

**Benchmarking**
- Nanosecond-precision latency histograms (p50, p90, p99, p99.9, max)
- Throughput under sustained load
- Cache-miss profiling with `perf stat`

## Performance

*Targets (measured on [hardware TBD]):*

| Operation | Median | p99 |
|---|---|---|
| Add order (no match) | < 100 ns | < 500 ns |
| Cancel order | < 50 ns | < 200 ns |
| Match + trade | < 200 ns | < 1 μs |
| Throughput | > 5M msgs/sec | — |

## Building

### Prerequisites
- C++17 compiler (GCC 9+, Clang 10+)
- CMake 3.20+
- Linux recommended (for `perf` profiling and `rdtsc` timing)

### Build
```bash
git clone https://github.com/<username>/hft-orderbook-engine.git
cd hft-orderbook-engine

cmake -B build -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build build -- -j$(nproc)
```

### Run
```bash
# Matching engine benchmark
./build/bench_orderbook

# Replay historical data with analytics
./build/replay --input data/btcusdt_l3.csv --output analytics/

# Unit tests
cd build && ctest --output-on-failure
```

## Project Structure

```
src/
  core/        — Order, Trade, PriceLevel, Side/OrderType enums
  orderbook/   — OrderBook, PriceLevelPool, MemoryPool (slab allocator)
  matching/    — MatchingEngine, validation, self-trade prevention
  gateway/     — OrderGateway (ingestion), MarketDataPublisher
  transport/   — SPSC ring buffer, MPSC queue, binary message format
  feed/        — L3 data replay from CSV
  analytics/   — Spread, microprice, imbalance, volatility, impact
  utils/       — High-resolution clock, logging, config
tests/         — Google Test (per-component)
benchmarks/    — Google Benchmark + custom latency profiling
data/          — Sample L3 order data
```

## Architecture

The hot path (order ingestion → matching → market data output) is strictly separated from the cold path (analytics, logging, configuration):

**Hot path constraints:** no heap allocation, no virtual dispatch, no exceptions, no `std::string`, no `std::map`, no `shared_ptr`. Communication between threads via lock-free SPSC ring buffers with cache-line padding.

**Cold path:** normal C++17 with STL containers, spdlog, JSON output. Analytics run on a separate thread consuming market data events from the ring buffer.

## Dependencies

| Library | Purpose | Path |
|---|---|---|
| Google Test | Unit tests | Cold |
| Google Benchmark | Performance measurement | Cold |
| nlohmann/json | Config, analytics output | Cold |
| spdlog | Logging | Cold |
| TBB | Thread pool for analytics | Cold |

No external dependencies on the hot path.

## References

1. Harris, *Trading and Exchanges: Market Microstructure for Practitioners*
2. Cartea, Jaimungal & Penalva, *Algorithmic and High-Frequency Trading*
3. Ghosh, *Building Low Latency Applications with C++* (Packt)
4. Kyle, "Continuous Auctions and Insider Trading", Econometrica 1985

## License

MIT
