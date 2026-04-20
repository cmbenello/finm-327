#include "order_manager.h"

#include <iostream>

const char* OrderManager::status_str(OrderStatus s) {
    switch (s) {
        case OrderStatus::New: return "NEW";
        case OrderStatus::Filled: return "FILLED";
        case OrderStatus::PartiallyFilled: return "PARTIAL";
        case OrderStatus::Cancelled: return "CANCELLED";
    }
    return "?";
}

int OrderManager::place_order(Side side, double price, int qty) {
    int id = next_id_++;
    orders_.emplace(id, std::make_unique<MyOrder>(id, side, price, qty));
    std::cout << "[Strategy] Placing " << side_str(side) << " order at "
              << price << " x " << qty << " (ID = " << id << ")\n";
    return id;
}

void OrderManager::cancel(int id) {
    auto it = orders_.find(id);
    if (it == orders_.end()) {
        std::cout << "[Order] Cancel for unknown ID " << id << "\n";
        return;
    }
    it->second->status = OrderStatus::Cancelled;
    std::cout << "[Order] Order " << id << " cancelled and removed\n";
    orders_.erase(it);
}

void OrderManager::handle_fill(int id, int filled_qty) {
    auto it = orders_.find(id);
    if (it == orders_.end()) {
        std::cout << "[Order] Fill for unknown ID " << id << " (ignored)\n";
        return;
    }

    auto& ord = *it->second;
    ord.filled += filled_qty;
    std::cout << "[Execution] Order " << id << " filled: " << filled_qty << "\n";

    if (ord.filled >= ord.quantity) {
        ord.status = OrderStatus::Filled;
        std::cout << "[Order] Order " << id << " completed ("
                  << ord.filled << " / " << ord.quantity << ") and removed\n";
        orders_.erase(it);
    } else {
        ord.status = OrderStatus::PartiallyFilled;
        std::cout << "[Order] Order " << id << " partially filled: "
                  << ord.filled << " / " << ord.quantity << "\n";
    }
}

void OrderManager::print_active_orders() const {
    if (orders_.empty()) {
        std::cout << "[Order] No active orders\n";
        return;
    }
    std::cout << "[Order] Active orders:\n";
    for (const auto& [id, ord] : orders_) {
        std::cout << "  #" << id << " " << side_str(ord->side)
                  << " " << ord->price << " x " << ord->quantity
                  << " filled=" << ord->filled
                  << " status=" << status_str(ord->status) << "\n";
    }
}

bool OrderManager::has_active_at(double price, Side side) const {
    for (const auto& [id, ord] : orders_) {
        if (ord->side == side && ord->price == price) return true;
    }
    return false;
}
