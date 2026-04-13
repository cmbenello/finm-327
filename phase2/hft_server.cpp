#include <iostream>
#include <string>
#include <vector>
#include <thread>
#include <mutex>
#include <random>
#include <cstring>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

using namespace std;

mutex clientsMtx;
vector<int> clientSockets;

void handleClient(int clientSock) {
    char buf[256];
    while (true) {
        int n = recv(clientSock, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            lock_guard<mutex> lock(clientsMtx);
            for (auto it = clientSockets.begin(); it != clientSockets.end(); ++it) {
                if (*it == clientSock) {
                    clientSockets.erase(it);
                    break;
                }
            }
            close(clientSock);
            cout << "[Server] Client disconnected." << endl;
            return;
        }
        buf[n] = '\0';
        string response(buf);
        cout << "[Server] Got order from client: price_id=" << response << endl;
    }
}

int main() {
    int serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (serverFd < 0) {
        cerr << "Failed to create socket" << endl;
        return 1;
    }

    int opt = 1;
    setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(12345);

    if (bind(serverFd, (sockaddr*)&addr, sizeof(addr)) < 0) {
        cerr << "Bind failed" << endl;
        return 1;
    }

    listen(serverFd, 5);
    cout << "[Server] Listening on port 12345..." << endl;

    // accept clients in a background thread
    thread acceptThread([&]() {
        while (true) {
            sockaddr_in clientAddr{};
            socklen_t len = sizeof(clientAddr);
            int clientSock = accept(serverFd, (sockaddr*)&clientAddr, &len);
            if (clientSock < 0) continue;

            cout << "[Server] New client connected." << endl;
            {
                lock_guard<mutex> lock(clientsMtx);
                clientSockets.push_back(clientSock);
            }
            thread(handleClient, clientSock).detach();
        }
    });
    acceptThread.detach();

    // price generation loop
    random_device rd;
    mt19937 gen(rd());
    uniform_real_distribution<float> priceDist(100.0f, 110.0f);

    int priceId = 0;
    while (true) {
        this_thread::sleep_for(chrono::seconds(5));

        float price = priceDist(gen);
        priceId++;
        string msg = to_string(priceId) + "," + to_string(price);

        cout << "[Server] Broadcasting price_id=" << priceId
             << " price=" << price << endl;

        lock_guard<mutex> lock(clientsMtx);
        for (int sock : clientSockets) {
            send(sock, msg.c_str(), msg.size(), 0);
        }
    }

    close(serverFd);
    return 0;
}
