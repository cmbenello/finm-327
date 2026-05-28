# Iteration notes

## v1 transpose-B + linear trace
Store B transposed during parse, compute trace as linear dot product. parse barely changed (~85.3us), compute 9.66 -> 3.19us. total 95.41 -> 88.54us. Kept.

## v2 sep1 parse (skip while-loop)
Knowing payload structure: each int followed by exactly 1 separator (only exception is the matrix-end " \n" pair). Use parse_int_s1 that omits the leading while-skip. parse 85.33 -> 63.22us (-22us). total 88.54 -> 66.43us. Kept.

## v3 SIMD 4-int parser with 3-digit fast path (REVERTED)
Load 16 bytes, check pattern 0x8888 (all 4 are 3-digit), then maddubs/madd to parse. Fast path 65% hit, slow falls back to scalar. parse went UP 63 -> 75us — branch misprediction overhead + slow path cost dominates. Reverted.

## v4 int16 storage + AVX-512 VPMADDWD trace (REVERTED)
Store A,B as int16. compute 3.19 -> 0.48us. But parse went 63 -> 69us (probably narrowing-cast codegen + 16-bit strided B stores). Net loss. Reverted.

## v5 AVX-512 trace + pack-to-int16 (REVERTED)
Hand-write trace as int32->int16 pack + VPMADDWD. compute 3.19 -> 1.12us. But parse regressed 63 -> 67us (codegen ripple from include / inlining changes). Reverted.

## v6 hoist p[2] load (KEPT)
Move d2 = p[2]-30 above the d1>=10 branch so all 3 byte loads issue in parallel. parse 63.22 -> 61.40us. total 66.43 -> 64.61us.

## v7 AVX-512 trace via packus + VPMADDWD
Pack int32 -> int16 with VPACKUSDW, then VPMADDWD for 16x16->32 mul-adds. compute 3.19 -> 0.99us. parse drifted +1.2us. total 64.61 -> 63.59us. Kept.

## v8 single 4-byte load + byte extract (REVERTED)
Replace 3 byte loads with memcpy + shifts. Parse 62 -> 72us. gcc generated worse code (extra shifts/masks). Reverted.

## v9 quad-accumulator AVX-512 trace (REVERTED)
4 independent acc registers, 128-int unrolling per inner. compute unchanged at 0.99us (dependency wasn't the bottleneck). parse drifted up 62.58 -> 65.79us — code layout ripple again. Reverted.

## v15 simplified AVX-512 trace (KEPT)
Drop intermediate int64 reduction from CHUNK loop; single int32 accumulator + one final reduction. compute 0.99 -> 0.85us. total 63.59 -> 63.43us. Kept.

## v16-v23: 8 reverts in a row exhausting parse_int micro-opts
Tried: pointer-loop, prefetch-B-lines, VNNI dpwssd, remove unused parse_int, SIMD scan + position-driven parse (both branchless and branchy variants), branchless parse with cmov, GCC unroll pragma. All regressed parse_p50 by 2-12us. Eight consecutive failures triggered the stop signal.

## v24 multi-threaded parse, worker on cpu6 (KEPT)
Different category, completely new approach: spawn a worker thread pinned to cpu6 (same socket as main on cpu4, so coherence goes through shared L3). Main does a SIMD '\n' scan to find A's end, hands B's start to the worker, then parses A while worker parses B in parallel. After both finish, main's trace pulls B from cpu6's L1 via L3 coherence (visible as compute jumping 0.85 -> 3.6us). Net total 63.43 -> 39.22us (-24us). Tail p99 climbs to ~120us (scheduling jitter), but p50 wins decisively.

## v25-v32: refinements / attempts inside the multi-thread regime
- v25 trace-on-worker: reverted; coherence cost just shifts
- v26 prefetch B in trace: reverted; the prefetch overhead exceeds savings
- v27 worker prefetch recvbuf: reverted; ~2us extra for not enough win
- v28 worker on cpu36 (SMT sibling of cpu4): reverted; shared physical core serialises
- v29 worker on cpu8: ~same as cpu6 (39.19 vs 39.22), not committed
- v30 dual-accumulator trace (KEPT): tiny additional pipelining via two accumulators. total 39.22 -> 39.18us.
- v31 split-trace: reverted - race condition because B_T storage layout means the "first half" of storage isn't written until the entire parse completes (worker writes are strided).
- v32 worker self-finds A_end: reverted; worker's extra scan time exceeds main's saved find_newline.

## v33 worker SCHED_FIFO priority (KEPT)
Worker sets sched_param=80, SCHED_FIFO. Reduces scheduling jitter — both p50 and p99 improve. total 39.18 -> 38.13us.

## v34 quad-accumulator trace (KEPT)
Four parallel accumulators, 128-int unroll. Tiny additional pipelining over v30's dual-acc. total 38.13 -> 38.10us.

## v35-v36: failures
- v35 packed sync cache line: same perf, reverted under strict rule.
- v36 no cpu_set on worker: catastrophic — worker on cpu4 with FIFO priority starved main. total p50 = 32 ms. Reverted.

## Final
Best total_p50 = 38.10us (v34). From baseline 95.41us → -57.31us (60% faster).

Iterations kept: v1 (transpose B) -6.87, v2 (sep1 parse) -22.11, v6 (hoist load) -1.82, v7 (AVX-512 trace) -1.02, v15 (simpler trace) -0.16, v24 (multi-thread parse) -24.21, v30 (dual-acc) -0.04, v33 (FIFO) -1.05, v34 (quad-acc) -0.03.

Race.sh validation: cascade-fast wins 4/5 (80%) with median 4ms vs cascade-naive's 10ms.

## v37-v43: more attempts in the multi-thread regime, all reverted
- v37 mlockall in micro: regression (parse 38us)
- v38 worker FIFO priority 99: regression (39-40us)
- v39 worker on cpu2: ~same p50, better p99 but not lower
- v40 worker on cpu0: ~same p50, not lower
- v41 worker on cpu14: ~same p50, not lower
- v42 three-way split (main + 2 workers, A+B partitioned with k=48 cache-aligned boundary): parse 43us, total 47us. Adding a 2nd worker on cpu8 brought ~7us of synchronisation/coherence overhead even in a sanity-check config where workers had minimal work. The 3-way parallelism gain (~10us) was eaten by the overhead.
- v43 worker scans for a_end itself (off main's critical path): regression (41us). Worker's scan-then-parse delays B's start enough that worker becomes the new critical path.

The single-worker design (v34) is at the practical limit for this hardware: main parses A on cpu4 while worker parses B on cpu6 (same socket, L3-coherent). Critical path = max(30us main, 30us worker) + ~1us sync + ~3.5us trace coherence ≈ 34-38us measured.

## v44 portability hardening (KEPT)
Made the build robust to different hardware:

1. **Tiered SIMD fallback** in `trace_AB_T` and `find_newline`, selected at compile time:
   - `__AVX512BW__ && __AVX512F__` → AVX-512 path (Skylake-X / Cascade Lake / Ice Lake)
   - `__AVX2__` → AVX2 path (Haswell+, AMD Zen)
   - else → scalar, auto-vectorisable

2. **Runtime CPU-topology discovery** via `/sys/devices/system/cpu/cpu*/topology/{physical_package_id, core_id}`. Scans from `main_cpu+1` outward (instead of cpu0 first, which is often busy with IRQs) and picks: same socket + different physical core, falling back through diff-core / any-non-main / single-core (serial path).

3. **Serial fallback** in `run_one` if no suitable worker CPU exists (single-core box or cgroup-restricted to one CPU).

4. **Best-effort SCHED_FIFO** in the worker (silently degrades to SCHED_OTHER without rtprio rlimits).

Verified locally:
- AVX-512 build (default `-march=native` on Cascade Lake): total_p50 ≈ 39.6µs
- AVX2-only build (`-march=haswell`): total_p50 ≈ 40.5µs (compute slightly slower)
- Scalar build (`-march=x86-64`): total_p50 ≈ 77µs (still ~18µs better than v0 baseline; multi-thread + parse_int_s1 + auto-vec trace all still contribute)

Trade-off vs v34's hardcoded cpu6: ~1.5µs regression for the discovery overhead and slightly slower compute path on AVX2, in exchange for working on any x86_64 Linux box.


## v10 2-int speculative parse (REVERTED)
Load 8 bytes, check bytes 3 and 7 are separators (both-3-digit ~80%), parse both in parallel; fall back scalar otherwise. parse 62 -> 85us. The 20% slow path's branch mispredict at ~17 cy plus larger fast-path body destroys the win. Reverted.

## v45 pure busy-poll, no blocking fallback (REVERTED)
Removed the BUSY_SPIN_BUDGET blocking-recv fallback so recv_payload_busy spins on
MSG_DONTWAIT forever. Hypothesis: keep core hot (governor holds turbo) + zero
kernel wakeup -> lower median/tail. Result: DISASTER. Fast client received zero
broadcasts (c_fast.log empty, server only ever saw naive answer; naive won 8/8).
Root cause: SCHED_FIFO prio-80 RT thread that never blocks on a non-isolated core
starves ksoftirqd/loopback delivery on that core -> packets never reach the socket.
The blocking fallback is load-bearing: yielding lets the kernel deliver packets.
Lesson: cannot pure-spin under RT prio without core isolation + IRQ steering.

## v45 client self-sets performance cpufreq governor (KEPT, e2e win)
The box idles at the powersave governor; cpu4 sits at 1.2GHz between the 10s
broadcasts, so v44 answered the next challenge cold (4ms median, 46ms cold-start
tail). Added best-effort set_performance_governor() writing "performance" to the
pinned core's (and worker core's) scaling_governor at startup. Verified: core
flips powersave/1.2GHz -> performance/~2.95GHz while the client runs; no-ops
silently without root. Clean race after killing leftovers: fast median 4ms->1ms,
wins 4/5 -> 7/8; naive (no self-set, powersave) 10ms->3ms only because I'd set the
whole box to performance in the contaminated run -- with only the client self-
setting, naive stays ~10ms. Micro proxy unchanged (~40us, micro is always-hot so
it never saw the governor issue). NOTE: race.sh cleanup can't kill root-owned
(sudo) clients -- must 'sudo pkill -9 -f bin/client' between races or a stale RT
spinner poisons core 4.

## v46 pin scaling_min_freq=max on client core (KEPT, e2e robustness)
Governor=performance under intel_pstate/HWP is only a hint; idle core still
clocks down between broadcasts. Now also write scaling_max_freq -> scaling_min_freq
for the pinned core(s). Verified idle-gap min_freq 1.2->3.9GHz. Race (box powersave,
client self-promotes): fast median 4ms, 8/9 wins vs naive 10ms. KEY INSIGHT: the
local 4ms floor is dominated by the SERVER (core 30) + loopback softirq still at
powersave 1.2GHz -- only the client core is boosted. Whole-box performance gave
fast 1ms. In the real competition the server is remote over a real network and we
only control our core, so further client micro-opts are unmeasurable locally; the
meaningful local signal is the win-rate (4/5 -> 8/9). Cold-start first challenge
(46ms tail) still loses ~1 of 9.

## v47 re-arm TCP_QUICKACK per challenge + group name shoemaker (KEPT on faith)
Re-arm TCP_QUICKACK after each recv (one-shot on Linux) so the payload ACK isn't
delayed. No local race change (4ms server-bound floor) but no downside on the real
remote path. Default group name set to "shoemaker". Sanity race: fast 7/8 wins,
no regression, correctness PASS.
