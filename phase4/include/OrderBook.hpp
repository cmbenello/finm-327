#pragma once
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <vector>

#include "Order.hpp"

// price-time priority book. two sides:
//   bids -- map<price, queue> in descending order, best bid is begin()
//   asks -- map<price, queue> in ascending order,  best ask is begin()
//
// orders live as shared_ptr because OrderManager also holds a copy --
// the book and the OMS need to see the same state object.
//
// templated on price/id types. multimap was suggested in the spec but
// map<price, deque<id>> is friendlier for fifo at the same level.
template <typename PriceT = double, typename OrderIdT = std::int64_t>
class OrderBook {
public:
    using OrderT = Order<PriceT, OrderIdT>;
    using OrderPtr = std::shared_ptr<OrderT>;
    using BidMap = std::map<PriceT, std::deque<OrderPtr>, std::greater<PriceT>>;
    using AskMap = std::map<PriceT, std::deque<OrderPtr>, std::less<PriceT>>;

    void add(OrderPtr o) {
        if (o->side == Side::Buy)  bids_[o->price].push_back(std::move(o));
        else                       asks_[o->price].push_back(std::move(o));
    }

    // remove first dead order at the best price level if any.
    void prune_front(Side s) {
        if (s == Side::Buy) prune_front_impl(bids_);
        else                prune_front_impl(asks_);
    }

    bool empty(Side s) const {
        return s == Side::Buy ? bids_.empty() : asks_.empty();
    }

    OrderPtr best(Side s) {
        if (s == Side::Buy) {
            if (bids_.empty()) return nullptr;
            return bids_.begin()->second.front();
        }
        if (asks_.empty()) return nullptr;
        return asks_.begin()->second.front();
    }

    // pop the front order of the best price level. used after a fill consumes it.
    void pop_best(Side s) {
        if (s == Side::Buy)  pop_best_impl(bids_);
        else                 pop_best_impl(asks_);
    }

    std::size_t depth(Side s) const {
        return s == Side::Buy ? bids_.size() : asks_.size();
    }

    BidMap& bids() { return bids_; }
    AskMap& asks() { return asks_; }

private:
    template <typename M>
    void prune_front_impl(M& m) {
        while (!m.empty()) {
            auto& q = m.begin()->second;
            while (!q.empty() && !q.front()->live()) q.pop_front();
            if (q.empty()) m.erase(m.begin()); else break;
        }
    }
    template <typename M>
    void pop_best_impl(M& m) {
        if (m.empty()) return;
        auto& q = m.begin()->second;
        q.pop_front();
        if (q.empty()) m.erase(m.begin());
    }

    BidMap bids_{};
    AskMap asks_{};
};

// flat-array variant: price levels in a sorted vector. cheaper traversal,
// expensive insert in the middle. keep both around for the bench.
template <typename PriceT = double, typename OrderIdT = std::int64_t>
class FlatOrderBook {
public:
    using OrderT = Order<PriceT, OrderIdT>;
    using OrderPtr = std::shared_ptr<OrderT>;

    struct Level {
        PriceT price;
        std::deque<OrderPtr> q;
    };

    void add(OrderPtr o) {
        auto& side = (o->side == Side::Buy) ? bids_ : asks_;
        // bids descending, asks ascending. linear search is fine for
        // shallow books -- the map version exists for the deep case.
        if (o->side == Side::Buy) {
            auto it = side.begin();
            while (it != side.end() && it->price > o->price) ++it;
            if (it != side.end() && it->price == o->price) it->q.push_back(o);
            else side.insert(it, Level{o->price, std::deque<OrderPtr>{std::move(o)}});
        } else {
            auto it = side.begin();
            while (it != side.end() && it->price < o->price) ++it;
            if (it != side.end() && it->price == o->price) it->q.push_back(o);
            else side.insert(it, Level{o->price, std::deque<OrderPtr>{std::move(o)}});
        }
    }

    bool empty(Side s) const { return side(s).empty(); }

    OrderPtr best(Side s) {
        auto& sd = side(s);
        if (sd.empty()) return nullptr;
        return sd.front().q.front();
    }

    void pop_best(Side s) {
        auto& sd = side(s);
        if (sd.empty()) return;
        sd.front().q.pop_front();
        if (sd.front().q.empty()) sd.erase(sd.begin());
    }

    void prune_front(Side s) {
        auto& sd = side(s);
        while (!sd.empty()) {
            auto& q = sd.front().q;
            while (!q.empty() && !q.front()->live()) q.pop_front();
            if (q.empty()) sd.erase(sd.begin()); else break;
        }
    }

    std::size_t depth(Side s) const { return side(s).size(); }

private:
    std::vector<Level>& side(Side s)             { return s == Side::Buy ? bids_ : asks_; }
    const std::vector<Level>& side(Side s) const { return s == Side::Buy ? bids_ : asks_; }

    std::vector<Level> bids_;
    std::vector<Level> asks_;
};
