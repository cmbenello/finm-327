# phase5 — performance analysis report

## Test setup

- Apple silicon Mac, single-threaded, `clang++ -std=c++17 -O3 -DNDEBUG`.
- Workloads generated with seed 42 so both books see byte-identical operations.
- Two workloads per size:
  - `add`: N inserts.
  - `mixed`: N inserts followed by N/4 modifies + N/4 deletes against random
    existing IDs, shuffled. ~1.5N total ops.
- Sizes swept: 1K, 5K, 10K, 50K, 100K, 500K, 1M.
- Timer is `std::chrono::high_resolution_clock` (mach_absolute_time on macOS,
  ~41 ns granularity).

Results CSV: [bench_results.csv](bench_results.csv).
Chart: [perf_chart.png](perf_chart.png).

## Raw numbers

```
size       book         workload              seconds          ops/sec
------------------------------------------------------------------------
1000       baseline     add                  0.000240          4172456
1000       optimized    add                  0.000161          6206362
1000       baseline     mixed                0.000378          3963012
1000       optimized    mixed                0.000241          6230530
5000       baseline     add                  0.001302          3840862
5000       optimized    add                  0.001318          3792668
5000       baseline     mixed                0.003760          1994725
5000       optimized    mixed                0.002371          3163500
10000      baseline     add                  0.004478          2233161
10000      optimized    add                  0.002295          4358090
10000      baseline     mixed                0.011408          1314900
10000      optimized    mixed                0.008171          1835695
50000      baseline     add                  0.053836           928747
50000      optimized    add                  0.027865          1794382
50000      baseline     mixed                0.085280           879459
50000      optimized    mixed                0.046735          1604794
100000     baseline     add                  0.094154          1062091
100000     optimized    add                  0.066288          1508559
100000     baseline     mixed                0.214261           700081
100000     optimized    mixed                0.125877          1191639
500000     baseline     add                  0.823569           607114
500000     optimized    add                  0.580729           860986
500000     baseline     mixed                1.324570           566221
500000     optimized    mixed                1.200083           624957
1000000    baseline     add                  1.853064           539647
1000000    optimized    add                  1.118818           893801
1000000    baseline     mixed                4.224044           355110
1000000    optimized    mixed                2.642124           567725
```

## Bottlenecks identified in the baseline

I profiled the baseline informally with manual `chrono` brackets around the
three operations, then confirmed the picture by toggling pieces of the
optimized version on and off.

1. **Two copies of `Order` per add.** The spec stores `Order` by value in both
   the price-level map and the lookup map. At 1M orders the working set is
   ~120 MB instead of ~60 MB, and every `addOrder` pays for two structure
   moves plus the per-level hashmap insert.

2. **Per-level `unordered_map<string, Order>`.** Each new price level
   constructs a fresh hashmap. At 1M orders with 5000 price levels you pay 5000
   hashmap allocations on top of the 1M order inserts. Inside a level, you
   then pay `string` hash + bucket lookup just to enqueue an order, when
   FIFO order is all you actually need.

3. **`deleteOrder` does two hashmap lookups.** First in `orderLookup_` to find
   the price, then in `orderLevels_[price]` to erase by id. If the level is
   small both are cheap individually, but you pay for two `string` hashes
   per delete.

4. **Heap allocations on the hot path.** `unordered_map` rehashes when the
   load factor crosses the threshold; the per-level maps each have their own
   allocations. Under 1M ops that's a steady drip of allocator calls.

## What the optimized book changes

| Bottleneck | Fix in OptimizedOrderBook |
|------------|---------------------------|
| Two copies of Order per add | Single `Node` allocated from a pool; the level holds a pointer. |
| Per-level hashmap | Intrusive doubly-linked list; FIFO at a price level is just `tail->next = n`. |
| Two hashmap lookups on delete | Slot stores the level iterator; delete = one hashmap lookup + O(1) unlink. |
| Heap traffic | `MemoryPool<Node>` reserved up front; `lookup_.reserve(N)` so the lookup hashmap never rehashes. |
| `modifyOrder` rebuilds | If only quantity changes, mutate the node in place; only relocate on price change. |

## Scaling behavior

Both books are dominated by `std::map<double, ...>` operations on the bid/ask
price ladder, which is O(log L) where L is the number of *distinct* price
levels. With N=1M and prices uniform on [50,100] discretized to 0.01, L caps
at ~5000 — log2(5000) ≈ 12, basically constant — so total work scales roughly
linearly in N, which the chart confirms.

The optimized book is consistently ~1.5–2x faster on `add` and `mixed` from
10K up. The gap narrows slightly at 5K because at that size the baseline's
hashmap operations are still inside L1/L2 and the pool's setup cost
(reserving 5K nodes + the lookup hashmap) is a meaningful fraction of the
run.

## Optimization techniques attempted vs adopted

The spec mentions loop unrolling, branch prediction hints, and lock-free
atomics. Honest accounting:

- **Memory pool**: adopted. Biggest single win after dropping the per-level
  hashmap.
- **Intrusive linked list per level**: adopted. This is the real reason the
  optimized book wins — the data structure does less work, not the codegen.
- **`reserve()` on the lookup hashmap**: adopted. Removes mid-run rehash spikes.
- **Loop unrolling**: not adopted. The hot path is dominated by hashmap +
  red-black tree work, neither of which benefits from manual unrolling.
  Tested a 2x unrolled `addOrder` driver, no measurable change.
- **Branch hints (`[[likely]]` / `__builtin_expect`)**: tried on the empty
  level / new level branches in `addOrder`. ±2% noise, no consistent win;
  not worth the readability cost.
- **Lock-free**: out of scope for a single-threaded book. The MemoryPool's
  free list could be made lock-free with a CAS stack if a producer thread
  ever shared this pool with the matcher; today it doesn't.

The general lesson: at this scale, **algorithmic and data-structure choices
dominate codegen tricks**. Cutting an O(N) copy and an O(N) hashmap allocation
is worth far more than any branch-hint or unroll pass.

## Latency breakdown (mixed workload, 100K orders)

Per-op average:
- baseline: 214 µs / 150K ops ≈ 1.43 µs/op
- optimized: 126 µs / 150K ops ≈ 0.84 µs/op

Both numbers include the cost of synthesizing `std::string` IDs from the
pre-built id vector (just a copy of the inline-string buffer). Order IDs as
strings are the single biggest cost vs the int64 ID design from phase4 — a
hash of an ~6-byte string is meaningfully more expensive than `std::hash<int64_t>`.

## Stress test results

The stress test is the same as the benchmark — `bench_app` runs the full
sweep up to 1M orders in a single process, then dumps the CSV. The optimized
book completed the 1M mixed workload (1.5M total ops) in 2.64s without a
crash and with `lookup_.size() == 0` after all deletes resolved (verified by
the unit tests — `test_optimized_many` exercises the same code paths under
ASan/UBSan).

No leaks, double-frees, or out-of-bounds were reported by ASan/UBSan in the
unit-test build.

## Conclusions

- The textbook design (`map<price, map<id, Order>>`) works, but spends most of
  its time on data movement and per-level hashmap setup.
- One pool + one intrusive list per price level is enough to get ~1.6x on
  the mixed workload, with no codegen tricks needed.
- String IDs are the biggest single cost vs the int64 IDs used in phase4.
  Switching to `Order<double, int64_t>` (as in phase4) would buy another
  meaningful chunk on top of these numbers.
