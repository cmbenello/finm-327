#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <vector>

#include "MarketData.hpp"
#include "MatchingEngine.hpp"
#include "OrderBook.hpp"
#include "OrderManager.hpp"
#include "Timer.hpp"
#include "TradeLogger.hpp"

namespace {

struct LatencyStats {
    long long min, max, p50, p99;
    double mean, stddev;
    std::size_t n;
};

LatencyStats analyze(std::vector<long long> v) {
    LatencyStats s{};
    s.n = v.size();
    if (v.empty()) return s;
    std::sort(v.begin(), v.end());
    s.min = v.front();
    s.max = v.back();
    s.p50 = v[v.size() / 2];
    s.p99 = v[static_cast<std::size_t>(v.size() * 0.99)];
    double sum = std::accumulate(v.begin(), v.end(), 0.0);
    s.mean = sum / v.size();
    double var = 0.0;
    for (auto x : v) var += (x - s.mean) * (x - s.mean);
    s.stddev = std::sqrt(var / v.size());
    return s;
}

void print_stats(const char* label, const LatencyStats& s) {
    std::cout << std::left << std::setw(28) << label
              << "n=" << s.n
              << "  min="    << s.min
              << "  p50="    << s.p50
              << "  mean="   << static_cast<long long>(s.mean)
              << "  p99="    << s.p99
              << "  max="    << s.max
              << "  std="    << static_cast<long long>(s.stddev)
              << "  (ns)\n";
}

// turn a tick into an order. half the time we cross the book aggressively,
// half the time we post a passive resting order. the goal is to keep both
// sides of the book non-empty so the matcher is exercised.
OrderManager::OrderPtr tick_to_order(OrderManager& oms, const MarketData& md,
                                     std::uint64_t r) {
    bool aggressive = (r & 1u);
    Side s          = (r & 2u) ? Side::Buy : Side::Sell;
    int  qty        = 50 + static_cast<int>((r >> 8) % 200);

    double px;
    if (aggressive) {
        px = (s == Side::Buy) ? md.ask_price + 0.02 : md.bid_price - 0.02;
    } else {
        px = (s == Side::Buy) ? md.bid_price - 0.01 : md.ask_price + 0.01;
    }
    return oms.place(md.symbol, px, qty, s);
}

template <typename BookT>
LatencyStats run(std::size_t num_ticks, BookT& book, const char* label) {
    OrderManager oms;
    TradeLogger  log(num_ticks * 2);
    MatchingEngine<BookT> engine(book, log);
    MarketDataFeed feed("AAPL", 150.0, 0.05, 0xCAFEBABE);

    std::vector<long long> tick_to_trade;
    tick_to_trade.reserve(num_ticks);

    std::uint64_t prng = 0x12345678;
    int trades = 0;

    for (std::size_t i = 0; i < num_ticks; ++i) {
        MarketData md = feed.next();

        // capture wall-clock at "tick recv" -- this is the start of t2t.
        auto t_recv = std::chrono::high_resolution_clock::now();
        long long t_recv_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  t_recv.time_since_epoch()).count();

        prng ^= prng << 13; prng ^= prng >> 7; prng ^= prng << 17;
        auto order = tick_to_order(oms, md, prng);

        Timer t; t.start();
        trades += engine.submit(order, md.seq, t_recv_ns);
        long long elapsed = t.stop();
        tick_to_trade.push_back(elapsed);

        if ((i & 0x3FF) == 0) oms.reap();
    }

    log.flush("trades.csv");

    auto s = analyze(std::move(tick_to_trade));
    std::cout << "[" << label << "] ticks=" << num_ticks
              << " trades=" << trades
              << " logged=" << log.pending() << "\n";
    print_stats(label, s);
    return s;
}

} // namespace

int main(int argc, char** argv) {
    std::size_t N = (argc > 1) ? static_cast<std::size_t>(std::atoll(argv[1])) : 100000;

    std::cout << "phase4 hft sim -- N=" << N << "\n\n";

    {
        OrderBook<double, std::int64_t> book;
        run(N, book, "map-book");
    }
    std::cout << "\n";
    {
        FlatOrderBook<double, std::int64_t> book;
        run(N, book, "flat-book");
    }

    std::cout << "\ntrades written to trades.csv\n";
    return 0;
}
