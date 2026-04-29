#include "MarketData.hpp"

MarketDataFeed::MarketDataFeed(std::string symbol, double mid, double spread, std::uint64_t seed)
    : symbol_(std::move(symbol)), mid_(mid), spread_(spread), state_(seed ? seed : 0x9E3779B97F4A7C15ULL) {}

void MarketDataFeed::seed(std::uint64_t s) { state_ = s ? s : 0x9E3779B97F4A7C15ULL; }

// xorshift64 -- fine for a sim, ~1ns per call.
std::uint64_t MarketDataFeed::rand64() {
    std::uint64_t x = state_;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    state_ = x;
    return x;
}

MarketData MarketDataFeed::next() {
    // walk the mid by +/- 5 cents on roughly half of ticks.
    int step = static_cast<int>(rand64() % 11) - 5;          // [-5, +5]
    mid_ += step * 0.01;

    MarketData m;
    m.symbol     = symbol_;
    m.bid_price  = mid_ - spread_ * 0.5;
    m.ask_price  = mid_ + spread_ * 0.5;
    m.bid_size   = 100 + static_cast<int>(rand64() % 400);
    m.ask_size   = 100 + static_cast<int>(rand64() % 400);
    m.timestamp  = std::chrono::high_resolution_clock::now();
    m.seq        = ++seq_;
    return m;
}
