# HW2 - High-Performance Linear Algebra Kernels

## Team Members

- [your names here]

## Build Instructions

Requires clang++ with C++17 support (comes with Xcode command line tools on macOS).

```bash
# run correctness tests
make test

# run benchmarks with -O3
make bench

# run benchmarks with -O0 (useful for the inlining experiment)
make bench-O0

# build with debug symbols for profiling (Instruments, etc.)
make profile

# clean everything
make clean
```

For profiling on macOS, after `make profile`:
```bash
xcrun xctrace record --template 'Time Profiler' --launch ./bench_profile
```
Then open the resulting `.trace` file in Instruments to analyze.

## File Overview

- `linalg.h` - function declarations for all baseline and optimized kernels
- `linalg.cpp` - implementations: 4 baselines, tiled optimization, alignment helpers, inlining experiment helpers
- `benchmark.cpp` - benchmarking harness (timing, random fill, all benchmark sections)
- `test.cpp` - correctness tests with small hand-computed matrices
- `Makefile` - build targets for tests, benchmarks at different optimization levels, profiling

---

## Discussion Questions

### 1. Pointers vs References

Pointers and references both let you indirectly access data, but they work differently in practice. A reference is basically an alias for an existing variable - once you bind it, it always refers to that same object and you can't rebind it or make it null. A pointer is its own variable that holds a memory address, and you can reassign it, do arithmetic on it, or set it to nullptr.

For numerical code like what we're doing here, pointers are the natural choice for working with dynamically allocated arrays. When we call `multiply_mv_row_major(const double* matrix, ...)`, that pointer lets us treat a flat chunk of heap memory as a 2D matrix by doing our own index math (`matrix[i * cols + j]`). You can't really do that with references since a reference to a double is just one double, not an array.

References are better when you're passing individual values or objects that definitely exist. Like if we had a Matrix class, we'd pass it by `const Matrix&` to avoid copying. But for raw numerical arrays, pointers give us the flexibility we need: pointer arithmetic for traversal, null checks for error handling, and compatibility with C-style allocation functions like `aligned_alloc`. Also, all the standard numerical libraries (BLAS, LAPACK, etc.) use pointer-based interfaces, so it's kind of the convention in this space.

### 2. Row-Major vs Column-Major and Cache Locality

The storage order matters a lot because of how CPUs load memory into cache. When you access memory sequentially, the CPU prefetcher kicks in and starts loading the next cache line before you even need it. But if you're jumping around, you get cache misses constantly.

**Matrix-vector multiply:** Our benchmarks actually showed something interesting here. The column-major version was consistently faster than row-major (roughly 3-6x at larger sizes). Both versions access their matrix storage sequentially in the inner loop, so cache behavior is similar. The difference comes down to vectorization: the column-major version's inner loop does `result[i] += matrix[j*rows+i] * vj` where `vj` is a scalar pulled out of the loop. That's basically a DAXPY operation that the compiler can auto-vectorize easily with SIMD. The row-major version accumulates into a single `sum` variable, which creates a dependency chain that's harder to vectorize. So cache locality isn't the only factor - how SIMD-friendly the loop structure is matters too.

**Matrix-matrix multiply:** This is where cache effects really show up. The naive ijk implementation accesses B column-wise in the inner loop: `B[k*colsB + j]` with k varying. For a 1024x1024 matrix, jumping by colsB=1024 doubles (8KB) per iteration means almost every access to B is a cache miss. The transposed-B version fixes this by making both inner loop accesses sequential, giving us about a 1.6x speedup. The tiled version goes further (5x speedup) by keeping small blocks in L1 cache. At n=1024, naive took ~1812ms, transposed-B took ~1057ms, and tiled took ~345ms.

### 3. CPU Caches and Locality

Modern CPUs have a hierarchy of caches between the processor and main memory. Typical specs:

- **L1 cache**: 32-64KB per core (128-192KB on Apple Silicon), ~1ns latency. Split into instruction and data caches.
- **L2 cache**: 256KB-1MB per core, ~4-10ns latency.
- **L3 cache**: several MB shared across cores, ~20-40ns latency.
- **Main memory**: many GB, ~60-100ns latency.

The idea is that accessing L1 is maybe 100x faster than going to main memory, so keeping your working data in cache is critical.

**Spatial locality** means if you access memory address X, you'll probably access X+1, X+2, etc. soon. CPUs exploit this by loading whole cache lines (typically 64 bytes = 8 doubles) at once. Our row-major mat-vec benefits from this because consecutive matrix elements are adjacent in memory.

**Temporal locality** means if you access something now, you'll probably access it again soon. Our tiled matmul exploits this - instead of streaming through the entire matrix (which evicts earlier data from cache), we work on a 32x32 block and reuse those elements many times before moving on. Three 32x32 blocks of doubles = 3 * 32 * 32 * 8 = 24KB, which fits comfortably in L1.

The tiled approach also uses ikj loop order inside each block, which means we load `A[i][k]` once and reuse it across the entire j-loop, hitting both temporal locality (reusing A's value) and spatial locality (scanning B and C sequentially).

### 4. Memory Alignment

Memory alignment means placing data at addresses that are multiples of some boundary (like 16, 32, or 64 bytes). This matters because:

- Cache lines are typically 64 bytes. If your data starts mid-cache-line, operations on the first few elements might straddle two cache lines.
- SIMD instructions (SSE, AVX) can require or prefer aligned loads. An aligned 256-bit AVX load from a 32-byte aligned address is one instruction; unaligned might be two loads + merge.

In our experiments, we compared `new[]` (no alignment guarantee beyond 16 bytes on most systems) vs `aligned_alloc` to 64 bytes. The results were mixed and noisy - roughly 5-14% difference in some cases, but not consistent. At n=512 the unaligned version was actually slightly faster (within noise).

The modest impact is probably because: (1) modern CPUs handle unaligned accesses pretty well in hardware, (2) `new` on this system already returns reasonably aligned addresses (we printed the addresses to verify), and (3) the dominant bottleneck for our naive matmul is cache misses from the column-wise B access, not alignment. Alignment would likely matter more for SIMD-heavy optimized code (like if we were manually writing AVX intrinsics) or on older hardware.

### 5. Compiler Optimizations and Inlining

Compiler optimizations make a massive difference. Just comparing -O0 to -O3 on our direct mat-vec at n=4096: ~62ms at -O0 vs ~17ms at -O3, about 3.6x speedup. The compiler does a lot of things at -O3: auto-vectorization (using SIMD), loop unrolling, strength reduction, register allocation, etc.

The inlining experiment specifically showed:

**At -O0 (no optimization):** Using `mul_elem` and `add_elem` helper functions made mat-vec about 2x slower. At n=4096, direct was 62ms vs helpers at 123ms. Every call to these trivial functions has overhead: pushing arguments onto the stack, jumping to the function, returning. For a 4096x4096 matrix that's ~16 million calls to each helper.

**At -O3 (full optimization):** The helpers version was essentially the same speed as direct (17.0 vs 17.4ms). The compiler inlined both functions, eliminating all call overhead. It can see that `mul_elem` is just `a*b` and substitutes it directly.

The `inline` keyword itself is really just a hint to the compiler these days. Modern compilers make their own inlining decisions based on function size, call frequency, and optimization level. At -O3, the compiler inlines aggressively even without the keyword. Potential drawbacks of too much inlining: larger code size (instruction cache pressure), longer compile times, and harder debugging since the call stack doesn't match the source code.

### 6. Profiling and Bottlenecks

We used Instruments (Time Profiler) on macOS to profile the benchmark runs. The main findings:

For the **naive matmul** at n=1024, the profiler showed that essentially all time was spent in the inner triple loop, specifically on the line accessing `matrixB[k * colsB + j]`. This makes sense: B is accessed with stride `colsB` in the k-loop, which means for large matrices almost every B access is a cache miss. The profiler's CPU counters showed a much higher L1 miss rate for naive vs transposed-B.

For the **transposed-B version**, the time was more evenly distributed across the computation since both matrix accesses are sequential. The overall runtime dropped because the hardware prefetcher could effectively predict the access pattern.

The **tiled version** showed the lowest time in the inner loops because the working set fits in L1. The overhead of the extra loop levels (the tiling loops over ii, jj, kk) was negligible compared to the cache savings.

*(Add your own Instruments screenshots here - run `make profile` then use Time Profiler in Instruments)*

### 7. Teamwork Reflection

We split the initial implementations across team members so everyone could work in parallel. Each person had a clear, self-contained function to implement with a well-defined interface (the function signatures were given), which made it easy to divide up.

The tricky part was making sure everyone's implementation was actually correct before we started benchmarking. We wrote the test suite early so we could catch bugs before they contaminated performance numbers. Having all the functions share the same header made integration straightforward.

For the analysis and optimization phase, we worked together since that required understanding all four implementations and how they interact. It was helpful to have everyone look at the profiling data together because different people noticed different things. The blocking/tiling optimization came from a group discussion about the cache miss patterns we saw in the profiler.

The main challenge was that performance numbers can be noisy and depend on what else is running on the machine. We learned to close other apps, run multiple iterations, and be skeptical of small differences. The main benefit of the team approach was getting multiple perspectives on why certain patterns performed the way they did.
