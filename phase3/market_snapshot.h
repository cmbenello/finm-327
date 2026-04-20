#pragma once

#include <map>
#include <memory>
#include <functional>

struct PriceLevel {
    double price;
    int quantity;

    PriceLevel(double p, int q) : price(p), quantity(q) {}
};

class MarketSnapshot {
public:
    void update_bid(double price, int qty);
    void update_ask(double price, int qty);

    const PriceLevel* get_best_bid() const;
    const PriceLevel* get_best_ask() const;

    void print_top_of_book() const;

private:
    // bids sorted high -> low so begin() is best bid
    std::map<double, std::unique_ptr<PriceLevel>, std::greater<double>> bids_;
    // asks sorted low -> high so begin() is best ask
    std::map<double, std::unique_ptr<PriceLevel>> asks_;
};
