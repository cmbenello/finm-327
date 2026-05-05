// quick demo: build a small book, push a few orders, and dump some stats.
// the heavy lifting lives in benchmark.cpp.
#include <cstdio>

#include "OptimizedOrderBook.hpp"
#include "OrderBook.hpp"

using namespace phase5;

int main() {
    OrderBook b;
    b.addOrder("ORD001", 50.10, 100, true);
    b.addOrder("ORD002", 50.20, 200, true);
    b.addOrder("ORD003", 50.20, 50,  false);
    b.modifyOrder("ORD002", 50.30, 250);
    b.deleteOrder("ORD001");

    std::printf("baseline book: %zu orders across %zu price levels\n",
                b.size(), b.levels());

    OptimizedOrderBook ob(1024);
    for (int i = 0; i < 100; ++i) {
        ob.addOrder("X" + std::to_string(i), 99.0 + (i % 10) * 0.01, 10, i & 1);
    }
    for (int i = 0; i < 50; ++i) ob.deleteOrder("X" + std::to_string(i));
    std::printf("optimized book: %zu orders across %zu price levels\n",
                ob.size(), ob.levels());
    return 0;
}
