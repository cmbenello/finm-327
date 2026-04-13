#include <iostream>
#include <string>
#include <deque>
#include <thread>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

using namespace std;

void receiveAndRespond(int socketFd, const string& name) {
    char buf[256];
    deque<float> priceHistory;
    int ordersHit = 0;
    int pricesReceived = 0;

    while (true) {
        int n = recv(socketFd, buf, sizeof(buf) - 1, 0);
        if (n <= 0) {
            cout << name << " disconnected from server." << endl;
            break;
        }
        buf[n] = '\0';
        string msg(buf);

        // parse "price_id,price"
        size_t comma = msg.find(',');
        if (comma == string::npos) continue;

        int priceId = stoi(msg.substr(0, comma));
        float currentPrice = stof(msg.substr(comma + 1));
        pricesReceived++;

        cout << name << " received price_id=" << priceId
             << " price=" << currentPrice << endl;

        // keep the last 3 prices
        if (priceHistory.size() >= 3)
            priceHistory.pop_front();
        priceHistory.push_back(currentPrice);

        // need at least 3 data points to check momentum
        if (priceHistory.size() == 3) {
            float a = priceHistory[0];
            float b = priceHistory[1];
            float c = priceHistory[2];

            bool up = (a < b) && (b < c);
            bool down = (a > b) && (b > c);

            if (up || down) {
                // simulate reaction delay
                this_thread::sleep_for(chrono::milliseconds(10 + rand() % 50));

                string order = to_string(priceId);
                send(socketFd, order.c_str(), order.size(), 0);
                ordersHit++;

                string direction = up ? "UP" : "DOWN";
                cout << name << " -> Momentum " << direction
                     << "! Sent order for price_id=" << priceId
                     << " (prices: " << a << ", " << b << ", " << c << ")"
                     << endl;
            } else {
                cout << name << " -> No momentum. Skipping price_id=" << priceId << endl;
            }
        } else {
            cout << name << " -> Waiting for more data (" << priceHistory.size()
                 << "/3 prices collected)" << endl;
        }
    }

    cout << "\n--- " << name << " Stats ---" << endl;
    cout << "Prices received: " << pricesReceived << endl;
    cout << "Orders sent: " << ordersHit << endl;
}

int main() {
    srand(time(nullptr));

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        cerr << "Failed to create socket" << endl;
        return 1;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(12345);
    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);

    cout << "Connecting to server on port 12345..." << endl;
    if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        cerr << "Connection failed. Is the server running?" << endl;
        return 1;
    }
    cout << "Connected!" << endl;

    string clientName = "Client-1";
    receiveAndRespond(sock, clientName);

    close(sock);
    return 0;
}
