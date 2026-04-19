#include <iostream>
#include <string>

struct Trade {
    std::string symbol;
    double price;

    Trade(const std::string& sym, double p) : symbol(sym), price(p) {}
};

int main() {
    Trade* single = new Trade("AAPL", 150.0);
    std::cout << "single: " << single->symbol << " @ " << single->price << "\n";
    delete single;

    Trade* arr = new Trade[5]{
        {"AAPL", 150.0},
        {"GOOG", 2800.0},
        {"MSFT", 300.0},
        {"NVDA", 900.0},
        {"AMZN", 3200.0}
    };
    for (int i = 0; i < 5; ++i) {
        std::cout << "arr[" << i << "]: " << arr[i].symbol << " @ " << arr[i].price << "\n";
    }
    delete[] arr;

    // Q1: delete on an array (not delete[]) only destroys the first element and
    // hands a mismatched size back to the allocator — undefined behavior.
    // Q2: forgetting delete leaks memory; the OS reclaims at exit but long-running
    // programs accumulate leaks until they OOM.
    // Q3: double-delete on the same pointer corrupts the allocator's free list.
    // Crashed immediately on my machine; on others it silently frees an unrelated
    // allocation later (much harder to debug).

    return 0;
}
