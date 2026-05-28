#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sched.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#if defined(__x86_64__) || defined(__i386__)
  #include <immintrin.h>
#endif

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <thread>

#include "trace.hpp"

#ifndef _mm_pause
#if defined(__x86_64__) || defined(__i386__)
  // already in immintrin.h
#else
  #define _mm_pause() __asm__ __volatile__("" ::: "memory")
#endif
#endif

using hft::N;
using hft::MOD;

static constexpr int   PORT_DEFAULT = 12345;
static constexpr int   RECV_BUF     = 1 << 18;
static constexpr int   PAYLOAD_MAX  = 1 << 19;
static constexpr int   BUSY_SPIN_BUDGET = 1 << 22;

alignas(64) static int32_t A[N*N];
alignas(64) static int32_t B[N*N];
alignas(64) static char    recvbuf[PAYLOAD_MAX];
alignas(64) static char    ansbuf[32];
// Multi-threaded parse: worker parses B in parallel with main parsing A.
alignas(64) static std::atomic<int> work_state{0};
alignas(64) static const char* worker_p = nullptr;
alignas(64) static std::atomic<bool> worker_exit{false};
static std::thread worker_thread;
static int  g_worker_cpu = -1;
static bool g_use_worker = false;

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

// Parses one integer followed by exactly ONE separator byte (' ' or '\n').
// Returns p advanced past the separator. All three byte loads are issued
// upfront so they can execute in parallel.
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

// Best-effort: pin a core to the "performance" cpufreq governor so it stays at
// turbo instead of dropping to min frequency during the ~10s idle between
// broadcasts (which makes the next response start cold and slow). No-ops
// silently without privilege or cpufreq, exactly like mlockall/SCHED_FIFO.
static void write_sysfs(const char* path, const char* val, size_t len) {
    int fd = open(path, O_WRONLY);
    if (fd < 0) return;
    ssize_t w = write(fd, val, len);
    (void)w;
    close(fd);
}

static void set_performance_governor(int cpu) {
    if (cpu < 0) return;
    char path[256];
    std::snprintf(path, sizeof(path),
                  "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_governor", cpu);
    write_sysfs(path, "performance", 11);

    // The governor alone doesn't pin frequency: under Intel HWP an idle core
    // (we block in recv during the ~10s gap) still clocks down, so the next
    // response starts slow. Raise the frequency floor to the ceiling so the
    // core stays at turbo even while idle.
    std::snprintf(path, sizeof(path),
                  "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_max_freq", cpu);
    char maxf[32] = {0};
    int fd = open(path, O_RDONLY);
    if (fd >= 0) {
        ssize_t n = read(fd, maxf, sizeof(maxf) - 1);
        close(fd);
        if (n > 0) {
            while (n > 0 && (maxf[n-1] == '\n' || maxf[n-1] == ' ')) maxf[--n] = 0;
            std::snprintf(path, sizeof(path),
                "/sys/devices/system/cpu/cpu%d/cpufreq/scaling_min_freq", cpu);
            write_sysfs(path, maxf, (size_t)n);
        }
    }
}

// Read a single int from /sys/devices/system/cpu/cpuN/topology/<what>.
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

// Pick a worker CPU: prefer same socket, different physical core,
// scanning outward from main_cpu+1 so we pick a "nearby" CPU instead of
// cpu0 (which is often busy with kernel/IRQs). Falls back to any
// different-physical-core CPU, then any non-main CPU.
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
    if ((c = try_pick(true,  true))  >= 0) return c;  // same socket, diff core
    if ((c = try_pick(false, true))  >= 0) return c;  // diff core, any socket
    if ((c = try_pick(false, false)) >= 0) return c;  // any non-main
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

static inline ssize_t recv_payload_busy(int fd, char* buf, size_t cap) {
    size_t total = 0;
    int seen = 0;
    int idle = 0;
    while (seen < 4) {
        ssize_t r = recv(fd, buf + total, cap - total, MSG_DONTWAIT);
        if (r > 0) {
            for (ssize_t i = 0; i < r; ++i) if (buf[total + i] == '\n') ++seen;
            total += r;
            idle = 0;
            if (total >= cap) return -1;
            continue;
        }
        if (r == 0) return 0;
        if (errno != EAGAIN && errno != EWOULDBLOCK) return -1;
        if (++idle > BUSY_SPIN_BUDGET) {
            ssize_t r2 = recv(fd, buf + total, cap - total, 0);
            if (r2 <= 0) return r2;
            for (ssize_t i = 0; i < r2; ++i) if (buf[total + i] == '\n') ++seen;
            total += r2;
            idle = 0;
            if (total >= cap) return -1;
            continue;
        }
        _mm_pause();
    }
    return total;
}

static void try_real_time() {
    sched_param sp{};
    sp.sched_priority = 80;
    if (sched_setscheduler(0, SCHED_FIFO, &sp) != 0) {
        sp.sched_priority = 0;
        sched_setscheduler(0, SCHED_OTHER, &sp);
    }
}

static void pretouch() {
    for (int i = 0; i < N*N; ++i) { A[i] = 0; B[i] = 0; }
    for (size_t i = 0; i < PAYLOAD_MAX; i += 4096) recvbuf[i] = 0;
}

int main(int argc, char** argv) {
    const char* host = (argc > 1) ? argv[1] : "127.0.0.1";
    int port = (argc > 2) ? atoi(argv[2]) : PORT_DEFAULT;
    const char* name = (argc > 3) ? argv[3] : "shoemaker";

    mlockall(MCL_CURRENT | MCL_FUTURE);
    try_real_time();
    pretouch();
    set_performance_governor(sched_getcpu());
    g_worker_cpu = pick_worker_cpu();
    g_use_worker = (g_worker_cpu >= 0);
    if (g_use_worker) {
        set_performance_governor(g_worker_cpu);
        worker_thread = std::thread(worker_fn);
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    int one = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    int rcv = RECV_BUF;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rcv, sizeof(rcv));
#ifdef TCP_QUICKACK
    setsockopt(sock, IPPROTO_TCP, TCP_QUICKACK, &one, sizeof(one));
#endif

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host, &addr.sin_addr);
    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("connect"); return 1; }

    send(sock, name, strlen(name), 0);

    while (true) {
        ssize_t got = recv_payload_busy(sock, recvbuf, PAYLOAD_MAX);
        if (got <= 0) break;
#ifdef TCP_QUICKACK
        // One-shot on Linux: re-arm each challenge so the kernel ACKs the
        // payload immediately instead of waiting out the delayed-ACK timer.
        setsockopt(sock, IPPROTO_TCP, TCP_QUICKACK, &one, sizeof(one));
#endif

        const char* p = recvbuf;
        int32_t cid, n;
        p = parse_int_s1(p, cid);
        p = parse_int_s1(p, n);

        if (g_use_worker) {
            // Locate A's end with a SIMD newline scan, hand the rest to
            // the worker (which parses B into transposed layout), then
            // parse A on the main thread.
            const char* a_end = find_newline(p);
            worker_p = a_end + 1;
            work_state.store(1, std::memory_order_release);
            for (int i = 0; i < N*N; ++i) p = parse_int_s1(p, A[i]);
            while (work_state.load(std::memory_order_acquire) != 2) _mm_pause();
            work_state.store(0, std::memory_order_relaxed);
        } else {
            // Serial fallback: single-core or no usable second CPU.
            for (int i = 0; i < N*N; ++i) p = parse_int_s1(p, A[i]);
            ++p;  // skip A's trailing '\n'
            for (int k = 0; k < N; ++k) {
                for (int i = 0; i < N; ++i) {
                    int32_t v;
                    p = parse_int_s1(p, v);
                    B[i*N + k] = v;
                }
            }
        }

        int64_t tr = hft::trace_AB_T(A, B);
        int ans = (int)((tr % MOD + MOD) % MOD);

        int len = snprintf(ansbuf, sizeof(ansbuf), "%d", ans);
        send(sock, ansbuf, len, 0);
    }

    close(sock);
    if (g_use_worker) {
        worker_exit.store(true, std::memory_order_release);
        work_state.store(1, std::memory_order_release);
        if (worker_thread.joinable()) worker_thread.join();
    }
    return 0;
}
