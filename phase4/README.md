# phase4 - HFT prototype in C++

A small but complete HFT pipeline: tick feed -> order management -> limit order book -> matching engine -> trade log, instrumented for tick-to-trade latency.

The point isn't to ship a real exchange. It's to exercise the C++ tools that matter for low latency — templates, smart pointers, RAII, alignment, custom allocators, compile-time checks — and measure what each one buys you.

## Layout

```
phase4/
├── include/
│   ├── MarketData.hpp     - alignas(64) tick + xorshift feed
│   ├── Order.hpp          - templated Order<PriceT, OrderIdT>, static_asserted
│   ├── OrderBook.hpp      - map-based book + flat-vector book
│   ├── MatchingEngine.hpp - templated over book type
│   ├── OrderManager.hpp   - shared_ptr OMS with state + cancel + reap
│   ├── TradeLogger.hpp    - reserve()'d vector + RAII flush
│   ├── MemoryPool.hpp     - fixed-capacity slab allocator
│   └── Timer.hpp          - high_resolution_clock wrapper + ScopedNanos
├── src/
│   ├── MarketData.cpp     - feed simulator
│   └── main.cpp           - end-to-end demo
├── test/
│   └── test_latency.cpp   - experiment harness
├── CMakeLists.txt
├── BENCHMARKS.md          - results + analysis
└── README.md
```

## Architecture

```
        +------------------+
        |  MarketDataFeed  |   xorshift mid-walk, alignas(64) ticks
        +--------+---------+
                 |
                 v
        +------------------+   timestamps tick recv
        |  TickHandler     |   (in main.cpp's loop)
        +--------+---------+
                 |
                 v
        +------------------+   shared_ptr<Order>; state machine
        |  OrderManager    |   New / Partial / Filled / Cancelled
        +--------+---------+
                 |
                 v
        +------------------+   templated on book type
        |  MatchingEngine  |---+
        +--------+---------+   |
                 |             v
                 v       +-----------+
        +------------------+   | TradeLogger |
        | OrderBook<P,Id>  |   +-------------+
        |  - map (fast)    |
        |  - flat (cache)  |
        +------------------+
```

The matching engine is templated over the book type so we can swap `OrderBook` (std::map of price levels) for `FlatOrderBook` (sorted vector of price levels) without touching matcher code.

## Build and run

```sh
cd phase4
mkdir build && cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .

./hft_app           # default 100K ticks
./hft_app 1000000   # 1M ticks
./test_latency      # full experiment suite
```

`hft_app` writes the trade tape to `trades.csv` (one row per fill, with the per-trade tick-to-trade latency in nanoseconds).

## What each module shows off

**Order.hpp**

```cpp
template <typename PriceT, typename OrderIdT>
struct Order {
    static_assert(std::is_integral<OrderIdT>::value, "Order ID must be integral");
    static_assert(std::is_arithmetic<PriceT>::value, "Price must be arithmetic");
    ...
};
```

Compile-time guards mean a typo like `Order<double, std::string>` won't link. The whole pipeline uses `OrderD = Order<double, int64_t>` as the concrete type, but the templates keep the door open for fixed-point integer prices later.

**MarketData.hpp**

```cpp
struct alignas(64) MarketData { ... };
static_assert(alignof(MarketData) == 64, "...");
```

One tick per cache line. Padding hurts memory footprint a bit but kills false sharing if a producer thread ever writes ticks while a consumer reads them.

**MemoryPool.hpp**

Fixed-capacity slab + free list. `acquire()` pops from the free list, `release()` pushes back. No `new`/`delete` on the hot path.

**OrderBook.hpp**

Two implementations:
- `OrderBook` — `std::map<Price, std::deque<OrderPtr>>` (descending for bids, ascending for asks). O(log n) inserts, but n is the number of *distinct price levels*, which stays small in practice.
- `FlatOrderBook` — sorted `std::vector<Level>`. O(n) insert worst case, but cache-friendly traversal.

**OrderManager.hpp**

`unordered_map<int64_t, shared_ptr<Order>>`. The book and the OMS hold the same `shared_ptr` so a fill seen by the matcher is reflected in the OMS without explicit synchronization. `reap()` collects done orders so the map doesn't grow without bound.

**TradeLogger.hpp**

`vector<Trade>`, `reserve()`'d up front. `flush(path)` dumps in one go via an RAII `Dump` handle that flushes + closes the ofstream on destruction.

## Benchmark report

See [BENCHMARKS.md](./BENCHMARKS.md). Short version:

- mean tick-to-trade (matcher-only) on the map-based book: ~150 ns.
- p99 ~1.4 µs, max excursions on the order of 100s of µs (stop-the-world allocator events, OS noise).
- flat-vector book is ~5x slower at 10K and degrades further as depth grows, exactly as expected.
- shared_ptr allocation is ~2x raw new/delete because of the control block; the pool is essentially tied with raw `new` for short-lived churn.

## Notes

The min=0 ns rows in some tables are timer resolution, not magic. macOS `high_resolution_clock` is mach_absolute_time which has ~41ns granularity on this machine — anything below that quantizes to 0.
