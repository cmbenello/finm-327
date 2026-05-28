#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

static constexpr int N = 128;
static constexpr int MOD = 997;

int main(int argc, char** argv) {
    const char* host = (argc > 1) ? argv[1] : "127.0.0.1";
    int port = (argc > 2) ? atoi(argv[2]) : 12345;
    const char* name = (argc > 3) ? argv[3] : "naive";

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, host, &addr.sin_addr);
    if (connect(sock, (sockaddr*)&addr, sizeof(addr)) < 0) { perror("connect"); return 1; }
    send(sock, name, strlen(name), 0);

    std::string buf;
    buf.reserve(1 << 19);
    char chunk[65536];

    while (true) {
        buf.clear();
        int newlines = 0;
        while (newlines < 4) {
            ssize_t r = recv(sock, chunk, sizeof(chunk), 0);
            if (r <= 0) { close(sock); return 0; }
            for (ssize_t i = 0; i < r; ++i) if (chunk[i] == '\n') ++newlines;
            buf.append(chunk, r);
        }

        std::istringstream iss(buf);
        int cid, n;
        iss >> cid >> n;
        std::vector<std::vector<int>> A(N, std::vector<int>(N));
        std::vector<std::vector<int>> B(N, std::vector<int>(N));
        for (int i = 0; i < N; ++i) for (int j = 0; j < N; ++j) iss >> A[i][j];
        for (int i = 0; i < N; ++i) for (int j = 0; j < N; ++j) iss >> B[i][j];

        std::vector<std::vector<long long>> C(N, std::vector<long long>(N, 0));
        for (int i = 0; i < N; ++i)
            for (int k = 0; k < N; ++k)
                for (int j = 0; j < N; ++j)
                    C[i][j] += (long long)A[i][k] * B[k][j];

        long long tr = 0;
        for (int i = 0; i < N; ++i) tr += C[i][i];
        int ans = (int)((tr % MOD + MOD) % MOD);

        char out[32];
        int len = snprintf(out, sizeof(out), "%d", ans);
        send(sock, out, len, 0);
    }
}
