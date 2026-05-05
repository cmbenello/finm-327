#pragma once
#include <map>
#include <string>
#include <unordered_map>

#include "Order.hpp"

namespace phase5 {

// baseline order book straight out of the spec:
//   price -> { id -> Order }   (sorted by price)
//   id    -> Order              (O(1) lookup)
//
// the spec stores Order by value in both maps. we keep that on purpose so the
// benchmark numbers reflect the textbook design, not a tuned variant.
class OrderBook {
public:
    void addOrder(const std::string& id, double price, int quantity, bool isBuy) {
        Order o{id, price, quantity, isBuy};
        orderLevels_[price].emplace(id, o);
        orderLookup_.emplace(id, o);
    }

    void modifyOrder(const std::string& id, double newPrice, int newQuantity) {
        auto it = orderLookup_.find(id);
        if (it == orderLookup_.end()) return;
        const Order old = it->second;
        eraseFromLevel(old.price, id);
        orderLookup_.erase(it);
        addOrder(id, newPrice, newQuantity, old.isBuy);
    }

    void deleteOrder(const std::string& id) {
        auto it = orderLookup_.find(id);
        if (it == orderLookup_.end()) return;
        eraseFromLevel(it->second.price, id);
        orderLookup_.erase(it);
    }

    bool   contains(const std::string& id) const { return orderLookup_.count(id) != 0; }
    size_t size()   const { return orderLookup_.size(); }
    size_t levels() const { return orderLevels_.size(); }

    // walked by tests; not used on the hot benchmark path.
    const std::unordered_map<std::string, Order>& lookup() const { return orderLookup_; }
    const std::map<double, std::unordered_map<std::string, Order>>& levels_map() const {
        return orderLevels_;
    }

private:
    void eraseFromLevel(double price, const std::string& id) {
        auto lvl = orderLevels_.find(price);
        if (lvl == orderLevels_.end()) return;
        lvl->second.erase(id);
        if (lvl->second.empty()) orderLevels_.erase(lvl);
    }

    std::map<double, std::unordered_map<std::string, Order>> orderLevels_;
    std::unordered_map<std::string, Order>                   orderLookup_;
};

} // namespace phase5
