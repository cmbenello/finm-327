// experiments harness. runs the table from the spec:
//   - container layout (map vs flat)
//   - load scaling (1K / 10K / 100K / 1M)
//   - allocation (heap shared_ptr vs raw pool)
//   - alignas tick struct vs unaligned tick struct
//
// each experiment reports min / p50 / mean / p99 / max in nanoseconds.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <vector>

#include "MarketData.hpp"
#include "MatchingEngine.hpp"
#include "MemoryPool.hpp"
#include "OrderBook.hpp"
#include "OrderManager.hpp"
#include "Timer.hpp"
#include "TradeLogger.hpp"

namespace {

struct Stats {
    long long min, max, p50, p99;
    double mean, stddev;
    std::size_t n;
};

Stats analyze(std::vector<long long> v) {
    Stats s{};
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

void row(const std::string& label, const Stats& s) {
    std::cout << std::left << std::setw(36) << label
              << " n=" << std::setw(8) << s.n
              << " min="  << std::setw(7) << s.min
              << " p50="  << std::setw(7) << s.p50
              << " mean=" << std::setw(7) << static_cast<long long>(s.mean)
              << " p99="  << std::setw(8) << s.p99
              << " max="  << std::setw(8) << s.max
              << " std="  << static_cast<long long>(s.stddev)
              << " (ns)\n";
}

OrderManager::OrderPtr tick_to_order(OrderManager& oms, const MarketData& md,
                                     std::uint64_t r) {
    bool aggressive = (r & 1u);
    Side s          = (r & 2u) ? Side::Buy : Side::Sell;
    int  qty        = 50 + static_cast<int>((r >> 8) % 200);
    double px;
    if (aggressive) px = (s == Side::Buy) ? md.ask_price + 0.02 : md.bid_price - 0.02;
    else            px = (s == Side::Buy) ? md.bid_price - 0.01 : md.ask_price + 0.01;
    return oms.place(md.symbol, px, qty, s);
}

template <typename BookT>
Stats sweep(std::size_t N, const std::string& label) {
    BookT book;
    OrderManager oms;
    TradeLogger  log(N * 2);
    MatchingEngine<BookT> engine(book, log);
    MarketDataFeed feed("AAPL", 150.0, 0.05, 0xCAFEBABE);

    std::vector<long long> t2t;
    t2t.reserve(N);

    std::uint64_t prng = 0x12345678;
    for (std::size_t i = 0; i < N; ++i) {
        MarketData md = feed.next();
        auto t_recv = std::chrono::high_resolution_clock::now();
        long long t_recv_ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                                  t_recv.time_since_epoch()).count();
        prng ^= prng << 13; prng ^= prng >> 7; prng ^= prng << 17;
        auto o = tick_to_order(oms, md, prng);

        Timer t; t.start();
        engine.submit(o, md.seq, t_recv_ns);
        t2t.push_back(t.stop());

        if ((i & 0x3FF) == 0) oms.reap();
    }
    auto s = analyze(std::move(t2t));
    row(label, s);
    return s;
}

// pure-allocation microbench: how fast can we churn N orders?
// (1) std::make_shared, (2) raw new/delete, (3) MemoryPool.
void alloc_bench(std::size_t N) {
    std::cout << "\n-- allocation paths (Order objects) --\n";

    {
        std::vector<long long> ns; ns.reserve(N);
        for (std::size_t i = 0; i < N; ++i) {
            Timer t; t.start();
            auto p = std::make_shared<OrderD>(static_cast<std::int64_t>(i),
                                              "AAPL", 150.0, 100, Side::Buy);
            (void)p;
            ns.push_back(t.stop());
        }
        row("shared_ptr / make_shared", analyze(std::move(ns)));
    }
    {
        std::vector<long long> ns; ns.reserve(N);
        for (std::size_t i = 0; i < N; ++i) {
            Timer t; t.start();
            auto* p = new OrderD(static_cast<std::int64_t>(i),
                                 "AAPL", 150.0, 100, Side::Buy);
            delete p;
            ns.push_back(t.stop());
        }
        row("raw new/delete", analyze(std::move(ns)));
    }
    {
        MemoryPool<OrderD> pool(N);
        std::vector<long long> ns; ns.reserve(N);
        for (std::size_t i = 0; i < N; ++i) {
            Timer t; t.start();
            auto* p = pool.acquire(static_cast<std::int64_t>(i),
                                   "AAPL", 150.0, 100, Side::Buy);
            pool.release(p);
            ns.push_back(t.stop());
        }
        row("MemoryPool acquire/release", analyze(std::move(ns)));
    }
}

// alignment microbench: read+write a stream of MarketData, with and
// without 64-byte alignment. allocate a big array of each, walk it.
void align_bench(std::size_t N) {
    std::cout << "\n-- cache alignment (MarketData walk) --\n";

    // deliberately sized to straddle a 64B line: 64 + 8 = 72 bytes,
    // with no alignas, so consecutive entries share cache lines awkwardly.
    struct Unaligned {
        std::string symbol;
        double bid_price{0.0};
        double ask_price{0.0};
        int    bid_size{0};
        int    ask_size{0};
        std::chrono::high_resolution_clock::time_point timestamp{};
        std::uint64_t seq{0};
        std::uint64_t pad_extra{0};   // pushes us to 72B, off the line
    };

    {
        std::vector<MarketData> v(N);
        for (std::size_t i = 0; i < N; ++i) {
            v[i].symbol = "AAPL";
            v[i].bid_price = 150.0 + (i % 5) * 0.01;
            v[i].ask_price = v[i].bid_price + 0.05;
            v[i].seq = i;
        }
        Timer t; t.start();
        double acc = 0.0;
        for (auto& m : v) acc += m.bid_price + m.ask_price;
        long long ns = t.stop();
        std::cout << std::left << std::setw(36) << "alignas(64) MarketData walk"
                  << " N=" << N << " ns=" << ns
                  << " (acc=" << acc << ", " << sizeof(MarketData) << "B/struct)\n";
    }
    {
        std::vector<Unaligned> v(N);
        for (std::size_t i = 0; i < N; ++i) {
            v[i].symbol = "AAPL";
            v[i].bid_price = 150.0 + (i % 5) * 0.01;
            v[i].ask_price = v[i].bid_price + 0.05;
            v[i].seq = i;
        }
        Timer t; t.start();
        double acc = 0.0;
        for (auto& m : v) acc += m.bid_price + m.ask_price;
        long long ns = t.stop();
        std::cout << std::left << std::setw(36) << "unaligned MarketData walk"
                  << " N=" << N << " ns=" << ns
                  << " (acc=" << acc << ", " << sizeof(Unaligned) << "B/struct)\n";
    }
}

} // namespace

int main() {
    std::cout << "phase4 latency experiments\n"
              << "==========================\n\n";

    std::cout << "-- container layout, N=10K --\n";
    sweep<OrderBook<double, std::int64_t>>     (10000, "map-book   N=10K");
    sweep<FlatOrderBook<double, std::int64_t>> (10000, "flat-book  N=10K");

    std::cout << "\n-- load scaling, map-book --\n";
    sweep<OrderBook<double, std::int64_t>>     (1000,    "map-book   N=1K");
    sweep<OrderBook<double, std::int64_t>>     (10000,   "map-book   N=10K");
    sweep<OrderBook<double, std::int64_t>>     (100000,  "map-book   N=100K");
    sweep<OrderBook<double, std::int64_t>>     (1000000, "map-book   N=1M");

    std::cout << "\n-- load scaling, flat-book --\n";
    sweep<FlatOrderBook<double, std::int64_t>> (1000,    "flat-book  N=1K");
    sweep<FlatOrderBook<double, std::int64_t>> (10000,   "flat-book  N=10K");
    sweep<FlatOrderBook<double, std::int64_t>> (100000,  "flat-book  N=100K");

    alloc_bench(100000);
    align_bench(1000000);
    return 0;
}
