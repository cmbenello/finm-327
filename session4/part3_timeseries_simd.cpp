// Part 3: Moving-average over a tick price series, scalar vs SIMD.
// AVX2 path on x86_64; NEON path on Apple Silicon / aarch64; scalar fallback otherwise.

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#if defined(__AVX2__)
  #include <immintrin.h>
  #define HAS_AVX2 1
#elif defined(__ARM_NEON) || defined(__aarch64__)
  #include <arm_neon.h>
  #define HAS_NEON 1
#endif

// O(N) running moving average using a single sliding sum (subtract leaving,
// add entering). out[i] is the mean of prices[i .. i+W-1].
static void moving_avg_scalar(const double* p, size_t n, size_t W, double* out) {
    if (n < W) return;
    double sum = 0.0;
    for (size_t i = 0; i < W; ++i) sum += p[i];
    double inv = 1.0 / static_cast<double>(W);
    out[0] = sum * inv;
    const size_t last = n - W;
    for (size_t i = 1; i <= last; ++i) {
        sum += p[i + W - 1] - p[i - 1];
        out[i] = sum * inv;
    }
}

// Brute-force window sum at every position, recomputed from scratch each step.
// O(N*W) — slow, but the regular inner loop is trivially vectorizable, so this
// is where SIMD pays off vs the scalar version of the same brute-force routine.
static void moving_avg_brute_scalar(const double* p, size_t n, size_t W, double* out) {
    if (n < W) return;
    double inv = 1.0 / static_cast<double>(W);
    const size_t last = n - W;
    for (size_t i = 0; i <= last; ++i) {
        double s = 0.0;
        for (size_t j = 0; j < W; ++j) s += p[i + j];
        out[i] = s * inv;
    }
}

#if HAS_AVX2
static void moving_avg_brute_simd(const double* p, size_t n, size_t W, double* out) {
    if (n < W) return;
    double inv = 1.0 / static_cast<double>(W);
    const size_t last = n - W;
    for (size_t i = 0; i <= last; ++i) {
        __m256d acc = _mm256_setzero_pd();
        size_t j = 0;
        for (; j + 4 <= W; j += 4) {
            __m256d v = _mm256_loadu_pd(p + i + j);
            acc = _mm256_add_pd(acc, v);
        }
        double tmp[4]; _mm256_storeu_pd(tmp, acc);
        double s = tmp[0] + tmp[1] + tmp[2] + tmp[3];
        for (; j < W; ++j) s += p[i + j];
        out[i] = s * inv;
    }
}
static const char* simd_kind() { return "AVX2 (256-bit, 4xf64)"; }

#elif HAS_NEON
static void moving_avg_brute_simd(const double* p, size_t n, size_t W, double* out) {
    if (n < W) return;
    double inv = 1.0 / static_cast<double>(W);
    const size_t last = n - W;
    for (size_t i = 0; i <= last; ++i) {
        float64x2_t acc0 = vdupq_n_f64(0.0);
        float64x2_t acc1 = vdupq_n_f64(0.0);
        size_t j = 0;
        for (; j + 4 <= W; j += 4) {
            acc0 = vaddq_f64(acc0, vld1q_f64(p + i + j));
            acc1 = vaddq_f64(acc1, vld1q_f64(p + i + j + 2));
        }
        float64x2_t acc = vaddq_f64(acc0, acc1);
        double s = vgetq_lane_f64(acc, 0) + vgetq_lane_f64(acc, 1);
        for (; j < W; ++j) s += p[i + j];
        out[i] = s * inv;
    }
}
static const char* simd_kind() { return "NEON (128-bit, 2xf64)"; }

#else
static void moving_avg_brute_simd(const double* p, size_t n, size_t W, double* out) {
    moving_avg_brute_scalar(p, n, W, out);
}
static const char* simd_kind() { return "scalar (no SIMD)"; }
#endif

template <typename F>
double time_ms(F&& f) {
    auto t0 = std::chrono::steady_clock::now();
    f();
    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

int main() {
    constexpr size_t N = 1 << 20;   // 1M ticks
    constexpr size_t W = 64;        // 64-tick window

    std::mt19937_64 rng(11);
    std::normal_distribution<double> step(0.0, 0.1);
    std::vector<double> prices(N);
    double px = 100.0;
    for (auto& x : prices) { px += step(rng); x = px; }

    std::vector<double> out_sliding(N - W + 1);
    std::vector<double> out_brute(N - W + 1);
    std::vector<double> out_simd(N - W + 1);

    double t_sliding = time_ms([&] { moving_avg_scalar(prices.data(), N, W, out_sliding.data()); });
    double t_brute   = time_ms([&] { moving_avg_brute_scalar(prices.data(), N, W, out_brute.data()); });
    double t_simd    = time_ms([&] { moving_avg_brute_simd(prices.data(), N, W, out_simd.data()); });

    // Correctness: brute-scalar and brute-simd must agree to within rounding.
    double maxerr = 0.0;
    for (size_t i = 0; i < out_brute.size(); ++i) {
        double e = std::abs(out_brute[i] - out_simd[i]);
        if (e > maxerr) maxerr = e;
    }

    std::printf("=== Part 3: Moving Average (N=%zu, W=%zu) ===\n", N, W);
    std::printf("SIMD path:        %s\n", simd_kind());
    std::printf("%-30s %12s\n", "method", "time(ms)");
    std::printf("%-30s %12.2f   (O(N), reference)\n", "sliding-sum scalar", t_sliding);
    std::printf("%-30s %12.2f   (O(N*W) baseline)\n", "brute scalar", t_brute);
    std::printf("%-30s %12.2f   (O(N*W) vectorized)\n", "brute SIMD", t_simd);
    std::printf("speedup brute_scalar -> SIMD: %.2fx\n", t_brute / t_simd);
    std::printf("max abs error (brute vs SIMD): %.3e\n", maxerr);
    return 0;
}
