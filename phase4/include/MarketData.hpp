#pragma once
#include <chrono>
#include <cstdint>
#include <string>

// 64-byte alignment so each tick lands on its own cache line.
// padding wastes a few bytes per tick but kills false sharing in
// any future producer/consumer split.
struct alignas(64) MarketData {
    std::string symbol;
    double bid_price{0.0};
    double ask_price{0.0};
    int    bid_size{0};
    int    ask_size{0};
    std::chrono::high_resolution_clock::time_point timestamp{};
    std::uint64_t seq{0};
};

static_assert(alignof(MarketData) == 64, "MarketData must be cache-line aligned");

// generates a stream of mock ticks. price walks a small range so the
// matching engine actually crosses sometimes.
class MarketDataFeed {
public:
    MarketDataFeed(std::string symbol, double mid, double spread, std::uint64_t seed);

    MarketData next();          // one tick
    void seed(std::uint64_t s); // reseed if you want deterministic runs

private:
    std::string symbol_;
    double mid_;
    double spread_;
    std::uint64_t state_;       // xorshift64 state
    std::uint64_t seq_{0};

    std::uint64_t rand64();
};
