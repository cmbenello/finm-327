#include <iostream>
#include <thread>
#include <chrono>
#include <cstring>
#include <cstdlib>
#include <deque>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>

using namespace std;

#define SERVER_IP "127.0.0.1"
#define SERVER_PORT 12345
#define BUFFER_SIZE 1024

void receiveAndRespond(int socketFd, const string& name) {
    char buffer[BUFFER_SIZE];

    // Send client name
    send(socketFd, name.c_str(), name.size(), 0);

    deque<float> priceHistory;
    int ordersHit = 0;
    int pricesReceived = 0;

    while (true) {
        memset(buffer, 0, BUFFER_SIZE);
        int bytesReceived = recv(socketFd, buffer, BUFFER_SIZE - 1, 0);
        if (bytesReceived <= 0) {
            cerr << "Server closed connection or error occurred." << endl;
            break;
        }

        string data(buffer);
        size_t commaPos = data.find(',');
        if (commaPos == string::npos) {
            cerr << "Invalid price format received: " << data << endl;
            continue;
        }

        int priceId = stoi(data.substr(0, commaPos));
        float price = stof(data.substr(commaPos + 1));
        pricesReceived++;

        cout << "📥 Received price ID: " << priceId << ", Value: " << price << endl;

        // keep the last 3 prices for momentum check
        if (priceHistory.size() >= 3)
            priceHistory.pop_front();
        priceHistory.push_back(price);

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
                send(socketFd, order.c_str(), order.length(), 0);
                ordersHit++;

                string direction = up ? "UP" : "DOWN";
                cout << "📤 Momentum " << direction << "! Sent order for price ID: " << priceId
                     << " (prices: " << a << ", " << b << ", " << c << ")" << endl;
            } else {
                cout << "⏸️ No momentum. Skipping price ID: " << priceId << endl;
            }
        } else {
            cout << "⏳ Waiting for more data (" << priceHistory.size()
                 << "/3 prices collected)" << endl;
        }
    }

    cout << "\n--- " << name << " Stats ---" << endl;
    cout << "Prices received: " << pricesReceived << endl;
    cout << "Orders sent: " << ordersHit << endl;

    close(socketFd);
}

int main() {
    srand(time(nullptr));

    string name;
    cout << "Enter your client name: ";
    getline(cin, name);

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        cerr << "Socket creation failed!" << endl;
        return 1;
    }

    sockaddr_in serverAddr{};
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(SERVER_PORT);
    inet_pton(AF_INET, SERVER_IP, &serverAddr.sin_addr);

    if (connect(sock, (sockaddr*)&serverAddr, sizeof(serverAddr)) < 0) {
        cerr << "Connection to server failed!" << endl;
        return 1;
    }

    cout << "✅ Connected to server at " << SERVER_IP << ":" << SERVER_PORT << endl;
    receiveAndRespond(sock, name);
    return 0;
}
