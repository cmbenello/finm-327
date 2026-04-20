#pragma once

#include <map>
#include <memory>
#include <string>

enum class Side { Buy, Sell };

enum class OrderStatus { New, Filled, PartiallyFilled, Cancelled };

struct MyOrder {
    int id;
    Side side;
    double price;
    int quantity;
    int filled = 0;
    OrderStatus status = OrderStatus::New;

    MyOrder(int i, Side s, double p, int q)
        : id(i), side(s), price(p), quantity(q) {}
};

class OrderManager {
public:
    int place_order(Side side, double price, int qty);
    void cancel(int id);
    void handle_fill(int id, int filled_qty);
    void print_active_orders() const;

    bool has_active() const { return !orders_.empty(); }
    bool has_active_at(double price, Side side) const;

private:
    std::map<int, std::unique_ptr<MyOrder>> orders_;
    int next_id_ = 1;

    static const char* side_str(Side s) { return s == Side::Buy ? "BUY" : "SELL"; }
    static const char* status_str(OrderStatus s);
};
