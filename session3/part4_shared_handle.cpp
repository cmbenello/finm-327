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

class SharedTradeHandle {
    struct ControlBlock {
        Trade* ptr;
        int count;
    };

    ControlBlock* cb;

    void release() {
        if (!cb) return;
        if (--cb->count == 0) {
            delete cb->ptr;
            delete cb;
        }
        cb = nullptr;
    }

public:
    explicit SharedTradeHandle(Trade* p) : cb(new ControlBlock{p, 1}) {}

    ~SharedTradeHandle() { release(); }

    SharedTradeHandle(const SharedTradeHandle& other) : cb(other.cb) {
        if (cb) ++cb->count;
    }

    SharedTradeHandle& operator=(const SharedTradeHandle& other) {
        if (this != &other) {
            release();
            cb = other.cb;
            if (cb) ++cb->count;
        }
        return *this;
    }

    SharedTradeHandle(SharedTradeHandle&& other) noexcept : cb(other.cb) {
        other.cb = nullptr;
    }

    SharedTradeHandle& operator=(SharedTradeHandle&& other) noexcept {
        if (this != &other) {
            release();
            cb = other.cb;
            other.cb = nullptr;
        }
        return *this;
    }

    Trade* operator->() { return cb ? cb->ptr : nullptr; }
    Trade& operator*() { return *cb->ptr; }
    int use_count() const { return cb ? cb->count : 0; }
};

int main() {
    SharedTradeHandle a{new Trade("AAPL", 150.0)};
    std::cout << "after a: count=" << a.use_count() << "\n";

    {
        SharedTradeHandle b = a;
        SharedTradeHandle c = a;
        std::cout << "after b,c: count=" << a.use_count() << "\n";
        std::cout << "b sees " << b->symbol << "\n";
    } // b and c die, count drops back to 1

    std::cout << "after inner scope: count=" << a.use_count() << "\n";

    SharedTradeHandle d{new Trade("GOOG", 2800.0)};
    d = a; // GOOG released here (last owner), d now shares AAPL
    std::cout << "after d=a: count=" << a.use_count() << "\n";

    return 0;
}
