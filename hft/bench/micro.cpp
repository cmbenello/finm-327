#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <random>
#include <sched.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>
#if defined(__x86_64__) || defined(__i386__)
  #include <immintrin.h>
#endif

#include "../src/trace.hpp"

#if !defined(__x86_64__) && !defined(__i386__)
  #define _mm_pause() __asm__ __volatile__("" ::: "memory")
#endif

using namespace std::chrono;
using hft::N;
using hft::MOD;

alignas(64) static int32_t A[N*N];
alignas(64) static int32_t B[N*N];

// Worker thread state, cache-line aligned to avoid false sharing.
alignas(64) static std::atomic<int> work_state{0};
alignas(64) static const char* worker_p = nullptr;
alignas(64) static std::atomic<bool> worker_exit{false};
static std::thread worker_thread;
static int  g_worker_cpu = -1;
static bool g_use_worker = false;

static std::string make_payload(uint32_t seed, int cid) {
    std::mt19937 rng(seed);
    std::string s;
    s.reserve(1 << 19);
    char num[16];
    int len = snprintf(num, sizeof(num), "%d\n%d\n", cid, N);
    s.append(num, len);
    auto emit = [&](int v){
        len = snprintf(num, sizeof(num), "%d ", v);
        s.append(num, len);
    };
    for (int i = 0; i < N*N; ++i) emit(rng() % MOD);
    s.push_back('\n');
    for (int i = 0; i < N*N; ++i) emit(rng() % MOD);
    s.push_back('\n');
    return s;
}

static inline const char* parse_int(const char* p, int32_t& out) {
    while (*p < '0') ++p;
    unsigned d0 = (unsigned char)p[0] - '0';
    unsigned d1 = (unsigned char)p[1] - '0';
    if (d1 >= 10u) { out = (int32_t)d0; return p + 1; }
    unsigned d2 = (unsigned char)p[2] - '0';
    if (d2 >= 10u) { out = (int32_t)(d0*10 + d1); return p + 2; }
    out = (int32_t)(d0*100 + d1*10 + d2);
    return p + 3;
}

static inline const char* parse_int_s1(const char* p, int32_t& out) {
    unsigned d0 = (unsigned char)p[0] - '0';
    unsigned d1 = (unsigned char)p[1] - '0';
    unsigned d2 = (unsigned char)p[2] - '0';
    if (d1 >= 10u) { out = (int32_t)d0; return p + 2; }
    if (d2 >= 10u) { out = (int32_t)(d0*10 + d1); return p + 3; }
    out = (int32_t)(d0*100 + d1*10 + d2);
    return p + 4;
}

// SIMD scan for the first '\n' starting at p. Tiered fallback.
static inline const char* find_newline(const char* p) {
#if defined(HFT_HAVE_AVX512)
    while (true) {
        __m512i v = _mm512_loadu_si512((const void*)p);
        __mmask64 m = _mm512_cmpeq_epi8_mask(v, _mm512_set1_epi8('\n'));
        if (m) return p + __builtin_ctzll(m);
        p += 64;
    }
#elif defined(HFT_HAVE_AVX2)
    while (true) {
        __m256i v = _mm256_loadu_si256((const __m256i*)p);
        __m256i eq = _mm256_cmpeq_epi8(v, _mm256_set1_epi8('\n'));
        uint32_t m = (uint32_t)_mm256_movemask_epi8(eq);
        if (m) return p + __builtin_ctz(m);
        p += 32;
    }
#else
    while (*p != '\n') ++p;
    return p;
#endif
}

static int read_topology_int(const char* what, int cpu) {
    char path[256];
    std::snprintf(path, sizeof(path),
                  "/sys/devices/system/cpu/cpu%d/topology/%s", cpu, what);
    FILE* f = std::fopen(path, "r");
    if (!f) return -1;
    int v = -1;
    if (std::fscanf(f, "%d", &v) != 1) v = -1;
    std::fclose(f);
    return v;
}

static int pick_worker_cpu() {
    int main_cpu = sched_getcpu();
    if (main_cpu < 0) main_cpu = 0;
    int n_cpus = (int)sysconf(_SC_NPROCESSORS_ONLN);
    if (n_cpus <= 1) return -1;
    int main_socket = read_topology_int("physical_package_id", main_cpu);
    int main_core   = read_topology_int("core_id", main_cpu);
    auto try_pick = [&](bool require_same_socket, bool require_diff_core) -> int {
        for (int offset = 1; offset < n_cpus; ++offset) {
            int cpu = (main_cpu + offset) % n_cpus;
            int sock = read_topology_int("physical_package_id", cpu);
            int core = read_topology_int("core_id", cpu);
            if (require_same_socket && sock != main_socket) continue;
            if (require_diff_core && core == main_core) continue;
            if (sock < 0) continue;
            return cpu;
        }
        return -1;
    };
    int c;
    if ((c = try_pick(true,  true))  >= 0) return c;
    if ((c = try_pick(false, true))  >= 0) return c;
    if ((c = try_pick(false, false)) >= 0) return c;
    return -1;
}

static void worker_fn() {
    if (g_worker_cpu >= 0) {
        cpu_set_t cs;
        CPU_ZERO(&cs);
        CPU_SET(g_worker_cpu, &cs);
        sched_setaffinity(0, sizeof(cs), &cs);
    }
    sched_param sp{};
    sp.sched_priority = 80;
    sched_setscheduler(0, SCHED_FIFO, &sp);  // best-effort
    while (true) {
        while (work_state.load(std::memory_order_acquire) != 1) {
            if (worker_exit.load(std::memory_order_relaxed)) return;
            _mm_pause();
        }
        const char* p = worker_p;
        for (int k = 0; k < N; ++k) {
            for (int i = 0; i < N; ++i) {
                int32_t v;
                p = parse_int_s1(p, v);
                B[i*N + k] = v;
            }
        }
        work_state.store(2, std::memory_order_release);
    }
}

static int run_one(const std::string& payload, int64_t& parse_ns, int64_t& compute_ns, int64_t& mod_ns) {
    const char* p = payload.data();
    int32_t cid, n;

    auto t0 = steady_clock::now();
    p = parse_int_s1(p, cid);
    p = parse_int_s1(p, n);

    if (g_use_worker) {
        const char* a_end = find_newline(p);
        worker_p = a_end + 1;
        work_state.store(1, std::memory_order_release);
        for (int i = 0; i < N*N; ++i) p = parse_int_s1(p, A[i]);
        while (work_state.load(std::memory_order_acquire) != 2) _mm_pause();
        work_state.store(0, std::memory_order_relaxed);
    } else {
        for (int i = 0; i < N*N; ++i) p = parse_int_s1(p, A[i]);
        ++p;
        for (int k = 0; k < N; ++k) {
            for (int i = 0; i < N; ++i) {
                int32_t v;
                p = parse_int_s1(p, v);
                B[i*N + k] = v;
            }
        }
    }
    auto t1 = steady_clock::now();

    int64_t tr = hft::trace_AB_T(A, B);
    auto t2 = steady_clock::now();

    int ans = (int)((tr % MOD + MOD) % MOD);
    auto t3 = steady_clock::now();

    parse_ns   = duration_cast<nanoseconds>(t1 - t0).count();
    compute_ns = duration_cast<nanoseconds>(t2 - t1).count();
    mod_ns     = duration_cast<nanoseconds>(t3 - t2).count();
    return ans;
}

int main(int argc, char** argv) {
    int iters = (argc > 1) ? atoi(argv[1]) : 200;
    std::vector<std::string> payloads;
    payloads.reserve(8);
    for (int i = 0; i < 8; ++i) payloads.push_back(make_payload(0xdeadbeef + i, i));

    g_worker_cpu = pick_worker_cpu();
    g_use_worker = (g_worker_cpu >= 0);
    if (g_use_worker) worker_thread = std::thread(worker_fn);

    for (int i = 0; i < 5; ++i) { int64_t a,b,c; run_one(payloads[i & 7], a, b, c); }

    std::vector<int64_t> tot, ps, cs;
    tot.reserve(iters); ps.reserve(iters); cs.reserve(iters);

    int sink = 0;
    for (int i = 0; i < iters; ++i) {
        int64_t pn, cn, mn;
        sink ^= run_one(payloads[i & 7], pn, cn, mn);
        ps.push_back(pn);
        cs.push_back(cn);
        tot.push_back(pn + cn + mn);
    }

    auto pct = [](std::vector<int64_t>& v, double q) {
        std::sort(v.begin(), v.end());
        size_t idx = (size_t)(q * (v.size() - 1));
        return v[idx];
    };

    fprintf(stderr, "sink=%d (ignore)\n", sink);
    printf("iters=%d  parse_us(p50/p99)=%.2f/%.2f  compute_us(p50/p99)=%.2f/%.2f  total_us(p50/p99)=%.2f/%.2f\n",
           iters,
           pct(ps, 0.5)/1000.0,  pct(ps, 0.99)/1000.0,
           pct(cs, 0.5)/1000.0,  pct(cs, 0.99)/1000.0,
           pct(tot, 0.5)/1000.0, pct(tot, 0.99)/1000.0);

    if (g_use_worker) {
        worker_exit.store(true, std::memory_order_release);
        work_state.store(1, std::memory_order_release);
        if (worker_thread.joinable()) worker_thread.join();
    }
    return 0;
}
