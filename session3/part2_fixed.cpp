#include <iostream>
#include <string>

struct Trade {
    std::string symbol;
    double price;

    Trade(const std::string& s, double p) : symbol(s), price(p) {
        std::cout << "Trade created: " << symbol << "\n";
    }

    ~Trade() {
        std::cout << "Trade destroyed: " << symbol << "\n";
    }
};

int main() {
    Trade* t1 = new Trade("AAPL", 150.0);
    Trade* t2 = new Trade("GOOG", 2800.0);
    delete t1;
    // FIX: removed second `delete t1`. Calling delete twice on the same pointer
    // corrupts the allocator's internal state and is undefined behavior.
    delete t2; // FIX: t2 was never freed — leak.

    Trade* t3 = new Trade("MSFT", 300.0);
    // FIX: the original reassigned t3 to a new allocation without freeing the
    // old one — MSFT's memory was leaked. Free first, then allocate.
    delete t3;
    t3 = new Trade("TSLA", 750.0);
    delete t3;

    Trade* trades = new Trade[3]{
        {"NVDA", 900.0},
        {"AMZN", 3200.0},
        {"META", 250.0}
    };
    // FIX: was `delete trades` — mismatched with `new[]`. Only the first
    // element's destructor runs and the allocator gets the wrong size.
    delete[] trades;

    return 0;
}
