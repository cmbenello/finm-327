#pragma once
#include <cstdint>

#if defined(__AVX512BW__) && defined(__AVX512F__)
  #define HFT_HAVE_AVX512 1
  #include <immintrin.h>
#elif defined(__AVX2__)
  #define HFT_HAVE_AVX2 1
  #include <immintrin.h>
#endif

namespace hft {

constexpr int N = 128;
constexpr int MOD = 997;

inline int64_t trace_AB(const int32_t* __restrict__ A,
                        const int32_t* __restrict__ B) {
    int64_t acc = 0;
    for (int i = 0; i < N; ++i) {
        for (int k = 0; k < N; ++k) {
            acc += (int64_t)A[i*N + k] * B[k*N + i];
        }
    }
    return acc;
}

// Dot product Σ A[j] * B_T[j]. A and B_T must be aligned to 64 bytes.
// Compile-time tiered fallback:
//   - AVX-512 (BW): pack int32 -> int16 with VPACKUSDW (values < 2^16),
//     then VPMADDWD for 32-wide mul-add. Two independent accumulators.
//   - AVX2: same idea on 256-bit, with two accumulators to keep int32
//     headroom (each lane sums ≤ 512*2*996² ≈ 1.02e9 between merges).
//   - Scalar: plain int64 dot product (auto-vectorised by the compiler).
inline int64_t trace_AB_T(const int32_t* __restrict__ A,
                          const int32_t* __restrict__ B_T) {
#if defined(HFT_HAVE_AVX512)
    __m512i acc0 = _mm512_setzero_si512();
    __m512i acc1 = _mm512_setzero_si512();
    for (int j = 0; j < N * N; j += 64) {
        __m512i a0 = _mm512_loadu_si512((const void*)(A + j));
        __m512i a1 = _mm512_loadu_si512((const void*)(A + j + 16));
        __m512i a2 = _mm512_loadu_si512((const void*)(A + j + 32));
        __m512i a3 = _mm512_loadu_si512((const void*)(A + j + 48));
        __m512i b0 = _mm512_loadu_si512((const void*)(B_T + j));
        __m512i b1 = _mm512_loadu_si512((const void*)(B_T + j + 16));
        __m512i b2 = _mm512_loadu_si512((const void*)(B_T + j + 32));
        __m512i b3 = _mm512_loadu_si512((const void*)(B_T + j + 48));
        acc0 = _mm512_add_epi32(acc0, _mm512_madd_epi16(_mm512_packus_epi32(a0, a1),
                                                       _mm512_packus_epi32(b0, b1)));
        acc1 = _mm512_add_epi32(acc1, _mm512_madd_epi16(_mm512_packus_epi32(a2, a3),
                                                       _mm512_packus_epi32(b2, b3)));
    }
    __m512i acc = _mm512_add_epi32(acc0, acc1);
    __m512i lo = _mm512_cvtepi32_epi64(_mm512_castsi512_si256(acc));
    __m512i hi = _mm512_cvtepi32_epi64(_mm512_extracti64x4_epi64(acc, 1));
    return _mm512_reduce_add_epi64(_mm512_add_epi64(lo, hi));
#elif defined(HFT_HAVE_AVX2)
    __m256i acc0 = _mm256_setzero_si256();
    __m256i acc1 = _mm256_setzero_si256();
    for (int j = 0; j < N * N; j += 32) {
        __m256i a0 = _mm256_loadu_si256((const __m256i*)(A + j));
        __m256i a1 = _mm256_loadu_si256((const __m256i*)(A + j +  8));
        __m256i a2 = _mm256_loadu_si256((const __m256i*)(A + j + 16));
        __m256i a3 = _mm256_loadu_si256((const __m256i*)(A + j + 24));
        __m256i b0 = _mm256_loadu_si256((const __m256i*)(B_T + j));
        __m256i b1 = _mm256_loadu_si256((const __m256i*)(B_T + j +  8));
        __m256i b2 = _mm256_loadu_si256((const __m256i*)(B_T + j + 16));
        __m256i b3 = _mm256_loadu_si256((const __m256i*)(B_T + j + 24));
        acc0 = _mm256_add_epi32(acc0, _mm256_madd_epi16(_mm256_packus_epi32(a0, a1),
                                                       _mm256_packus_epi32(b0, b1)));
        acc1 = _mm256_add_epi32(acc1, _mm256_madd_epi16(_mm256_packus_epi32(a2, a3),
                                                       _mm256_packus_epi32(b2, b3)));
    }
    __m256i acc = _mm256_add_epi32(acc0, acc1);
    __m256i lo = _mm256_cvtepi32_epi64(_mm256_castsi256_si128(acc));
    __m256i hi = _mm256_cvtepi32_epi64(_mm256_extracti128_si256(acc, 1));
    __m256i s  = _mm256_add_epi64(lo, hi);
    alignas(32) int64_t lanes[4];
    _mm256_store_si256((__m256i*)lanes, s);
    return lanes[0] + lanes[1] + lanes[2] + lanes[3];
#else
    int64_t acc = 0;
    for (int j = 0; j < N * N; ++j) acc += (int64_t)A[j] * B_T[j];
    return acc;
#endif
}

}
