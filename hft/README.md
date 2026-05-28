# HFT Matrix Challenge — Client

**Group name:** `group8`

A low-latency TCP client for the FINM 327 HFT Matrix Challenge. The server
broadcasts an `N x N` matrix-multiply challenge every ~10 seconds; the client
parses it, computes `trace(A·B) mod 997`, and sends the answer back over the
same socket as fast as possible. Fastest correct response wins the challenge.

## Protocol

Each broadcast is ASCII over TCP:

```
<challenge_id>\n
128\n
<16384 ints, space-separated, each in [0,996]>   # matrix A, row-major
<16384 ints, space-separated, each in [0,996]>   # matrix B, row-major
```

The answer is `trace(A·B) mod 997` as a single decimal integer. Note this is
`O(N^2)`: `trace(A·B) = Σ_i Σ_k A[i][k]·B[k][i]` — only the diagonal of the
product is needed, never the full matmul.

## Build

Plain `g++-9` and standard headers — no external libraries.

```
make -j4
```

Produces `bin/client` (plus the local mock `bin/server`, the `bin/client_naive`
reference, and the `bin/micro` / `bin/correctness` benchmarks).

## Run

```
./bin/client <host> <port> <group_name>
```

Defaults: `127.0.0.1 12345 group8`. Pin to a dedicated core for best latency:

```
taskset -c 4 ./bin/client <host> 12345 group8
```

Running as root (or with `CAP_SYS_NICE` + cpufreq write access) lets the client
apply its real-time and CPU-frequency optimizations; without privilege it
degrades gracefully and still runs correctly.

## Optimizations

**Parsing (the dominant cost — ~130 KB of ASCII ints per challenge):**
- Branchless fixed-width integer parser (`parse_int_s1`): all digit-byte loads
  issued up front, no separator-scanning loop, separator length folded into the
  pointer advance.
- SIMD newline scan (`find_newline`) to split the A and B blocks: AVX-512 /
  AVX2 / scalar tiers chosen at compile time.
- Multi-threaded parse: a pinned worker thread parses B (directly into a
  transposed layout) in parallel with the main thread parsing A. The worker CPU
  is chosen by reading `/sys/.../topology` at runtime — same socket, different
  physical core — with serial fallback on single-core or restricted hosts.

**Compute (`trace_AB_T`):**
- B is stored transposed during parse so the trace reduces to a single
  contiguous dot product `Σ A[j]·B_T[j]`, giving sequential, cache-friendly
  access on both operands.
- Runtime/compile-time SIMD tiers: AVX-512 and AVX2 pack int32→int16
  (`VPACKUSDW`) then `VPMADDWD` for double-throughput 16-bit multiply-add, with
  multiple independent accumulators; scalar path auto-vectorizes. Values in
  `[0,996]` fit safely in 16 bits.

**Memory & scheduling:**
- Static 64-byte-aligned buffers; `mlockall` + pre-touch to eliminate page
  faults on the hot path.
- Best-effort `SCHED_FIFO` real-time priority on the main and worker threads.

**Network / latency:**
- `TCP_NODELAY`, enlarged `SO_RCVBUF`, and `TCP_QUICKACK` re-armed every
  challenge (it is one-shot on Linux) so payload ACKs are never delayed.
- Busy-poll receive: spins on non-blocking `recv` and only falls back to a
  blocking `recv` after a budget, so the common case is caught in userspace
  without a syscall sleep. (A pure never-blocking spin was tried and reverted —
  under `SCHED_FIFO` on a non-isolated core it starves kernel softirq packet
  delivery; the bounded fallback is load-bearing.)

**CPU frequency:**
- The client self-promotes its pinned core(s) to the `performance` cpufreq
  governor and pins `scaling_min_freq` to the max at startup. Between broadcasts
  the core is idle for ~10 s, so a power-saving governor would let it drop to
  base frequency and answer the next challenge cold; pinning the floor keeps it
  at turbo. Best-effort — silently no-ops without privilege.

## Benchmarks / testing

- `./bin/correctness` — parser + trace validated against an independent
  `istringstream` + `O(N^3)` reference over 25 random payloads. Must print
  `PASS`.
- `./bin/micro <iters>` — synthetic-payload microbenchmark (parse / compute /
  total percentiles, no network).
- `bash scripts/race.sh <seconds>` — runs the mock server plus this client and
  the naive reference locally and reports win counts and latency percentiles.
