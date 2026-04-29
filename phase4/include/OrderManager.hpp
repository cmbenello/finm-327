#pragma once
#include <cstdint>
#include <memory>
#include <unordered_map>

#include "Order.hpp"

// tracks every order we've placed by id. shared_ptr so the book can hold
// the same instance -- when the matching engine fills an order, the
// state field updates here too.
//
// no allocator policy: order *creation* runs once per submission. the
// hot path is matching, not OMS bookkeeping.
class OrderManager {
public:
    using OrderPtr = std::shared_ptr<OrderD>;

    OrderPtr place(std::string symbol, double price, int qty, Side s) {
        auto id = next_id_++;
        auto o  = std::make_shared<OrderD>(id, std::move(symbol), price, qty, s);
        active_[id] = o;
        return o;
    }

    bool cancel(std::int64_t id) {
        auto it = active_.find(id);
        if (it == active_.end()) return false;
        if (!it->second->live()) return false;
        it->second->state = OrdState::Cancelled;
        return true;
    }

    OrderPtr get(std::int64_t id) const {
        auto it = active_.find(id);
        return it == active_.end() ? nullptr : it->second;
    }

    // remove fully-done orders from the working set so the map doesn't grow
    // unbounded. caller decides when to call this.
    std::size_t reap() {
        std::size_t n = 0;
        for (auto it = active_.begin(); it != active_.end(); ) {
            if (!it->second->live()) { it = active_.erase(it); ++n; }
            else ++it;
        }
        return n;
    }

    std::size_t size() const { return active_.size(); }

private:
    std::unordered_map<std::int64_t, OrderPtr> active_;
    std::int64_t next_id_ = 1;
};
