# CLAUDE.md

## Architecture

```
src/
  core/          — Order, Trade, PriceLevel, enums (Side, OrderType, TimeInForce, InstrumentId)
  orderbook/     — OrderBook (per-instrument), PriceLevelPool, MemoryPool
  matching/      — MatchingEngine, order validation, self-trade prevention
  gateway/       — OrderGateway, MarketDataPublisher, InstrumentRegistry, InstrumentRouter
  transport/     — SPSC ring buffer, MPSC queue, message serialization
  feed/          — L3 feed parser, single- and multi-instrument replay engines
  analytics/     — Spread, microprice, imbalance, realized vol, impact curves (single + multi-instrument)
  utils/         — Clock (rdtsc-based), logging, config
python/
  bindings/      — pybind11 bindings (module.cpp, core/orderbook/replay/analytics bindings)
  examples/      — Python example scripts (simple_replay, analytics_demo, multi_instrument)
tests/           — Google Test unit tests for every component
benchmarks/      — Google Benchmark + custom latency profiling
data/            — Sample L3 order data (single- and multi-instrument)
```

The engine supports multiple instruments simultaneously. `OrderBook` and `MatchingEngine` remain single-instrument classes — multi-instrument awareness lives in the `InstrumentRouter` layer, which owns a per-instrument pipeline (book + pool + engine + gateway) and dispatches orders via O(1) flat-array lookup. All instruments share a single `EventBuffer`; the `instrument_id` field in each `EventMessage` header distinguishes them.

The hot path (order ingestion → matching → market data output) is strictly separated from the cold path (analytics, logging, configuration). This separation is architectural, not just conventional — different compiler flags, different allowed constructs, different performance contracts.

## Hot Path vs Cold Path

**Hot path** (`core/`, `orderbook/`, `matching/`, `transport/`):
- Zero heap allocations after startup. Pre-allocated memory pools and slab allocators only.
- No virtual functions, no `std::string`, no `std::map`, no `shared_ptr`
- No exceptions (`-fno-exceptions`), no RTTI (`-fno-rtti`)
- Lock-free SPSC ring buffers for inter-thread communication
- Fixed-size binary POD messages — no serialization overhead
- Cache-line padded structures to prevent false sharing
- Compiled with `/W4 /WX` (MSVC) or `-Wall -Wextra -Wpedantic -Werror` (GCC/Clang) — zero warnings

**Cold path** (`analytics/`, `feed/`, `utils/`, config, I/O):
- Normal C++17 with STL containers, readability first
- spdlog for logging, nlohmann/json for output
- TBB thread pool for analytics computation
- Also compiled with `/W4 /WX` (MSVC) or `-Wall -Wextra -Wpedantic -Werror` (GCC/Clang) — zero warnings

## Code Style & Conventions

- **C++17** — not C++20/23. This is intentional.
- Google C++ Style Guide as baseline, with these modifications:
  - `snake_case` for functions and variables, `PascalCase` for types/classes
  - POD types and trivially copyable structures on the hot path
  - Use `[[nodiscard]]`, `[[likely]]`, `[[unlikely]]` attributes
  - `constexpr` everything that can be `constexpr`
- All public APIs must have doc comments
- Every component must have unit tests

## Build & Run

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build build -- -j$(nproc)

# Matching engine benchmark
./build/bench_orderbook

# Replay historical data with analytics (single-instrument)
./build/replay --input data/btcusdt_l3_sample.csv --analytics

# Replay multi-instrument data (auto-detected from 7-column CSV with "symbol" header)
./build/replay --input data/multi_instrument_l3_sample.csv --analytics

# Unit tests
cd build && ctest --output-on-failure

# Python bindings (requires Python 3.7+)
cmake -B build -DBUILD_PYTHON_BINDINGS=ON -DCMAKE_BUILD_TYPE=Release -G Ninja
cmake --build build --target hft_python

# Python smoke test
cd python && python -c "import hft_orderbook; print(hft_orderbook.PRICE_SCALE)"

# Python examples
python python/examples/simple_replay.py
python python/examples/analytics_demo.py
python python/examples/multi_instrument.py
```

## Dependencies

| Library | Purpose | Path |
|---|---|---|
| Google Test | Unit testing | Cold |
| Google Benchmark | Latency/throughput measurement | Cold |
| nlohmann/json | Config and analytics output | Cold |
| spdlog | Logging | Cold |
| TBB | Thread pool for analytics | Cold |
| Eigen3 | Analytics math (optional) | Cold |
| pybind11 | Python bindings (optional, `BUILD_PYTHON_BINDINGS=ON`) | Cold |

No external dependencies on the hot path.

## Priorities

1. **Hot path performance** — sub-microsecond median latency for add/cancel/modify/match. Every allocation, branch, and cache miss matters.
2. **Correct matching semantics** — price-time priority, proper handling of all order types (limit, market, IOC, FOK, iceberg), order modify (cancel-and-replace)
3. **Hot/cold separation** — zero-alloc lock-free hot path vs normal C++ cold path must be architecturally enforced
4. **Benchmarking rigor** — latency histograms with p50/p99/p99.9/max, throughput measurements, cache-miss analysis via `perf stat`
5. **Market microstructure analytics** — spread, microprice, order flow imbalance, realized volatility, price impact

## Key Domain Concepts

- **Price-time priority**: FIFO within each price level
- **Order types**: Limit, Market, IOC, FOK, GTC, Iceberg (hidden quantity)
- **Self-trade prevention**: detect and prevent by participant ID
- **InstrumentId**: `uint32_t` identifier for each tradable instrument; `DEFAULT_INSTRUMENT_ID = 0` for backward compatibility
- **InstrumentRouter**: O(1) dispatch layer mapping `InstrumentId` → per-instrument pipeline (`OrderBook` + `MatchingEngine` + `OrderGateway`)
- **L3 data**: order-by-order feed format (individual add/modify/cancel/trade messages)
- **Multi-instrument CSV**: 7-column format (`symbol,timestamp,event_type,order_id,side,price,quantity`); auto-detected, backward-compatible with 6-column single-instrument format
- **Microprice**: `(bid_size * ask_price + ask_size * bid_price) / (bid_size + ask_size)`
- **Order flow imbalance**: `(buy_volume - sell_volume) / (buy_volume + sell_volume)`
- **Kyle's Lambda**: price impact coefficient from regressing price changes on signed order flow

## Performance Targets

| Operation | Median | p99 |
|---|---|---|
| Add order (no match) | < 100 ns | < 500 ns |
| Cancel order | < 50 ns | < 200 ns |
| Match + trade | < 200 ns | < 1 us |
| Throughput | > 5M msgs/sec | — |

## References

1. Harris, *Trading and Exchanges: Market Microstructure for Practitioners*
2. Cartea, Jaimungal & Penalva, *Algorithmic and High-Frequency Trading*
3. Ghosh, *Building Low Latency Applications with C++* (Packt)
4. Kyle, "Continuous Auctions and Insider Trading", Econometrica 1985
