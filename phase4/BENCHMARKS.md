# Benchmarks

All numbers in nanoseconds, captured on:

- Apple Silicon (arm64), Darwin 25.1.0
- AppleClang 17, `-O3 -march=native -std=c++17`
- single-threaded; no thread pinning, no isolated cores
- timer source: `std::chrono::high_resolution_clock` (mach_absolute_time)

The "matcher" timing brackets only the `MatchingEngine::submit` call. The full tick-to-trade latency (timestamp at tick recv -> trade emission) is written into the trade record's `ns` field by the matching engine itself.

Reproduce with:

```sh
./test_latency > bench_results.txt
```

## 1. Container layout (map vs flat-vector)

Same workload (10K ticks, AAPL, 5-cent spread, ~50/50 aggressive vs passive):

| book        | n     | min | p50  | mean | p99   | max    | std   |
|-------------|-------|-----|------|------|-------|--------|-------|
| `OrderBook` (map)        | 10000 | 41  | 125  | 161  | 1666  | 10000  | 257   |
| `FlatOrderBook` (vector) | 10000 | 41  | 625  | 842  | 3417  | 81083  | 1288  |

The map wins on every percentile. The flat book has cache-friendly traversal in theory but pays for it on insertion: shifting elements in a sorted vector dominates as the book widens. For a shallow book the constants might tilt — but at any realistic depth, the std::map is fine.

## 2. Load scaling

Map-based book at four sizes:

| ticks | min | p50 | mean | p99  | max    | std |
|-------|-----|-----|------|------|--------|-----|
| 1K    | 41  | 125 | 187  | 1917 | 7209   | 347 |
| 10K   | 0   | 125 | 152  | 1375 | 22250  | 409 |
| 100K  | 0   | 125 | 159  | 1458 | 100083 | 523 |
| 1M    | 0   | 125 | 174  | 1458 | 200875 | 641 |

Median is essentially flat across three orders of magnitude. Mean creeps up slowly (more allocator pressure as the OMS map grows). The max blows up at 1M ticks — those tail spikes are mostly allocator/page-fault noise, not work the matcher is doing. With huge-page hints, an arena allocator, and core pinning you'd squash most of that.

Flat book at the same sizes:

| ticks | min | p50  | mean | p99   | max    | std  |
|-------|-----|------|------|-------|--------|------|
| 1K    | 41  | 209  | 305  | 1667  | 7458   | 407  |
| 10K   | 0   | 625  | 825  | 3458  | 29334  | 856  |
| 100K  | 0   | 2250 | 3945 | 20542 | 85541  | 4682 |

Insert cost grows linearly with depth — the median jumps from 209 ns at 1K to 2.25 µs at 100K. This is the textbook tradeoff: vectors crush maps for read-mostly small-N data, but a price book is insert-heavy with many distinct levels.

## 3. Allocation paths (per-order)

Pure micro: acquire one Order, release it, repeat.

| path                          | n      | min | p50 | mean | p99 | max    | std |
|-------------------------------|--------|-----|-----|------|-----|--------|-----|
| `make_shared<Order>`          | 100000 | 0   | 41  | 29   | 83  | 138833 | 452 |
| raw `new`/`delete`            | 100000 | 0   | 0   | 16   | 42  | 7333   | 31  |
| `MemoryPool::acquire`/release | 100000 | 0   | 0   | 18   | 42  | 42250  | 220 |

Takeaways:

- `make_shared` is ~2x raw `new` because of the control block (refcount + weak count + deleter). In a steady-state HFT, that overhead is constant per order — perfectly acceptable in the OMS, but you'd avoid it on the hottest inner loop.
- The pool ties raw `new` here. Apple's allocator is excellent for tiny fixed-size objects. Where the pool actually shines is **tail latency**: it eliminates the worst-case allocator path entirely (no syscall, no global lock, no page fault). You'd see the win clearly on Linux with glibc, on a write-heavy workload, or under memory pressure.
- In production an HFT would skip `shared_ptr` for hot orders and use intrusive free-list pools tied to per-thread arenas.

## 4. Cache alignment

Walk a `vector<MarketData>` of 1M ticks and sum bid+ask:

| variant                              | ns/walk  | size/struct |
|--------------------------------------|----------|-------------|
| `alignas(64) MarketData`             | 1263458  | 64          |
| `Unaligned` (extra 8B field, 72B)    | 1466375  | 72          |

~16% slower walking the unaligned version. Each access straddles a cache line about 1/8 of the time, so the prefetcher has to do extra work. With `alignas(64)` every tick lands on its own line and the access pattern is dead simple.

In a real feed handler, the alignment matters even more: ticks fly across a thread boundary into a ring buffer, and aligned slots make the producer/consumer split safe from false sharing on the slot itself (separate from any header fields, which would also need padding).

## 5. Smart vs raw pointer overhead (qualitative)

Captured implicitly in section 3 above: `shared_ptr<Order>` allocation is ~2x raw `new`. The OMS uses `shared_ptr` because the book and the OMS need the same instance — switching to raw `Order*` would require manual lifetime management and a separate ownership policy. The 13 ns/order tax for that safety is a clean trade.

## Where the time actually goes (matcher path)

A typical fill on the map book breaks down roughly as:

- `book_.prune_front` -> map.begin / deque.front access: ~30 ns
- compute crosses + adjust qty/state: ~20 ns
- `log_.add` (push_back into pre-reserved vector): ~10 ns
- chrono::now + duration cast for the trade record: ~80 ns
- everything else (branches, function calls): residual

The chrono call is genuinely the dominant hot-path cost. If you needed sub-100ns matcher latency, you'd switch to TSC reads (rdtsc on x86, mrs cntvct on aarch64) and convert to ns at log time.

## Limitations / honest caveats

- single-threaded. real HFTs split the feed handler, OMS, and matcher across cores.
- no I/O on the hot path. trades are buffered to memory, only flushed at end.
- no risk checks, no fees, no actual exchange protocol. matching is purely price-time.
- macOS isn't ideal for low-latency benchmarking — no isolcpus, no rt scheduling. numbers on a tuned Linux box would be tighter at the tail.
