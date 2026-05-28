# HFT matrix challenge — autonomous optimization loop

You are running on a remote machine (`cascade`: 2× Xeon Gold 6242 Cascade Lake, 64
logical cores, AVX-512). Your job is to make `~/hft_client/bin/client` answer the
challenge faster than any other client in a class competition, by iterating in a
tight optimize → benchmark → keep-or-revert loop.

## The challenge

A server (`~/hft_client/src/server.cpp`, port 12345) broadcasts every ~10s:

    <challenge_id>\n
    128\n
    <16384 ints, space-separated, each in [0,996]>\n
    <16384 ints, space-separated, each in [0,996]>\n

Client must reply with one integer over the same TCP socket. Server records
latency from broadcast → recv. Lowest latency wins. The server does NOT validate
the answer (just `atoi`s), but the **correctness test must always pass** because
the instructor probably will validate.

The correct answer is `trace(A·B) mod 997` computed as int64. Note this is
O(N²): `Σᵢₖ A[i][k]·B[k][i]`, not a real matmul.

## Workspace

- `~/hft_client/Makefile` — `make -j4` builds everything to `bin/`
- `src/client.cpp`        — the client we're optimizing
- `src/client_naive.cpp`  — slow reference (do not edit)
- `src/server.cpp`        — local mock server (do not edit)
- `src/trace.hpp`         — `hft::trace_AB(A,B)`
- `bench/micro.cpp`       — synthetic-payload microbench (no network)
- `bench/correctness.cpp` — parser + trace vs sscanf+O(N³) reference
- `scripts/race.sh DUR`   — runs server + fast + naive locally for DUR seconds
- `results/log.tsv`       — append your iteration results here (create if absent)
- `results/notes.md`      — append a 1–3 line note per iteration: what you tried,
                            the number, whether you kept or reverted

## Hard rules

1. **`./bin/correctness` must print `PASS: 0 failures` after every change**, or
   you immediately revert via `git checkout -- src/ bench/`. No exceptions.
2. Never edit `src/server.cpp`, `src/client_naive.cpp`, `bench/correctness.cpp`,
   or the protocol-facing parts of `src/client.cpp` in a way that changes what
   gets sent on the wire (must still be `trace(A·B) mod 997` as a decimal int).
3. Never break the build. Always `make -j4` after editing.
4. Don't add dependencies (no apt-get installs). Plain g++-9 and standard headers
   only. AVX2 / AVX-512 intrinsics via `<immintrin.h>` are fine.
5. Don't run anything that needs the public internet.

## Current baseline (already on disk)

- microbench: parse_p50 ≈ 86 µs, compute_p50 ≈ 9.5 µs, total_p50 ≈ 96 µs
- correctness: 25/25 PASS
- end-to-end (local race): fast client wins more than naive but data is sparse

The compute is already small enough that further gains are micro-optimizations.
The big remaining lever is **parse** (86 µs to consume ~130 KB of ASCII ints).

## Iteration protocol (execute this loop)

For each iteration:

1. **Pick one optimization** from the ideas list (or invent your own). Write a
   one-line hypothesis: "I expect parse_p50 to drop by Xµs because Y."
2. **Edit `src/client.cpp`** (and `bench/micro.cpp` for the parse_int copy if
   the parser changed — keep them identical).
3. **Build**: `make -j4`. If it fails, fix it, then continue.
4. **Verify**: `./bin/correctness`. If not PASS, `git checkout -- src/ bench/`
   and skip to next idea.
5. **Measure** (3 runs to filter outliers):
   ```
   for i in 1 2 3; do taskset -c 4 ./bin/micro 2000; done
   ```
   Take the **min** of the 3 p50 totals as the iteration's score.
6. **Compare** to current best in `results/log.tsv`. If lower, append the row:
   `<iso-timestamp>\t<short-name>\t<parse_p50>\t<compute_p50>\t<total_p50>\t kept`
   and `git add -A && git commit -m "v<N>: <name> total=<x>µs"`. If not lower,
   `git checkout -- src/ bench/` and append the row with `reverted`.
7. Append 1–3 lines to `results/notes.md` summarizing the attempt + result.
8. If the last 4 attempts all reverted, switch idea categories (don't keep
   poking the same dead end).

When the absolute-best total has not improved over 8 consecutive iterations,
stop iterating. Print a summary of the top 3 results from `log.tsv` and exit.

## Optimization ideas (rough order of expected impact)

Parse (the dominant cost):
- SIMD separator scan: load 32 bytes via `_mm256_loadu_si256`, OR `cmpeq` for
  `' '` and `'\n'`, `_mm256_movemask_epi8`, then walk separator positions with
  `__builtin_ctz` and parse each fixed-length run via small switch.
- SWAR digit-block parse: load 4 bytes, mask, multiply by `[100,10,1,0]` magic,
  horizontal-add. Branch on length using ctz of the separator mask.
- Fused parse-compute: don't store B at all; while parsing each B[k][i] read,
  immediately do `trace += (int64_t)A[i*N+k] * B_value`. Saves 64 KB of writes.
  Watch the access pattern on A — may want A stored both row-major and
  column-major to get sequential reads in the fused loop.
- Pre-store A transposed during A's parse, then both A_t and B accessed
  sequentially in the trace loop.

Compute (already tiny but cheap to try):
- AVX-512 explicit intrinsics for trace_AB — `_mm512_madd_epi16` style. Ints fit
  in 16 bits so VPMADDWD doubles throughput. Be careful: VPMADDWD does signed
  16×16→32, so values 0..996 are fine.
- Make sure trace loop is auto-vectorizing (`-fopt-info-vec` to check).

End-to-end / system (won't show in micro, but matters):
- `taskset` is already used in race.sh; isolate the chosen core via
  `cat /sys/devices/system/cpu/isolated` and pick a quiet one.
- `sudo cpupower frequency-set -g performance` (sudo is passwordless on
  cascade — check `sudo -n true` first).
- Try `SO_BUSY_POLL` (needs `sysctl net.core.busy_poll=50`).
- Try `MSG_PEEK` ahead of recv to prime the socket.
- Compute the answer for the previous broadcast, then start busy-spinning a few
  hundred ms before the next expected broadcast (server is on a 10s cadence).

Out-of-the-box:
- Drop the answer into the socket via `writev` with both the int and a no-op
  follow byte to potentially trigger an earlier kernel push.
- Connect twice and race the two connections — but the protocol expects one
  registered name. Probably not worth it.

## How to bench end-to-end

```
bash scripts/race.sh 75
```

Runs server + your client + naive client locally for 75 seconds (≈7 broadcasts
each), then prints win counts and per-client min/median/max latencies.

## Logging discipline

- `results/log.tsv` — one row per attempt (kept or reverted)
- `results/notes.md` — one paragraph per attempt, in chronological order
- Commit each kept change separately so we can `git log` the history later
- After every 5 iterations, run `bash scripts/race.sh 75` once and append the
  win-counts to `results/notes.md`

## Getting started

1. `cd ~/hft_client`
2. `mkdir -p results && [ -f results/log.tsv ] || printf 'ts\tname\tparse_us\tcompute_us\ttotal_us\tstatus\n' > results/log.tsv`
3. `make -j4 && ./bin/correctness | tail -3`
4. Record current baseline in log.tsv as `v0_baseline ... kept`
5. Begin iteration loop.

Good luck. May the fastest matrix win.
