# phase5 - HFT order book with string-based order IDs

Phase 5 of the FINM 327 class project. Builds an order book the way the spec
asks for it (`std::map<double, std::unordered_map<std::string, Order>>` plus a
flat `unordered_map<string, Order>` lookup), then ships an optimized variant
that uses a memory pool, intrusive linked lists per price level, and O(1)
delete via cached level iterators. Both books have identical APIs and are
benchmarked head-to-head under the same workload.

## Layout

```
phase5/
├── include/
│   ├── Order.hpp                - { id (string), price, qty, isBuy }
│   ├── OrderBook.hpp            - baseline: spec-literal map<price,map<id,Order>>
│   ├── OptimizedOrderBook.hpp   - pooled nodes + intrusive list per level
│   ├── MemoryPool.hpp           - fixed-capacity slab allocator
│   └── Timer.hpp                - chrono wrapper
├── src/
│   ├── main.cpp                 - small demo
│   └── benchmark.cpp            - stress + bench harness, writes CSV
├── test/
│   └── test_orderbook.cpp       - unit tests (asan/ubsan build)
├── plot_results.py              - chart generator
├── Makefile
├── PERFORMANCE.md               - benchmark report
├── bench_results.csv            - latest run
└── perf_chart.png               - latest chart
```

## Build & run

```sh
cd phase5
make all            # builds bench_app, demo_app, test_app
make run-tests      # asan+ubsan unit tests
make run-bench      # writes bench_results.csv
make chart          # writes perf_chart.png
```

Requires a C++17 compiler and `python3 -m pip install matplotlib` for the chart.

## What the two books do differently

**OrderBook (baseline)** — straight from the spec.
- `std::map<double, std::unordered_map<std::string, Order>>` for price levels.
- `std::unordered_map<std::string, Order>` for direct lookup.
- Order is stored *by value* in both maps. `addOrder` does two copies.
- `deleteOrder` does two hashmap lookups (lookup map, then the per-level map),
  plus erase from both.

**OptimizedOrderBook** — same public API, faster guts.
- Pool-allocated `Node { Order, prev*, next* }`. No `new`/`delete` after the
  initial reserve.
- Each price level is an intrusive doubly-linked list of `Node*`. No per-level
  hashmap, no second copy of Order.
- Lookup is `unordered_map<string, Slot>` where `Slot` carries the iterator
  into the level map. Delete unlinks the node and erases the level entry in
  O(1) — no rescan of the level.
- `modifyOrder` skips relocation when only quantity changes.

## Spec → file map

| Step (spec)                          | Where it lives                        |
|--------------------------------------|---------------------------------------|
| 1.1 core data structures             | [include/OrderBook.hpp](include/OrderBook.hpp) |
| 1.2 add/modify/delete                | [include/OrderBook.hpp](include/OrderBook.hpp) |
| 2.1 chrono profiling                 | [include/Timer.hpp](include/Timer.hpp) + [src/benchmark.cpp](src/benchmark.cpp) |
| 2.2 bottleneck identification        | [PERFORMANCE.md](PERFORMANCE.md) |
| 3.1 memory pool                      | [include/MemoryPool.hpp](include/MemoryPool.hpp) |
| 3.2 loop unrolling / scalar tuning   | not used — the optimized book wins on data structures, not micro-tricks (see PERFORMANCE.md) |
| 3.3 lock-free                        | atomic counter sketch in [include/MemoryPool.hpp](include/MemoryPool.hpp); single-threaded book by design |
| 4.1 unit tests                       | [test/test_orderbook.cpp](test/test_orderbook.cpp) |
| 4.2 stress tests                     | [src/benchmark.cpp](src/benchmark.cpp) |
| 5   matplotlib chart                 | [plot_results.py](plot_results.py) → [perf_chart.png](perf_chart.png) |

## Headline numbers

On an Apple-silicon Mac, release build (`-O3 -DNDEBUG`):

| size      | mixed workload (baseline) | mixed workload (optimized) | speedup |
|-----------|---------------------------|----------------------------|---------|
|     1,000 | 0.000378 s                | 0.000241 s                 | 1.57x   |
|    10,000 | 0.011408 s                | 0.008171 s                 | 1.40x   |
|   100,000 | 0.214261 s                | 0.125877 s                 | 1.70x   |
| 1,000,000 | 4.224044 s                | 2.642124 s                 | 1.60x   |

Full breakdown in [PERFORMANCE.md](PERFORMANCE.md), CSV in
[bench_results.csv](bench_results.csv), chart in [perf_chart.png](perf_chart.png).
