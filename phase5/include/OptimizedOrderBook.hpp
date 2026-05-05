#pragma once
#include <map>
#include <string>
#include <unordered_map>

#include "MemoryPool.hpp"
#include "Order.hpp"

namespace phase5 {

// optimized variant. wins over the baseline:
//   * pool-allocated nodes -- no malloc on add/delete after warmup.
//   * each price level is an intrusive doubly-linked list of nodes,
//     not an unordered_map<string,Order>. saves a hashmap per level
//     plus the second copy of Order.
//   * each node carries the iterator into the price-level map, so
//     delete is O(1) once we've looked up the node by id.
//   * lookup is a flat unordered_map<string, Slot> reserved up front so
//     it never rehashes during the run.
class OptimizedOrderBook {
    struct Node {
        Order  order;
        Node*  prev{nullptr};
        Node*  next{nullptr};
    };

    struct Level {
        Node* head{nullptr};
        Node* tail{nullptr};
    };

    using LevelMap = std::map<double, Level>;
    using LevelIt  = LevelMap::iterator;

    // hashmap of node* keyed by id. carry the level iterator on the value
    // so we can splice out of the price level in O(1).
    struct Slot {
        Node*   node;
        LevelIt level;
    };

public:
    explicit OptimizedOrderBook(size_t capacity = 1u << 20)
        : pool_(capacity) {
        lookup_.reserve(capacity);
    }

    void addOrder(const std::string& id, double price, int quantity, bool isBuy) {
        Node* n = pool_.acquire(Order{id, price, quantity, isBuy}, nullptr, nullptr);
        auto [lit, _] = orderLevels_.try_emplace(price);
        Level& lvl = lit->second;
        n->prev = lvl.tail;
        if (lvl.tail) lvl.tail->next = n; else lvl.head = n;
        lvl.tail = n;
        lookup_.emplace(id, Slot{n, lit});
    }

    void modifyOrder(const std::string& id, double newPrice, int newQuantity) {
        auto it = lookup_.find(id);
        if (it == lookup_.end()) return;

        Slot& slot = it->second;
        if (slot.node->order.price == newPrice) {
            slot.node->order.quantity = newQuantity;
            return;
        }
        const bool wasBuy = slot.node->order.isBuy;
        unlink(slot);
        pool_.release(slot.node);
        lookup_.erase(it);
        addOrder(id, newPrice, newQuantity, wasBuy);
    }

    void deleteOrder(const std::string& id) {
        auto it = lookup_.find(id);
        if (it == lookup_.end()) return;
        unlink(it->second);
        pool_.release(it->second.node);
        lookup_.erase(it);
    }

    bool contains(const std::string& id) const { return lookup_.find(id) != lookup_.end(); }
    size_t size()   const { return lookup_.size(); }
    size_t levels() const { return orderLevels_.size(); }

private:
    void unlink(Slot& slot) {
        Node*  n   = slot.node;
        Level& lvl = slot.level->second;
        if (n->prev) n->prev->next = n->next; else lvl.head = n->next;
        if (n->next) n->next->prev = n->prev; else lvl.tail = n->prev;
        if (!lvl.head) orderLevels_.erase(slot.level);
    }

    MemoryPool<Node>                       pool_;
    LevelMap                               orderLevels_;
    std::unordered_map<std::string, Slot>  lookup_;
};

} // namespace phase5
