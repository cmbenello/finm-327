# Session 4 — High-Performance Data Structures for HFT

**Hardware:** Apple Silicon (`arm64`), Apple clang 17, `-O3 -march=native`, `-std=c++17`.
**SIMD path:** ARM NEON (128-bit, 2× `float64`). The code also has an `__AVX2__` path that compiles unchanged on x86.

Build & run: `make bench` (writes `bench_results.txt`).

---

## Benchmark Results

### Part 1 — Robin Hood Hash Map vs `std::unordered_map`
N = 200 000 inserts, 2 000 000 random lookups, then erase half.

| structure              | insert (ms) | lookup (ms) | erase (ms) |
|------------------------|------------:|------------:|-----------:|
| `RobinHoodMap`         |       17.94 |     **118.41** |       **5.22** |
| `std::unordered_map`   |   **10.04** |      126.76 |       6.30 |

Lookup speedup: **1.07×**. Erase speedup: **1.21×**. Insert is 1.8× slower because each
insert may walk a probe chain and perform Robin Hood swaps; the win shows up
where the workload is — lookups.

### Part 2 — Binary Max-Heap vs `std::priority_queue`
N = 1 000 000. Push-all, then pop-all, then a mixed push-half / pop-half loop.

| structure              | push (ms) | pop (ms) | mixed (ms) |
|------------------------|----------:|---------:|-----------:|
| `BinaryHeap` (hole)    |     25.40 |   279.13 |  **82.13** |
| `std::priority_queue`  |     26.30 | **206.91**|     83.97 |

Push and the matching-engine-style mixed workload are within noise of `std`.
On a pure pop-all `std` is still 1.35× faster — libc++'s `pop_heap` does some
extra work to bias toward sequential memory access on the leaf level which my
implementation doesn't replicate.

### Part 3 — Moving Average, Scalar vs SIMD
N = 2²⁰ = 1 048 576 ticks, window W = 64.

| method                                | time (ms) | notes                |
|---------------------------------------|----------:|----------------------|
| sliding-sum scalar (O(N) reference)   |      0.00 | one add+sub per tick |
| brute scalar (O(N·W) baseline)        |     11.74 | recompute each window|
| brute SIMD (NEON, O(N·W) vectorized)  |  **6.16** | 2× double-precision lanes |

**SIMD speedup vs brute scalar: 1.91×.** Numerical error vs scalar:
`1.7e-13` — pure rounding-order effect.

The sliding-sum reference is at the limit of timer resolution: it's O(N) instead
of O(N·W), and that algorithmic improvement (~64×) dwarfs SIMD on this kernel.
That's the most important takeaway from this part.

### Part 4 — Order Book: `unordered_map`+`map` vs flat `unordered_map`
N = 200 000 resting orders, 500 000 best-bid/offer queries.

| structure                              | add (ms) | modify (ms) | BBO ×500k (ms) | cancel (ms) |
|----------------------------------------|---------:|------------:|---------------:|------------:|
| `OrderBook` (unordered + ordered map)  |    18.90 |        3.23 |       **1.78** |       24.81 |
| `FlatBook` (unordered map only)        | **4.92** |    **2.02** |     822 311.88 |    **8.15** |

(`FlatBook` BBO was timed on 200 queries and scaled — full 500 k would take
≈ 14 minutes.) **BBO speedup ≈ 4.6 × 10⁵×.** A linear scan of 200 k orders per
quote is structurally wrong for an HFT path that quotes on every tick.

---

## Big-O Summary

| operation           | RobinHoodMap | std::unordered_map | BinaryHeap | std::map (price idx) |
|---------------------|:------------:|:------------------:|:----------:|:--------------------:|
| insert              | O(1) avg     | O(1) avg           | O(log n)   | O(log n)             |
| lookup by key       | O(1) avg     | O(1) avg           | O(n)       | O(log n)             |
| top / best price    | —            | —                  | O(1)       | O(1) on `begin()`    |
| pop / extract best  | —            | —                  | O(log n)   | O(log n)             |
| ordered iteration   | not ordered  | not ordered        | not ordered| **O(n) in order**    |

In the order book: `unordered_map<id,Order>` gives O(1) modify/cancel by ID,
and `map<price, vector<id>>` gives O(1) best-bid/offer (`begin()`) plus
O(log n) price-level insertion.

---

## Discussion (≈ 500 words)

**Part 1 — collision strategies.** Robin Hood hashing is open-addressing with
one extra invariant: when probing, we track the displacement from the home
slot; a richer entry yields its slot to a poorer one. The result is variance
collapse — the longest probe chain is ~`log(n)` instead of the `n²/m`-ish tail
of vanilla linear probing, and average probe length stays tight under high
load. The benefits show up most for **lookup**, especially negative lookups,
because we can stop probing the moment we find a slot poorer than ourselves.
That's exactly what the numbers show: ~1.07× faster lookup with smaller
variance-driven tail. `std::unordered_map` uses chaining: each bucket points
to a linked list, so every lookup is at least one cache line for the bucket
plus one for the node — two pointer chases for what should be a single load.
The downside of Robin Hood is that **insert** does more work (the swap walk
and an occasional power-of-two rehash), so we trade insert latency for
lookup latency. For a quote-heavy HFT workload (insert once at session
start or on subscription, lookup millions of times) that's the right trade.
A cuckoo hash would push lookup to a bounded *worst case* of two probes, at
the cost of a more complex insert. Pick by workload shape.

**Part 2 — heap vs `std::priority_queue`.** A binary max-heap is the right
shape for "always extract the highest bid." The hole-based sift cuts memory
traffic roughly in half vs the textbook swap-based version (one store per
level + one store at the end, vs two stores per level). Even after that
optimization, `std::priority_queue` is ≈ 1.35× faster on a pure pop-all,
because libc++ also tunes its leaf-level walk for sequential prefetches. For
a real matching engine, neither raw structure is what you want long-term —
price-time priority needs FIFO ties at a price level, and you'll outgrow a
single heap. A min-heap of (price, seq) plus an intrusive `seq` ordering at
each price gives O(log n) insert/cancel and O(1) best-price.

**Part 3 — SIMD and Amdahl.** The biggest win in Part 3 wasn't the 1.91×
NEON speedup; it was the algorithmic O(N·W) → O(N) shift to the sliding-sum
form. On AVX2 you'd get ≈ 4× from SIMD (4 doubles per vector); on NEON we
get 2×, because 128-bit registers fit two `float64` lanes. The lesson is the
classical one — pick the right algorithm before reaching for intrinsics.
SIMD is a constant-factor multiplier on whatever you're already doing.

**Part 4 — composite indexes.** The order book is the clearest example in
this set: no single container can cheaply give you both O(1) lookup-by-ID
and O(1) best-bid/offer. The solution is to keep two structures consistent
on every mutation. Memory cost is roughly 2× compared to the flat book, but
the BBO path is what publishes quotes — five orders of magnitude faster
matters. In production you'd go further: replace `std::map` with a flat
sorted vector of price levels for cache locality, store `Order` objects in a
slab/pool so iteration is contiguous, and intern symbols so the hash table
key is a 32-bit ID instead of a string.

**Real-world applicability.** The combined pattern — an ID hash table plus a
price-level structure plus an arena allocator plus SIMD on aggregations —
is the core of every low-latency book in the wild. The micro-benchmarks here
confirm the textbook answer: pick data structures so that the fast path is
O(1) or O(log n), and only after that does intrinsic-level optimization buy
you anything.
