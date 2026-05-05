// Part 4: Order book — fast id lookup (unordered_map) + ordered price levels (map).
// Compared against an unordered-only book where best bid/offer is a linear scan.

#include <chrono>
#include <cstdio>
#include <cstdint>
#include <map>
#include <random>
#include <unordered_map>
#include <vector>

struct Order {
    uint32_t id;
    double   price;
    uint32_t qty;
    char     side; // 'B' or 'S'
};

class OrderBook {
    std::unordered_map<uint32_t, Order> by_id_;
    // bids ordered descending so begin() is the best bid; asks ascending so begin() is best ask.
    std::map<double, std::vector<uint32_t>, std::greater<double>> bids_;
    std::map<double, std::vector<uint32_t>>                       asks_;

    template <typename Map>
    static void erase_id_at(Map& m, double price, uint32_t id) {
        auto it = m.find(price);
        if (it == m.end()) return;
        auto& v = it->second;
        for (size_t i = 0; i < v.size(); ++i) {
            if (v[i] == id) { v[i] = v.back(); v.pop_back(); break; }
        }
        if (v.empty()) m.erase(it);
    }

public:
    void add(uint32_t id, double price, uint32_t qty, char side) {
        by_id_.emplace(id, Order{id, price, qty, side});
        if (side == 'B') bids_[price].push_back(id);
        else             asks_[price].push_back(id);
    }
    bool modify(uint32_t id, uint32_t new_qty) {
        auto it = by_id_.find(id);
        if (it == by_id_.end()) return false;
        it->second.qty = new_qty;
        return true;
    }
    bool cancel(uint32_t id) {
        auto it = by_id_.find(id);
        if (it == by_id_.end()) return false;
        if (it->second.side == 'B') erase_id_at(bids_, it->second.price, id);
        else                        erase_id_at(asks_, it->second.price, id);
        by_id_.erase(it);
        return true;
    }
    double best_bid() const { return bids_.empty() ? 0.0 : bids_.begin()->first; }
    double best_ask() const { return asks_.empty() ? 0.0 : asks_.begin()->first; }
    const std::vector<uint32_t>* orders_at(double price, char side) const {
        if (side == 'B') { auto it = bids_.find(price); return it == bids_.end() ? nullptr : &it->second; }
        auto it = asks_.find(price); return it == asks_.end() ? nullptr : &it->second;
    }
    size_t size() const { return by_id_.size(); }
};

// Comparison book: only an unordered_map; best bid/offer requires a linear scan.
class FlatBook {
    std::unordered_map<uint32_t, Order> by_id_;
public:
    void add(uint32_t id, double price, uint32_t qty, char side) {
        by_id_.emplace(id, Order{id, price, qty, side});
    }
    bool modify(uint32_t id, uint32_t new_qty) {
        auto it = by_id_.find(id);
        if (it == by_id_.end()) return false;
        it->second.qty = new_qty;
        return true;
    }
    bool cancel(uint32_t id) { return by_id_.erase(id) > 0; }
    double best_bid() const {
        double best = 0.0;
        for (auto& kv : by_id_) if (kv.second.side == 'B' && kv.second.price > best) best = kv.second.price;
        return best;
    }
    double best_ask() const {
        double best = 0.0;
        for (auto& kv : by_id_) if (kv.second.side == 'S' && (best == 0.0 || kv.second.price < best)) best = kv.second.price;
        return best;
    }
};

template <typename F>
double time_ms(F&& f) {
    auto t0 = std::chrono::steady_clock::now();
    f();
    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

int main() {
    constexpr size_t N = 200'000;        // resting orders
    constexpr size_t QUERIES = 500'000;  // BBO queries on the OrderBook
    constexpr size_t FLAT_QUERIES = 200; // FlatBook is O(N) per query — keep it small

    std::mt19937_64 rng(99);
    std::uniform_int_distribution<int> px_ticks(0, 999);    // 0.01-spaced grid
    std::uniform_int_distribution<int> coin(0, 1);
    std::uniform_int_distribution<uint32_t> qty(1, 1000);

    std::vector<Order> input;
    input.reserve(N);
    for (size_t i = 0; i < N; ++i) {
        char side = coin(rng) ? 'B' : 'S';
        double price = (side == 'B' ? 100.0 : 101.0) + 0.01 * px_ticks(rng);
        input.push_back(Order{static_cast<uint32_t>(i), price, qty(rng), side});
    }

    OrderBook ob;
    FlatBook  fb;

    double ob_add = time_ms([&] { for (auto& o : input) ob.add(o.id, o.price, o.qty, o.side); });
    double fb_add = time_ms([&] { for (auto& o : input) fb.add(o.id, o.price, o.qty, o.side); });

    // Random modifies on existing IDs
    std::vector<uint32_t> ids(N);
    for (size_t i = 0; i < N; ++i) ids[i] = static_cast<uint32_t>(i);
    std::shuffle(ids.begin(), ids.end(), rng);

    double ob_mod = time_ms([&] { for (auto id : ids) ob.modify(id, 42); });
    double fb_mod = time_ms([&] { for (auto id : ids) fb.modify(id, 42); });

    // Best bid/offer queries dominate quote dissemination — measure many of them.
    volatile double sink = 0;
    double ob_bbo = time_ms([&] {
        for (size_t i = 0; i < QUERIES; ++i) sink += ob.best_bid() - ob.best_ask();
    });
    double fb_bbo_small = time_ms([&] {
        for (size_t i = 0; i < FLAT_QUERIES; ++i) sink += fb.best_bid() - fb.best_ask();
    });
    // Project FlatBook BBO cost up to the same query count for an apples-to-apples table.
    double fb_bbo = fb_bbo_small * (static_cast<double>(QUERIES) / FLAT_QUERIES);

    // Cancel half
    double ob_cancel = time_ms([&] {
        for (size_t i = 0; i < N; i += 2) ob.cancel(ids[i]);
    });
    double fb_cancel = time_ms([&] {
        for (size_t i = 0; i < N; i += 2) fb.cancel(ids[i]);
    });

    std::printf("=== Part 4: Order Book (N=%zu orders, %zu BBO queries) ===\n", N, QUERIES);
    std::printf("%-22s %10s %10s %12s %10s\n", "structure", "add(ms)", "mod(ms)", "BBOx500k(ms)", "cancel(ms)");
    std::printf("%-22s %10.2f %10.2f %12.2f %10.2f\n", "OrderBook(uo+map)", ob_add, ob_mod, ob_bbo, ob_cancel);
    std::printf("%-22s %10.2f %10.2f %12.2f %10.2f\n", "FlatBook(uo only)", fb_add, fb_mod, fb_bbo, fb_cancel);
    std::printf("(FlatBook BBO timed on %zu queries, scaled to %zu)\n", FLAT_QUERIES, QUERIES);
    std::printf("BBO speedup (uo+map vs uo only): %.0fx\n", fb_bbo / ob_bbo);
    std::printf("sink=%f\n", (double)sink);
    return 0;
}
