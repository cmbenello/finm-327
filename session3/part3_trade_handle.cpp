#include <iostream>
#include <string>
#include <utility>

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

class TradeHandle {
    Trade* ptr;

public:
    explicit TradeHandle(Trade* p) : ptr(p) {}

    ~TradeHandle() { delete ptr; }

    TradeHandle(const TradeHandle&) = delete;
    TradeHandle& operator=(const TradeHandle&) = delete;

    TradeHandle(TradeHandle&& other) noexcept : ptr(other.ptr) {
        other.ptr = nullptr;
    }

    TradeHandle& operator=(TradeHandle&& other) noexcept {
        if (this != &other) {
            delete ptr;
            ptr = other.ptr;
            other.ptr = nullptr;
        }
        return *this;
    }

    Trade* operator->() { return ptr; }
    Trade& operator*() { return *ptr; }
};

void inspect(TradeHandle h) {
    std::cout << "inspecting " << h->symbol << " @ " << h->price << "\n";
}

int main() {
    {
        TradeHandle h{new Trade("AAPL", 150.0)};
        std::cout << "using " << h->symbol << "\n";
    } // AAPL destroyed here automatically

    {
        TradeHandle a{new Trade("GOOG", 2800.0)};
        TradeHandle b = std::move(a); // ownership transfers, a is now empty
        std::cout << "b holds " << b->symbol << "\n";

        TradeHandle c{new Trade("MSFT", 300.0)};
        c = std::move(b); // old MSFT gets deleted, c takes GOOG
        std::cout << "c holds " << c->symbol << "\n";
    }

    inspect(TradeHandle{new Trade("NVDA", 900.0)}); // moved into the arg, dies at return

    // Q1: copy constructor is deleted because copying would mean two handles
    // owning the same pointer — whichever destructs second double-frees.
    // Q2: moves are safe: the source is nulled out so only one handle ever
    // owns the resource at a time.
    // Q3: without a destructor, the pointer leaks when the handle leaves scope —
    // which defeats the entire point of the wrapper.

    return 0;
}
