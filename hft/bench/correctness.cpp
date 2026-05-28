#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>
#include <sstream>
#include <vector>
#include <string>
#include "../src/trace.hpp"

using hft::N;
using hft::MOD;

alignas(64) static int32_t A_fast[N*N];
alignas(64) static int32_t B_fast[N*N];

static std::string make_payload(uint32_t seed, int cid) {
    std::mt19937 rng(seed);
    std::string s;
    char num[16];
    int len = snprintf(num, sizeof(num), "%d\n%d\n", cid, N);
    s.append(num, len);
    for (int i = 0; i < N*N; ++i) {
        len = snprintf(num, sizeof(num), "%d ", (int)(rng() % MOD));
        s.append(num, len);
    }
    s.push_back('\n');
    for (int i = 0; i < N*N; ++i) {
        len = snprintf(num, sizeof(num), "%d ", (int)(rng() % MOD));
        s.append(num, len);
    }
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

int main() {
    int fails = 0;
    for (int t = 0; t < 25; ++t) {
        std::string payload = make_payload(0xCAFE + t, t);

        const char* p = payload.data();
        int32_t cid_f, n_f;
        p = parse_int(p, cid_f);
        p = parse_int(p, n_f);
        for (int i = 0; i < N*N; ++i) p = parse_int(p, A_fast[i]);
        for (int i = 0; i < N*N; ++i) p = parse_int(p, B_fast[i]);
        int64_t tr_fast = hft::trace_AB(A_fast, B_fast);
        int ans_fast = (int)((tr_fast % MOD + MOD) % MOD);

        std::istringstream iss(payload);
        int cid_r, n_r;
        iss >> cid_r >> n_r;
        std::vector<std::vector<int>> A(N, std::vector<int>(N));
        std::vector<std::vector<int>> B(N, std::vector<int>(N));
        for (int i = 0; i < N; ++i) for (int j = 0; j < N; ++j) iss >> A[i][j];
        for (int i = 0; i < N; ++i) for (int j = 0; j < N; ++j) iss >> B[i][j];
        long long tr_ref = 0;
        for (int i = 0; i < N; ++i)
            for (int k = 0; k < N; ++k)
                tr_ref += (long long)A[i][k] * B[k][i];
        int ans_ref = (int)((tr_ref % MOD + MOD) % MOD);

        int parse_mismatches = 0;
        for (int i = 0; i < N*N; ++i) if (A_fast[i] != A[i/N][i%N]) ++parse_mismatches;
        for (int i = 0; i < N*N; ++i) if (B_fast[i] != B[i/N][i%N]) ++parse_mismatches;

        bool ok = (cid_f == cid_r) && (n_f == n_r) && (ans_fast == ans_ref) && (parse_mismatches == 0);
        printf("test %2d: cid=%d n=%d  fast_ans=%d ref_ans=%d  parse_mismatches=%d  %s\n",
               t, cid_f, n_f, ans_fast, ans_ref, parse_mismatches, ok ? "OK" : "FAIL");
        if (!ok) ++fails;
    }
    printf("\n%s: %d failures across 25 tests\n", fails == 0 ? "PASS" : "FAIL", fails);
    return fails == 0 ? 0 : 1;
}
