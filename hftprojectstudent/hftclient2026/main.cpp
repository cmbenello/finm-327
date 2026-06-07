#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <unistd.h>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#if defined(__linux__)
  #include <sys/mman.h>
  #include <sched.h>
#endif

static constexpr int    N      = 128;
static constexpr int    MOD    = 997;
static constexpr size_t BUFCAP = 1u << 22;   // 4 MiB: buffers several queued challenges under blast

static char buf[BUFCAP];

static inline void cpu_relax() {
#if defined(__x86_64__) || defined(__i386__)
    __builtin_ia32_pause();
#elif defined(__aarch64__) || defined(__arm__)
    __asm__ __volatile__("yield" ::: "memory");
#else
    __asm__ __volatile__("" ::: "memory");
#endif
}

// Matrix values are in [0,996]: 1-3 digits, each followed by exactly one
// separator byte (' ' between values, '\n' after the row). All three byte
// loads issue up front so they overlap, then we branch on the digit count.
static inline const char* parse_val(const char* p, int& out) {
    unsigned d0 = (unsigned char)p[0] - '0';
    unsigned d1 = (unsigned char)p[1] - '0';
    unsigned d2 = (unsigned char)p[2] - '0';
    if (d1 >= 10u) { out = (int)d0;            return p + 2; }
    if (d2 >= 10u) { out = (int)(d0*10 + d1);  return p + 3; }
    out = (int)(d0*100 + d1*10 + d2);          return p + 4;
}

// Bounded busy-poll: catch the in-flight payload with no syscall wakeup, but
// fall back to a blocking recv during the long idle gap so we don't spin a
// core at 100% between challenges.
static ssize_t recv_some(int fd, char* dst, size_t cap) {
    for (int spin = 0; spin < (1 << 20); ++spin) {
        ssize_t r = recv(fd, dst, cap, MSG_DONTWAIT);
        if (r > 0) return r;
        if (r == 0) return 0;
        if (errno != EAGAIN && errno != EWOULDBLOCK) return -1;
        cpu_relax();
    }
    return recv(fd, dst, cap, 0);
}

int main(int argc, char** argv) {
    const char* host = argc > 1 ? argv[1] : "127.0.0.1";
    int         port = argc > 2 ? atoi(argv[2]) : 12345;
    const char* team = argc > 3 ? argv[3] : "group8";

#if defined(__linux__)
    mlockall(MCL_CURRENT | MCL_FUTURE);
    sched_param sp{};
    sp.sched_priority = 80;
    sched_setscheduler(0, SCHED_FIFO, &sp);   // best-effort, needs privilege
#endif

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) { perror("socket"); return 1; }

    int one = 1;
    setsockopt(sock, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
    int rcv = 1 << 20;
    setsockopt(sock, SOL_SOCKET, SO_RCVBUF, &rcv, sizeof(rcv));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        fprintf(stderr, "bad host: %s\n", host);
        return 1;
    }
    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("connect"); return 1; }

    std::string intro = std::string(team) + "\n";
    send(sock, intro.data(), intro.size(), 0);
    printf("connected to %s:%d as %s\n", host, port, team);
    fflush(stdout);

    size_t have = 0;
    long   solved = 0;
    while (true) {
        // A full challenge is four '\n'-terminated lines: cid, N, A, B.
        int    nl  = 0;
        size_t end = 0;
        for (size_t i = 0; i < have; ++i)
            if (buf[i] == '\n' && ++nl == 4) { end = i + 1; break; }

        if (nl < 4) {
            if (have == BUFCAP) { fprintf(stderr, "payload overflow\n"); break; }
            ssize_t r = recv_some(sock, buf + have, BUFCAP - have);
            if (r <= 0) { printf("disconnected\n"); break; }
            have += (size_t)r;
            continue;
        }

        const char* p = buf;

        long cid = 0;
        while (*p >= '0' && *p <= '9') cid = cid * 10 + (*p++ - '0');
        ++p;                                   // newline after cid
        while (*p >= '0' && *p <= '9') ++p;    // N (always 128)
        ++p;                                   // newline after N

        // answer = sum of all entries of A*B (mod 997)
        //        = sum_k (sum_i A[i][k]) * (sum_j B[k][j])
        // colA[k] accumulates column sums of A; B's row sums fold in inline.
        int colA[N];
        for (int k = 0; k < N; ++k) colA[k] = 0;
        for (int i = 0; i < N; ++i)
            for (int k = 0; k < N; ++k) {
                int v;
                p = parse_val(p, v);
                colA[k] += v;
            }
        ++p;                                   // newline ending A's line

        long long checksum = 0;
        for (int k = 0; k < N; ++k) {
            int s = 0;
            for (int j = 0; j < N; ++j) {
                int v;
                p = parse_val(p, v);
                s += v;
            }
            checksum += (long long)colA[k] * s;
        }
        int ans = (int)(checksum % MOD);

        char outb[48];
        int  len = snprintf(outb, sizeof(outb), "%ld %d\n", cid, ans);
        send(sock, outb, (size_t)len, 0);
        ++solved;

        memmove(buf, buf + end, have - end);   // retain queued bytes (blast-safe)
        have -= end;
    }

    printf("solved %ld challenges\n", solved);
    close(sock);
    return 0;
}
