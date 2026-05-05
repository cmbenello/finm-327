#include <cassert>
#include <iostream>
#include <string>

#include "OptimizedOrderBook.hpp"
#include "OrderBook.hpp"

using namespace phase5;

namespace {

void test_add() {
    OrderBook b;
    b.addOrder("ORD001", 50.10, 100, true);
    b.addOrder("ORD002", 50.20, 200, false);
    assert(b.contains("ORD001"));
    assert(b.contains("ORD002"));
    assert(b.size() == 2);
    assert(b.levels() == 2);
}

void test_modify() {
    OrderBook b;
    b.addOrder("A", 10.0, 5, true);
    b.modifyOrder("A", 11.0, 7);
    auto it = b.lookup().find("A");
    assert(it != b.lookup().end());
    assert(it->second.price == 11.0);
    assert(it->second.quantity == 7);
    assert(b.levels() == 1);
}

void test_delete() {
    OrderBook b;
    b.addOrder("X", 99.0, 1, false);
    b.addOrder("Y", 99.0, 2, false);
    b.deleteOrder("X");
    assert(!b.contains("X"));
    assert(b.contains("Y"));
    assert(b.levels() == 1);
    b.deleteOrder("Y");
    assert(b.levels() == 0);
}

void test_delete_unknown_is_noop() {
    OrderBook b;
    b.deleteOrder("nope");
    b.modifyOrder("nope", 1.0, 1);
    assert(b.size() == 0);
}

void test_optimized_parity() {
    OptimizedOrderBook b(1024);
    b.addOrder("A", 10.0, 5, true);
    b.addOrder("B", 10.0, 7, true);
    b.addOrder("C", 11.0, 1, false);
    assert(b.size() == 3);
    assert(b.levels() == 2);

    b.modifyOrder("A", 10.0, 50);     // same-level: in-place
    b.modifyOrder("B", 12.0, 9);      // cross-level: relocate
    assert(b.size() == 3);
    assert(b.levels() == 3);

    b.deleteOrder("A");
    b.deleteOrder("B");
    b.deleteOrder("C");
    assert(b.size() == 0);
    assert(b.levels() == 0);
}

void test_optimized_many() {
    OptimizedOrderBook b(10000);
    for (int i = 0; i < 5000; ++i) {
        b.addOrder("ORD" + std::to_string(i), 50.0 + (i % 50) * 0.01, 100, i & 1);
    }
    assert(b.size() == 5000);
    for (int i = 0; i < 5000; i += 2) {
        b.deleteOrder("ORD" + std::to_string(i));
    }
    assert(b.size() == 2500);
}

} // namespace

int main() {
    test_add();
    test_modify();
    test_delete();
    test_delete_unknown_is_noop();
    test_optimized_parity();
    test_optimized_many();
    std::cout << "all phase5 unit tests passed\n";
    return 0;
}
