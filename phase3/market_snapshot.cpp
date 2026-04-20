#include "market_snapshot.h"

#include <iostream>

void MarketSnapshot::update_bid(double price, int qty) {
    if (qty <= 0) {
        auto it = bids_.find(price);
        if (it != bids_.end()) {
            bids_.erase(it); // unique_ptr cleans up
            std::cout << "[Market] Bid " << price << " removed\n";
        }
        return;
    }

    auto it = bids_.find(price);
    if (it == bids_.end()) {
        bids_.emplace(price, std::make_unique<PriceLevel>(price, qty));
    } else {
        it->second->quantity = qty;
    }
    std::cout << "[Market] Bid " << price << " x " << qty << "\n";
}

void MarketSnapshot::update_ask(double price, int qty) {
    if (qty <= 0) {
        auto it = asks_.find(price);
        if (it != asks_.end()) {
            asks_.erase(it);
            std::cout << "[Market] Ask " << price << " removed\n";
        }
        return;
    }

    auto it = asks_.find(price);
    if (it == asks_.end()) {
        asks_.emplace(price, std::make_unique<PriceLevel>(price, qty));
    } else {
        it->second->quantity = qty;
    }
    std::cout << "[Market] Ask " << price << " x " << qty << "\n";
}

const PriceLevel* MarketSnapshot::get_best_bid() const {
    if (bids_.empty()) return nullptr;
    return bids_.begin()->second.get();
}

const PriceLevel* MarketSnapshot::get_best_ask() const {
    if (asks_.empty()) return nullptr;
    return asks_.begin()->second.get();
}

void MarketSnapshot::print_top_of_book() const {
    const auto* bb = get_best_bid();
    const auto* ba = get_best_ask();
    std::cout << "[Market] TOB  bid=";
    if (bb) std::cout << bb->price << " x " << bb->quantity; else std::cout << "-";
    std::cout << "  ask=";
    if (ba) std::cout << ba->price << " x " << ba->quantity; else std::cout << "-";
    std::cout << "\n";
}
