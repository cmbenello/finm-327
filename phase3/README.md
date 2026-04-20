# Phase 3 — Local Order Book and Core Trading Infrastructure

A small C++17 trading core that maintains a local view of the market, tracks
its own orders, and reacts to a simulated feed. Built with smart pointers and
RAII so there is no manual `new`/`delete` anywhere in the code.

## Architecture

```
            +------------------+
            |   feed_parser    |   reads sample_feed.txt -> FeedEvent[]
            +--------+---------+
                     |
                     v
+------------------+    +------------------+
|  MarketSnapshot  |<---|     main.cpp     |--->|   OrderManager   |
|  bids / asks     |    |  strategy loop   |    |  active orders   |
+------------------+    +------------------+    +------------------+
```

- **`MarketSnapshot`** keeps two `std::map`s of `std::unique_ptr<PriceLevel>`,
  one for bids (sorted high to low via `std::greater`), one for asks
  (sorted low to high). Top of book is `begin()` on each map. When an update
  arrives with `qty == 0` the level is `erase`d and the `unique_ptr` releases
  the heap allocation.
- **`OrderManager`** owns each `MyOrder` in a `std::unique_ptr` keyed by its
  monotonically increasing id. Orders are removed (and their memory freed) on
  full fill or cancel. Partial fills update the running `filled` total and
  status.
- **`main.cpp`** wires it together: load the feed, dispatch each event to the
  snapshot or order manager, and after every market update run a tiny
  strategy that places a `SELL` if the best bid >= 100.20 and a `BUY` if the
  best ask <= 100.16 (and an order at that price/side is not already
  resting). The thresholds and quantity are constants at the top of `main.cpp`.

## Memory safety

- All heap allocations are wrapped in `std::unique_ptr` at the point of
  allocation (`std::make_unique`).
- No raw `new` or `delete` appears in the code — `grep -RnE 'new |delete '
  *.cpp *.h` returns nothing relevant.
- `unique_ptr` lives inside `std::map`, so erasing the entry frees the
  payload deterministically (RAII).
- Verified clean under `-fsanitize=address` (`make run-asan`).

## Build and run

```
make            # builds ./trader
make run        # runs ./trader sample_feed.txt and tees output.log
make asan       # builds ./trader_asan with AddressSanitizer
make run-asan   # runs the ASAN build
make clean
```

You can pass an alternate feed file: `./trader path/to/feed.txt`.

## Verifying correctness

1. `make run` and inspect `output.log`. Expected behavior on the bundled
   feed:
   - Bid/ask levels are added and removed as the feed dictates.
   - When the best bid touches 100.20 the strategy places `SELL` order #1.
   - `EXECUTION 1 10` and `EXECUTION 1 20` produce two partial-fill lines.
   - When the best ask drops to 100.16 the strategy places `BUY` order #2,
     which is then fully filled and removed.
   - `EXECUTION 1 20` completes order #1 (10 + 20 + 20 = 50) and removes it.
2. `make run-asan` should print the same trade output and exit `0` with no
   leak / use-after-free reports from AddressSanitizer.

## Files

| File                 | Purpose                                       |
|----------------------|-----------------------------------------------|
| `market_snapshot.h/.cpp` | local bid/ask book                        |
| `order_manager.h/.cpp`   | active-order ledger                       |
| `feed_parser.h`          | header-only loader for `sample_feed.txt`  |
| `main.cpp`               | strategy loop                             |
| `sample_feed.txt`        | scripted market events + executions       |
| `Makefile`               | normal + ASAN builds                      |
| `output.log`             | last captured run                         |
