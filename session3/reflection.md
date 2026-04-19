# Session 3 Reflection

## 1. Three biggest risks of manual memory management

- **Leaks.** Forgetting a `delete`, or losing the only pointer to an allocation
  (reassignment, early return, exception), leaks memory permanently. Small leaks
  are invisible in tests and only surface after hours of uptime.
- **Use-after-free / dangling pointers.** Freeing memory while another part of
  the program still holds a pointer to it. The next read returns stale data or
  memory that now belongs to something else — silent corruption, not a crash.
- **Double-free and `new`/`delete` mismatches.** Freeing the same pointer twice
  or pairing `new[]` with `delete` corrupts the allocator's free list.
  Symptoms show up arbitrarily far from the actual bug.

## 2. Real-world HFT consequences

- A leak in a hot path (per-tick allocation that's never freed) eats the RSS
  budget over the trading day. The process gets killed by the OOM killer or
  starts swapping — either way, you miss fills.
- Heap fragmentation from mixed allocation sizes degrades cache locality. A
  cold cache on an order path adds hundreds of nanoseconds to tick-to-trade,
  which is the difference between being at the top of the queue and not.
- Use-after-free on an order object could send a stale price or wrong size.
  In HFT that's a real-money bug, not a test failure.
- Non-deterministic allocation pauses (malloc taking a slow path, page faults)
  show up as tail-latency spikes. These are the kinds of outliers a P99
  latency SLO is designed to catch.

## 3. How RAII reduces risk

Ownership is tied to scope, so cleanup runs automatically at the matching `}` —
including on early returns and exceptions. You can't forget to free because
there's no free call to forget. Copy is disabled or reference-counted, so two
owners can't accidentally both try to free. The compiler enforces the lifetime
rule rather than the programmer remembering it.

## 4. Tradeoffs in performance-critical code

- **GC languages** give automatic safety but pause times (even short ones) are
  unacceptable on a hot trading path. Unpredictable is worse than slow.
- **Manual `new`/`delete`** gives control but is fragile and hard to audit.
- **RAII + smart pointers** is the usual answer, but `shared_ptr` has an
  atomic refcount on copy/destroy, which is real cost on a hot loop. HFT code
  typically prefers `unique_ptr` or raw owning pointers behind a narrow
  interface, plus **arena/pool allocators** so the fast path never touches
  `malloc` at all.
- The realistic pattern: RAII at the boundaries (startup, config, teardown)
  where safety matters more than a few nanoseconds; pre-allocated pools and
  plain pointers on the tick-to-trade path where every ns counts.
