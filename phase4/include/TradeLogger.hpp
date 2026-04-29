#pragma once
#include <fstream>
#include <string>
#include <vector>

#include "Order.hpp"

// buffered append-only trade log. add() drops a record into the in-memory
// vector; flush() writes everything pending to disk in one go. the vector
// is reserved up front so steady-state add() should be amortized O(1) with
// no realloc.
class TradeLogger {
public:
    explicit TradeLogger(std::size_t reserve = 1 << 16) {
        buf_.reserve(reserve);
    }

    void add(const Trade& t) { buf_.push_back(t); }

    std::size_t pending() const { return buf_.size(); }
    const std::vector<Trade>& records() const { return buf_; }

    // RAII handle around an ofstream. flushes on destruction so we never
    // forget. caller picks the path.
    void flush(const std::string& path) {
        Dump d(path);
        for (const auto& t : buf_) {
            d.os << t.seq << ',' << t.symbol << ',' << t.price << ','
                 << t.quantity << ',' << t.buy_id << ',' << t.sell_id << ','
                 << t.ns << '\n';
        }
        // keep the in-memory tail for stats; caller can clear() if needed.
    }

    void clear() { buf_.clear(); }

private:
    struct Dump {
        std::ofstream os;
        explicit Dump(const std::string& path) : os(path) {}
        ~Dump() { os.flush(); }   // ofstream dtor closes; explicit flush before
    };

    std::vector<Trade> buf_;
};
