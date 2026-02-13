# ROADMAP.md — hft-orderbook-engine

> Development roadmap for building a low-latency order book and matching engine in C++17.
> This file lives in the repo root alongside CLAUDE.md, PROJECT.md, and README.md.
> Use it as the primary reference when working with Claude Code.

---

## Phase 0: Environment & Scaffold

**Goal:** Working build system, compiling "hello world" test and benchmark, all tooling verified.

### 0.1 WSL2 Setup (manual, one-time)

```bash
# In PowerShell (admin)
wsl --install -d Ubuntu-24.04

# Inside WSL2
sudo apt update && sudo apt upgrade -y
sudo apt install -y \
  build-essential gcc-13 g++-13 clang-17 clang-format-17 clang-tidy-17 \
  cmake ninja-build gdb valgrind \
  linux-tools-generic \
  git curl zip unzip pkg-config

# Set gcc-13 as default
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-13 100
sudo update-alternatives --install /usr/bin/g++ g++ /usr/bin/g++-13 100

# Verify
g++ --version    # should show 13.x
cmake --version  # should show 3.28+
ninja --version
```

### 0.2 Project Scaffold

Create the following directory structure. Every directory under `src/` gets its own `CMakeLists.txt` that produces a static library. Tests link against these libraries.

```
hft-orderbook-engine/
├── CMakeLists.txt              # Root CMake — project-level settings, compiler flags
├── cmake/
│   ├── CompilerFlags.cmake     # Hot-path vs cold-path flag sets
│   └── Dependencies.cmake      # FetchContent for GTest, GBench, spdlog, json, TBB
├── src/
│   ├── CMakeLists.txt          # Collects all sub-libraries
│   ├── core/
│   │   ├── CMakeLists.txt      # lib: hft_core
│   │   ├── types.h             # Side, OrderType, TimeInForce enums
│   │   ├── order.h             # Order struct (POD, fixed-size)
│   │   ├── trade.h             # Trade struct (POD)
│   │   └── price_level.h       # PriceLevel struct
│   ├── orderbook/
│   │   ├── CMakeLists.txt      # lib: hft_orderbook
│   │   ├── order_book.h / .cpp
│   │   ├── memory_pool.h       # Slab allocator for Order objects
│   │   └── price_level_pool.h  # Contiguous sorted price level storage
│   ├── matching/
│   │   ├── CMakeLists.txt      # lib: hft_matching
│   │   ├── matching_engine.h / .cpp
│   │   └── self_trade_prevention.h
│   ├── gateway/
│   │   ├── CMakeLists.txt      # lib: hft_gateway
│   │   ├── order_gateway.h / .cpp
│   │   └── market_data_publisher.h / .cpp
│   ├── transport/
│   │   ├── CMakeLists.txt      # lib: hft_transport
│   │   ├── spsc_ring_buffer.h  # Header-only, lock-free
│   │   └── message.h           # Fixed-size binary POD messages
│   ├── feed/
│   │   ├── CMakeLists.txt      # lib: hft_feed (cold path)
│   │   └── l3_replay.h / .cpp
│   ├── analytics/
│   │   ├── CMakeLists.txt      # lib: hft_analytics (cold path)
│   │   ├── spread.h / .cpp
│   │   ├── microprice.h / .cpp
│   │   ├── imbalance.h / .cpp
│   │   ├── realized_vol.h / .cpp
│   │   ├── price_impact.h / .cpp
│   │   └── depth_profile.h / .cpp
│   └── utils/
│       ├── CMakeLists.txt      # lib: hft_utils
│       ├── clock.h             # rdtsc + chrono wrappers
│       ├── logging.h           # spdlog wrapper (cold path only)
│       └── config.h            # nlohmann/json config loader
├── tests/
│   ├── CMakeLists.txt
│   ├── test_order.cpp
│   ├── test_memory_pool.cpp
│   ├── test_price_level.cpp
│   ├── test_order_book.cpp
│   ├── test_matching_engine.cpp
│   ├── test_spsc_ring_buffer.cpp
│   ├── test_l3_replay.cpp
│   └── test_analytics.cpp
├── benchmarks/
│   ├── CMakeLists.txt
│   ├── bench_orderbook.cpp
│   ├── bench_matching.cpp
│   └── bench_spsc.cpp
├── scripts/
│   ├── perf_cache_analysis.sh  # perf stat wrapper for cache miss measurement
│   ├── run_benchmarks.sh       # Build + run + collect results
│   └── download_data.sh        # Fetch Binance L3 CSV sample
├── data/
│   ├── btcusdt_l3_sample.csv   # Small sample checked into repo (~5K messages)
│   └── README.md               # Instructions to download full dataset
├── CLAUDE.md
├── PROJECT.md
├── README.md
├── ROADMAP.md                  # This file
├── .clang-format
├── .clang-tidy
└── .gitignore
```

### 0.3 CMake Architecture

Root `CMakeLists.txt` must enforce the hot/cold split at the build system level:

```cmake
# cmake/CompilerFlags.cmake

# Hot-path libraries: maximum optimization, no overhead
set(HOT_PATH_FLAGS
    -O3
    -march=native
    -fno-exceptions
    -fno-rtti
    -Wall -Wextra -Wpedantic -Werror
    -fno-omit-frame-pointer    # Keep frame pointers for perf profiling
)

# Cold-path libraries: optimize but allow full C++ features
set(COLD_PATH_FLAGS
    -O2
    -Wall -Wextra -Wpedantic
)

# Debug overlay (applied via CMAKE_BUILD_TYPE)
set(DEBUG_FLAGS
    -g
    -fsanitize=address,undefined
    -fno-omit-frame-pointer
)
```

Hot-path libraries (`hft_core`, `hft_orderbook`, `hft_matching`, `hft_transport`): compiled with `HOT_PATH_FLAGS`.
Cold-path libraries (`hft_feed`, `hft_analytics`, `hft_utils`, `hft_gateway`): compiled with `COLD_PATH_FLAGS`.

### 0.4 Dependencies via FetchContent

```cmake
# cmake/Dependencies.cmake
include(FetchContent)

FetchContent_Declare(googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.15.2)

FetchContent_Declare(googlebenchmark
    GIT_REPOSITORY https://github.com/google/benchmark.git
    GIT_TAG        v1.9.1
    CMAKE_ARGS     -DBENCHMARK_ENABLE_TESTING=OFF)

FetchContent_Declare(spdlog
    GIT_REPOSITORY https://github.com/gabime/spdlog.git
    GIT_TAG        v1.15.0)

FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG        v3.11.3)

# TBB — use system package or FetchContent
find_package(TBB QUIET)
if(NOT TBB_FOUND)
    FetchContent_Declare(tbb
        GIT_REPOSITORY https://github.com/oneapi-src/oneTBB.git
        GIT_TAG        v2022.0.0)
endif()

FetchContent_MakeAvailable(googletest googlebenchmark spdlog nlohmann_json)
```

### 0.5 Phase 0 Deliverables Checklist

- [x] Repo initialized with the directory structure above
- [x] Root CMakeLists.txt compiles with `cmake -B build -DCMAKE_BUILD_TYPE=Release -G Ninja`
- [x] Placeholder `hft_core` library builds (even if empty — just a types.h with enums)
- [x] One Google Test links and runs: `test_order.cpp` with a trivial assertion
- [x] One Google Benchmark links and runs: `bench_orderbook.cpp` with a trivial benchmark
- [x] `.clang-format` configured (Google style base, modified per CLAUDE.md conventions)
- [x] `.clang-tidy` configured with performance-relevant checks
- [x] `.gitignore` covers `build/`, IDE files, data downloads
- [ ] `perf stat ls` works inside WSL2 (verifies perf access)

---

## Phase 1: Core Types & Memory Pool

**Goal:** Order, Trade, PriceLevel as cache-friendly POD structs. Memory pool that guarantees zero heap allocation after warmup.

### 1.1 Core Types (`src/core/`)

**types.h** — enums and constants:
```
enum class Side : uint8_t { Buy, Sell };
enum class OrderType : uint8_t { Limit, Market, IOC, FOK, GTC, Iceberg };
enum class TimeInForce : uint8_t { GTC, IOC, FOK, DAY };
enum class OrderStatus : uint8_t { New, Accepted, PartialFill, Filled, Cancelled, Rejected };

using Price = int64_t;      // Fixed-point: price * 10^8 (avoid floating point)
using Quantity = uint64_t;
using OrderId = uint64_t;
using ParticipantId = uint32_t;
using Timestamp = uint64_t; // nanoseconds since epoch (or rdtsc ticks)
```

Key decisions:
- Price as `int64_t` fixed-point, not `double`. Floating point comparison bugs are a classic LOB mistake. Multiply by 10^8 to handle 8 decimal places (enough for crypto).
- All IDs are integer types, no strings anywhere near the hot path.
- Enums are `uint8_t` to minimize struct padding.

**order.h** — the Order struct:
- Must be fixed-size POD (trivially copyable, standard layout)
- Target size: ≤ 128 bytes (2 cache lines or less)
- Fields: `order_id`, `participant_id`, `side`, `type`, `time_in_force`, `price`, `quantity`, `visible_quantity` (for iceberg), `filled_quantity`, `timestamp`, `status`
- Include `static_assert(std::is_trivially_copyable_v<Order>)`
- Include `static_assert(sizeof(Order) <= 128)`
- Intrusive linked list pointer (`Order* next`) for FIFO queue within price levels

**trade.h** — the Trade struct:
- Fields: `trade_id`, `buy_order_id`, `sell_order_id`, `price`, `quantity`, `timestamp`
- Also POD, also `static_assert`'d

**price_level.h** — PriceLevel struct:
- `price`, `total_quantity`, `order_count`
- Head/tail pointers for the intrusive FIFO linked list of orders
- Methods: `add_order()`, `remove_order()`, `front()` — all O(1)

### 1.2 Memory Pool (`src/orderbook/memory_pool.h`)

Slab allocator for Order objects:
- Pre-allocate a contiguous block at startup (e.g. 1M Order slots = ~128 MB)
- `allocate()` returns pointer from free list — O(1), no syscall
- `deallocate()` pushes back to free list — O(1)
- Free list is intrusive (reuse the `next` pointer in Order when the slot is free)
- `static_assert` that the pool never calls `new`/`malloc` after construction
- Track high-water mark for diagnostics

### 1.3 Tests for Phase 1

- `test_order.cpp`: Verify POD properties, size constraints, field layout
- `test_memory_pool.cpp`: Allocate/deallocate cycles, verify no heap allocation (override global `operator new` in test to detect), pool exhaustion behavior, high-water mark tracking

### 1.4 Phase 1 Deliverables Checklist

- [x] All core types defined with `static_assert` guards
- [x] Price is fixed-point `int64_t`, not floating point
- [x] Order struct is ≤ 128 bytes (72 bytes actual), trivially copyable
- [x] Memory pool allocates/deallocates in O(1) with zero heap allocation
- [x] Tests pass: POD verification, alloc/dealloc correctness, pool exhaustion (38 tests)
- [x] No STL containers, no `std::string`, no virtual functions in any hot-path code

---

## Phase 2: Order Book

**Goal:** Per-instrument order book with O(1) best bid/ask, O(1) add at existing level, O(log n) add at new level. Cache-friendly storage.

### 2.1 Price Level Storage (`src/orderbook/price_level_pool.h`)

Two design options (implement the simpler one first, optimize if needed):

**Option A — Sorted flat array with direct-mapped index:**
- For instruments with bounded price ranges (e.g. BTC±10%), use a flat array indexed by price tick.
- `levels_[price_to_index(price)]` gives O(1) access.
- Excellent cache behavior for sequential scanning.
- Wastes memory if price range is wide, but for a single instrument this is fine.

**Option B — Sorted `std::vector<PriceLevel>` + binary search for insert, O(1) best:**
- Maintain a pointer/index to best bid and best ask.
- Insert at correct position with `std::lower_bound` + `insert` (rare operation relative to add-at-existing-level).
- More general but worse cache behavior for insert.

Start with Option A if targeting a single instrument (BTC/USDT). Document the tradeoff.

### 2.2 OrderBook Class (`src/orderbook/order_book.h`)

```
class OrderBook {
public:
    // Returns: matched trades (if any), order status
    AddResult add_order(Order* order);
    CancelResult cancel_order(OrderId id);

    // O(1) accessors
    const PriceLevel* best_bid() const;
    const PriceLevel* best_ask() const;
    Price spread() const;

    // Lookup
    Order* find_order(OrderId id) const; // hash map: OrderId -> Order*

private:
    PriceLevelArray bids_;   // Sorted descending (best bid = front)
    PriceLevelArray asks_;   // Sorted ascending (best ask = front)
    OrderMap order_map_;     // OrderId -> Order* for O(1) cancel lookup
};
```

**OrderMap** implementation: Use a flat hash map (e.g. a simple open-addressing hash table with linear probing, or Robin Hood hashing). Do NOT use `std::unordered_map` — it heap-allocates nodes. Write a custom one or use a pre-allocated flat array if OrderId space is bounded.

### 2.3 Tests for Phase 2

- `test_price_level.cpp`: Add/remove orders from a level, FIFO order, quantity tracking
- `test_order_book.cpp`:
  - Add bids and asks, verify best bid/ask and spread
  - Cancel orders, verify book state updates
  - Add order at existing price level vs new price level
  - Empty book edge cases
  - Order map lookup correctness

### 2.4 Phase 2 Deliverables Checklist

- [x] OrderBook with bid/ask sides, O(1) best bid/ask
- [x] Price level storage is contiguous flat array indexed by price tick (Option A)
- [x] Order lookup by ID via FlatOrderMap (open-addressing, backward-shift deletion)
- [x] All add/cancel operations verified with unit tests (32 new tests, 70 total)
- [x] Zero heap allocation verified (global `operator new` override in tests)
- [x] First benchmark: add=198ns, cancel=195ns, mixed=99ns

---

## Phase 3: Matching Engine

**Goal:** Price-time priority matching for all order types. Correct semantics, not just fast.

### 3.1 Matching Logic (`src/matching/matching_engine.h`)

Process each incoming order:

1. **Limit order with potential cross:**
   - While order has remaining quantity AND opposite side has levels at crossable prices:
     - Match against the front order at the best opposite level
     - Generate Trade event
     - If front order fully filled, remove from level; if level empty, remove level
   - If order has remaining quantity, rest on book

2. **Market order:**
   - Same as limit cross, but no price limit — walk the entire opposite side if needed
   - If book is empty on opposite side, reject

3. **IOC (Immediate-or-Cancel):**
   - Attempt to match like a limit order
   - Cancel any remaining unfilled quantity (do not rest on book)

4. **FOK (Fill-or-Kill):**
   - Before matching: walk the book to check if full fill is possible at acceptable price
   - If yes, execute all matches atomically
   - If no, reject the entire order (no partial fill)

5. **Iceberg:**
   - Only `visible_quantity` is shown on the book
   - When visible portion fills, replenish from `hidden_quantity`
   - From the matcher's perspective: treat visible quantity as the order's quantity, but after fill, reload if hidden remains

### 3.2 Self-Trade Prevention (`src/matching/self_trade_prevention.h`)

Before each potential match, check if `incoming.participant_id == resting.participant_id`. Strategies:
- **Cancel Newest (CN):** Cancel the incoming order
- **Cancel Oldest (CO):** Cancel the resting order
- **Cancel Both (CB):** Cancel both

Implement CN as default, make configurable.

### 3.3 Event Generation

Every state transition produces an event (these flow to the gateway/publisher):
- `OrderAccepted` — order entered the book
- `OrderRejected` — validation failure or FOK cannot fill
- `OrderCancelled` — cancel request processed
- `Trade` — match occurred (contains both sides)
- `OrderFilled` — order fully filled (after last trade)
- `OrderPartialFill` — partial fill (order remains on book)

Events are POD structs written to the SPSC ring buffer.

### 3.4 Tests for Phase 3

- `test_matching_engine.cpp`:
  - Limit order: no cross, partial cross, full cross, multi-level cross
  - Market order: full fill, partial fill (thin book), empty book rejection
  - IOC: partial fill with cancel remainder
  - FOK: full fill possible (execute), full fill not possible (reject)
  - Iceberg: visible fills, replenishment, interaction with other order types
  - Self-trade prevention: same participant, all three modes
  - Price-time priority: two orders at same price, first one matches first
  - Event generation: verify correct sequence of events for each scenario

**This is the most critical test file in the project.** Every matching edge case must be covered.

### 3.5 Phase 3 Deliverables Checklist

- [x] All order types match correctly (limit, market, IOC, FOK, GTC, iceberg)
- [x] Price-time FIFO priority verified with tests
- [x] Self-trade prevention works for all modes (CancelNewest, CancelOldest, CancelBoth)
- [x] Trade generation with correct fields, monotonic IDs, passive price improvement
- [x] Matching benchmark: `bench_matching.cpp` — single-level ~1.2μs, multi-level ~2.3μs (includes setup; WSL)
- [x] All hot-path code: no heap alloc, no exceptions, no virtual dispatch
- [x] 45 new tests (115 total), all passing

---

## Phase 4: Transport Layer

**Goal:** Lock-free SPSC ring buffer for zero-copy message passing between threads.

### 4.1 SPSC Ring Buffer (`src/transport/spsc_ring_buffer.h`)

Header-only, templated:

```
template <typename T, size_t Capacity>
class SPSCRingBuffer {
    // Capacity must be power of 2 (for bitwise modulo)
    static_assert((Capacity & (Capacity - 1)) == 0);

    alignas(64) std::atomic<size_t> head_;  // Written by producer
    char pad1_[64 - sizeof(std::atomic<size_t>)];
    alignas(64) std::atomic<size_t> tail_;  // Written by consumer
    char pad2_[64 - sizeof(std::atomic<size_t>)];
    alignas(64) T buffer_[Capacity];

public:
    bool try_push(const T& item);   // Returns false if full
    bool try_pop(T& item);          // Returns false if empty
};
```

Key requirements:
- `head_` and `tail_` on separate cache lines (prevent false sharing)
- `std::memory_order_acquire` / `std::memory_order_release` — no `seq_cst`
- `T` must be trivially copyable (enforced with `static_assert`)
- No locks, no mutexes, no system calls

### 4.2 Message Types (`src/transport/message.h`)

Fixed-size binary messages, all POD:

```
struct alignas(64) OrderMessage {
    MessageType type;  // Add, Cancel, Modify
    Order order;
    // pad to cache line boundary
};

struct alignas(64) EventMessage {
    EventType type;    // Trade, OrderAccepted, OrderCancelled, ...
    // Union or variant of event data
    // pad to cache line boundary
};
```

### 4.3 Tests for Phase 4

- `test_spsc_ring_buffer.cpp`:
  - Single-threaded push/pop correctness
  - Full buffer behavior (try_push returns false)
  - Empty buffer behavior (try_pop returns false)
  - Multi-threaded stress test: producer fills, consumer drains, verify no data loss or corruption
  - Verify cache-line padding with `offsetof` checks

### 4.4 Phase 4 Deliverables Checklist

- [x] SPSC ring buffer: lock-free, cache-line padded, power-of-2 capacity, acquire/release ordering
- [x] Message types are POD, cache-line aligned — OrderMessage (128B), EventMessage (64B)
- [x] Multi-threaded stress tests pass (1M uint64, 500K EventMessage, 200K OrderMessage)
- [x] Benchmark: push+pop 2.3ns (uint64), ~27ns (64/128B messages); throughput ~64M msg/s (uint64), ~13M msg/s (EventMessage)
- [x] 23 new tests (138 total), all passing

---

## Phase 5: Gateway & Market Data Publisher

**Goal:** Wire everything together. Orders flow in through the gateway, through the matching engine, and events flow out through the publisher.

### 5.1 OrderGateway (`src/gateway/order_gateway.h`)

- Receives order messages (from ring buffer or direct call for replay mode)
- Basic validation: price > 0, quantity > 0, valid side/type, etc.
- Stamps arrival timestamp
- Forwards to matching engine
- In replay mode: reads from L3 feed parser and injects directly

### 5.2 MarketDataPublisher (`src/gateway/market_data_publisher.h`)

- Consumes events from the matching engine (via ring buffer)
- Publishes to analytics module (cold path)
- In replay mode: writes to a log or feeds directly to analytics

### 5.3 Threading Model

```
Thread 1 (hot): Gateway → MatchingEngine → [SPSC buffer] →
Thread 2 (cold): → MarketDataPublisher → Analytics
```

Only one SPSC buffer crosses the thread boundary. The matching engine is single-threaded (this is intentional and correct — real exchanges do this).

### 5.4 Phase 5 Deliverables Checklist

- [x] OrderGateway validates and forwards orders (price/qty/pool-exhaustion checks, GatewayResult return)
- [x] MarketDataPublisher consumes events from ring buffer (poll/run/stop, callback dispatch)
- [x] EventBuffer type alias wires SPSC transport to gateway (`SPSCRingBuffer<EventMessage, 65536>`)
- [x] MatchResult decomposed into EventMessages (trades first, then terminal status)
- [x] Nullable event buffer for testing without publisher (no crash)
- [x] Backpressure handling: spin-wait on try_push failure with diagnostic counter
- [x] Monotonic sequence numbers across all published events
- [x] End-to-end multi-threaded test: 200 orders, background publisher, all events received in order
- [x] MSVC cross-platform support: _aligned_malloc, /wd5051, conditional compiler flags
- [x] 27 new tests (165 total), all passing

---

## Phase 6: L3 Data Replay

**Goal:** Parse L3 (order-by-order) CSV data, replay through the matching engine, validate book state, produce statistics.

### 6.1 Data Format

Custom L3 CSV format (Binance doesn't provide true order-by-order data):

```
timestamp,event_type,order_id,side,price,quantity
1704067200000000000,ADD,1,BUY,42150.50,10
1704067200000100000,ADD,2,SELL,42155.25,5
1704067200000200000,CANCEL,1,,,,
1704067200000300000,TRADE,,BUY,42155.25,5
```

- `timestamp`: uint64_t nanoseconds since epoch
- `event_type`: ADD, CANCEL, TRADE (case-insensitive)
- `price`: decimal string parsed via integer arithmetic (no stod) to fixed-point int64_t
- TRADE records are informational only; the matching engine generates its own trades

### 6.2 L3FeedParser (`src/feed/l3_feed_parser.h`)

- Streaming CSV parser with `open()` / `next()` / `reset()` / `close()` iterator API
- Static parsing utilities: `parse_price()` (integer-only), `parse_quantity()`, `parse_event_type()`, `parse_side()`
- `to_order_message()`: converts L3Record to OrderMessage for gateway injection
- Header line detection and automatic skip
- Parse error tracking with line-level error messages

### 6.3 ReplayEngine (`src/feed/replay_engine.h`)

- Orchestrator that owns the full pipeline: OrderBook, MemoryPool, MatchingEngine, OrderGateway, EventBuffer, MarketDataPublisher
- Reads CSV via L3FeedParser, feeds ADD→process_order, CANCEL→process_cancel
- Collects ReplayStats: message counts, order/trade/cancel stats, final book state, throughput
- Optional JSON report output via nlohmann/json
- Optional event callbacks for downstream consumers (analytics)

### 6.4 Synthetic Sample Data

`data/btcusdt_l3_sample.csv` — 5,000 deterministic messages (seeded RNG):
- Phase 1 (1-500): Build initial book depth around $42,000
- Phase 2 (501-2500): Normal trading (60% ADD, 30% CANCEL, 10% TRADE)
- Phase 3 (2501-3500): Volatile period — price sweeps to $42,100
- Phase 4 (3501-5000): Recovery and steady state around $42,050

### 6.5 CLI Executable

```bash
./build/replay --input data/btcusdt_l3_sample.csv [--output report.json] [--speed max|realtime|2x] [--verbose]
```

Hand-rolled argv parsing (no external deps). Prints summary and optionally writes JSON report.

### 6.6 Phase 6 Deliverables Checklist

- [x] L3FeedParser with integer-only price parsing, case-insensitive event/side parsing, error tracking
- [x] ReplayEngine orchestration: pipeline ownership, stats collection, JSON report output
- [x] `replay` executable with CLI argument parsing and summary output
- [x] Synthetic `data/btcusdt_l3_sample.csv` (5K messages, deterministic final state)
- [x] Replay results: 5K msgs, 1038 trades generated, 1197 orders resting, $42,051/$42,052.50 BBA, 361K msgs/s
- [x] 67 new tests (232 total), all passing — parser unit tests, replay integration, end-to-end sample replay

---

## Phase 7: Market Microstructure Analytics

**Goal:** Compute quantitative analytics from the event stream. This is the cold path — readability and correctness over raw speed.

### 7.1 Analytics Module (`src/analytics/`)

All analytics consume the event stream from the MarketDataPublisher. Each computes its metric and stores time series data.

| Metric | File | Formula / Description |
|---|---|---|
| Bid-Ask Spread | `spread.cpp` | `best_ask - best_bid`, also as bps of mid |
| Effective Spread | `spread.cpp` | `2 * |trade_price - mid|` at time of trade |
| Microprice | `microprice.cpp` | `(bid_qty * ask_px + ask_qty * bid_px) / (bid_qty + ask_qty)` |
| Order Flow Imbalance | `imbalance.cpp` | `(buy_vol - sell_vol) / (buy_vol + sell_vol)` over window |
| Realized Volatility | `realized_vol.cpp` | `sqrt(sum(log_returns^2))` over window, tick-level and time-bar |
| Price Impact | `price_impact.cpp` | Temporary vs permanent impact, regress ΔP on signed flow |
| Kyle's Lambda | `price_impact.cpp` | Slope coefficient from Kyle regression |
| Depth Profile | `depth_profile.cpp` | Cumulative quantity at each level, shape analysis |

### 7.2 Output Format

Two outputs:
1. **JSON summary** (`analytics/report.json`): aggregate statistics, key metrics
2. **CSV time series** (`analytics/timeseries.csv`): per-event or per-interval data for external plotting (Python, R, or any tool)

Use `nlohmann/json` for JSON, raw `ofstream` for CSV.

### 7.2.1 Actual Implementation Architecture

**OrderBook depth API extension** — added `DepthEntry` struct and `get_bid_depth()`/`get_ask_depth()` public const methods to walk flat price arrays from BBO without exposing private internals.

**AnalyticsEngine orchestrator** (`analytics_engine.h/.cpp`):
- Single callback registered with ReplayEngine via `register_event_callback()`
- Infers aggressor side via Lee-Ready tick test (`trade.price >= prev_mid` => buyer-initiated)
- Dispatches every event to all 6 modules
- Captures `TimeSeriesRow` on each Trade for CSV output
- `to_json()` aggregates all module JSON, `write_csv()` writes one row per trade

**Module files** (actual names differ slightly from plan):
| Module | Files | Key detail |
|---|---|---|
| SpreadAnalytics | `spread_analytics.h/.cpp` | Pre-trade mid caching via `prev_mid_`; running min/max/mean stats |
| MicropriceCalculator | `microprice_calculator.h/.cpp` | All arithmetic in `double`; invalid when either side empty |
| OrderFlowImbalance | `order_flow_imbalance.h/.cpp` | `std::deque` rolling window with running buy/sell sums |
| RealizedVolatility | `realized_volatility.h/.cpp` | Tick-level (`log(p/p_prev)`) and time-bar (mid at bar boundary) |
| PriceImpact | `price_impact.h/.cpp` | OLS via Cov/Var running sums; permanent impact via 5-trade lag |
| DepthProfile | `depth_profile.h/.cpp` | Uses `get_bid_depth()`/`get_ask_depth()`; incremental average depth |

**CLI integration** — `replay_main.cpp` accepts `--analytics`, `--analytics-json <path>`, `--analytics-csv <path>`.

### 7.3 Phase 7 Deliverables Checklist

- [x] All 6 analytics implemented: spread, microprice, order flow imbalance, realized volatility, price impact (Kyle's Lambda), depth profile
- [x] JSON summary report generated after replay (`--analytics-json <path>`)
- [x] CSV time series generated for plotting (`--analytics-csv <path>`, one row per trade with 11 columns)
- [x] Unit tests with known inputs/outputs for each metric (45 new tests, 277 total)
- [x] Analytics consume events via MarketDataPublisher callback, fed via SPSC buffer (no hot-path impact)
- [x] OrderBook extended with `get_bid_depth()`/`get_ask_depth()` depth-walking API
- [x] Lee-Ready tick test for aggressor side inference
- [x] OLS regression for Kyle's Lambda implemented from scratch (Cov/Var formula, no Eigen)
- [x] Edge cases tested: empty book, single-sided book, zero-quantity trades, window eviction, large windows

---

## Phase 8: Benchmarking & Profiling

**Goal:** Rigorous performance measurement that proves the system meets targets. This is what quant firms will scrutinize.

### 8.1 Latency Benchmarks

Using Google Benchmark + custom `rdtsc` timing:

| Benchmark | Target p50 | Target p99 |
|---|---|---|
| `BM_AddOrder_NoMatch` | < 100 ns | < 500 ns |
| `BM_CancelOrder` | < 50 ns | < 200 ns |
| `BM_MatchOrder_SingleLevel` | < 200 ns | < 1 µs |
| `BM_MatchOrder_MultiLevel` | < 500 ns | < 2 µs |
| `BM_SPSCPushPop` | < 20 ns | < 50 ns |
| `BM_Throughput_Sustained` | > 5M msg/s | — |

Each benchmark should:
- Run with enough iterations for stable percentiles (minimum 100K iterations)
- Report p50, p90, p99, p99.9, max
- Pre-warm caches and memory pools before measurement
- Pin to a single core (`taskset -c 0`) to avoid migration noise

### 8.2 Cache Analysis

```bash
# scripts/perf_cache_analysis.sh
perf stat -e cache-references,cache-misses,L1-dcache-load-misses,LLC-load-misses \
    taskset -c 0 ./build/bench_orderbook --benchmark_filter=BM_AddOrder
```

Document results and explain data structure layout decisions that minimize cache misses.

### 8.3 Assembly Inspection

For the most critical functions (add_order hot loop, match loop):
```bash
objdump -d -S build/src/orderbook/libhft_orderbook.a | less
```
Or use Compiler Explorer (godbolt.org) for key snippets. Verify no unexpected function calls, allocations, or exception handling code in the hot path.

### 8.4 Phase 8 Deliverables Checklist

- [x] All benchmarks run with analysis: SPSC + throughput pass targets; add/cancel/match within 2-8x of aspirational Linux/GCC targets due to MSVC+Windows overhead (documented in README analysis section)
- [x] Latency histogram in README with p50/p90/p99/p99.9/max — 1M samples per operation, rdtsc overhead subtracted
- [x] Cache analysis: deferred to VTune/assembly inspection (Windows — `perf stat` Linux-only); data structure cache rationale documented
- [x] Benchmark run script: `scripts/run_benchmarks.ps1` (PowerShell, Windows-native; documents HW env, runs all 4 exes, saves timestamped results)
- [x] Results reproducible: CPU model, OS version, MSVC version, build flags documented in README
- [x] Custom `bench_latency` harness with rdtsc_start/rdtsc_end, LFENCE+RDTSCP serialization, overhead calibration
- [x] `src/utils/clock.h` and `src/utils/latency_histogram.h` — timing and stats utilities
- [x] 13 new tests (290 total), all passing

---

## Phase 9: Polish & Documentation

**Goal:** Portfolio-ready. Someone cloning the repo should be able to build, run, and understand everything in under 10 minutes.

### 9.1 Documentation

- [x] README.md: Updated with actual benchmark results (not just targets)
- [x] CLAUDE.md: Updated with architectural changes and warning policy
- [x] Code comments: Every public API has a doc comment. Non-obvious optimizations have inline explanations.
- [x] Architecture diagram: Mermaid diagram showing threading model and data flow (GitHub-rendered)

### 9.2 Code Quality

- [x] `.clang-format` config present, codebase consistent
- [ ] `clang-tidy` passes with no warnings — requires Linux/Clang toolchain (not available on MSVC)
- [x] No compiler warnings: `/W4 /WX` on MSVC (both hot and cold paths), `-Wall -Wextra -Wpedantic -Werror` on GCC/Clang
- [x] All 290 tests pass under MSVC AddressSanitizer (`-DHFT_ENABLE_ASAN=ON`)
- [ ] UndefinedBehaviorSanitizer — requires GCC/Clang (`-fsanitize=undefined`); GCC/Clang Debug build already configured
- [ ] ThreadSanitizer — requires Linux/GCC/Clang (`-fsanitize=thread`); not available on MSVC

### 9.3 Stretch Goals (if time permits)

These are NOT required for the core project but would differentiate further:

- [x] FIX protocol message parsing (even basic) — shows awareness of real exchange protocols; FIX 4.2 parser (35=D/F/G), serializer (35=8 Execution Report), ~20 tags, checksum validation, 30 sample messages, 55+ tests
- [ ] Multiple instruments — generalize from single to multi-instrument order book
- [x] Order modify (amend price/quantity) — cancel-and-replace semantics, zero-alloc (reuses pool slot), crossing triggers matching, 32 new tests (322 total)
- [ ] Python bindings (pybind11) for the analytics — useful for quant researchers
- [ ] Grafana dashboard fed by analytics output — visual wow factor

---

## Development Order Summary

| Phase | What | Depends On | Estimated Effort |
|---|---|---|---|
| 0 | Environment + scaffold | Nothing | 1 day |
| 1 | Core types + memory pool | Phase 0 | 2–3 days |
| 2 | Order book | Phase 1 | 3–4 days |
| 3 | Matching engine | Phase 2 | 4–5 days |
| 4 | SPSC ring buffer | Phase 1 (types only) | 2–3 days |
| 5 | Gateway + publisher | Phases 3, 4 | 2–3 days |
| 6 | L3 data replay | Phase 5 | 2–3 days |
| 7 | Analytics | Phase 6 | 3–4 days |
| 8 | Benchmarking + profiling | Phase 5 | 2–3 days |
| 9 | Polish + docs | All | 2–3 days |

**Total estimated: ~25–30 working days.**

Phases 4 and 8 can be developed in parallel with Phases 2–3. The critical path is Phases 1 → 2 → 3 → 5 → 6.

---

## Claude Code Usage Notes

When working with Claude Code on this project:

1. **Always reference CLAUDE.md** for style conventions and architectural constraints before writing code.
2. **Hot path code reviews**: After generating any code in `core/`, `orderbook/`, `matching/`, or `transport/`, verify:
   - No `std::string`, `std::map`, `std::unordered_map`, `std::shared_ptr`
   - No `new`/`delete` (only memory pool)
   - No `virtual` functions
   - No exception handling (`try`/`catch`/`throw`)
   - All structs have `static_assert` for trivially copyable and size
3. **Test-first for matching**: Write the test cases in Phase 3 before implementing the matching logic. The test file defines the contract.
4. **Benchmark after each phase**: Don't wait until Phase 8. Run quick benchmarks as you go to catch performance regressions.
5. **Commit per phase**: Each phase should be a clean, buildable, tested commit (or small series of commits). This shows clean development practice in the git history.
