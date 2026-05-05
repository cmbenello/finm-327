// Part 2: Binary max-heap priority queue for order matching, vs std::priority_queue.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdint>
#include <queue>
#include <random>
#include <vector>

struct Order {
    double   price;
    uint64_t timestamp;
    uint32_t qty;
    uint32_t id;
};

// Max-heap on price; ties broken by earlier timestamp (FIFO at a price level).
struct OrderLess {
    bool operator()(const Order& a, const Order& b) const noexcept {
        if (a.price != b.price) return a.price < b.price;
        return a.timestamp > b.timestamp;
    }
};

// Hole-based sift: instead of swapping at every level (2 stores/level), we
// carry a "hole" through the tree and write the moved-in element exactly once
// at the end (1 store/level + 1 final store). This is what libstdc++/libc++
// do in push_heap/pop_heap, and it roughly halves the memory traffic.
template <typename T, typename Cmp = std::less<T>>
class BinaryHeap {
    std::vector<T> data_;
    Cmp cmp_;

    void sift_up_hole(size_t hole, T value) {
        while (hole > 0) {
            size_t p = (hole - 1) >> 1;
            if (!cmp_(data_[p], value)) break;
            data_[hole] = std::move(data_[p]);
            hole = p;
        }
        data_[hole] = std::move(value);
    }
    void sift_down_hole(size_t hole, T value) {
        const size_t n = data_.size();
        size_t child = 2 * hole + 1;
        while (child < n) {
            if (child + 1 < n && cmp_(data_[child], data_[child + 1])) ++child;
            if (!cmp_(value, data_[child])) break;
            data_[hole] = std::move(data_[child]);
            hole = child;
            child = 2 * hole + 1;
        }
        data_[hole] = std::move(value);
    }
public:
    BinaryHeap() = default;
    explicit BinaryHeap(size_t reserve) { data_.reserve(reserve); }

    bool empty() const { return data_.empty(); }
    size_t size() const { return data_.size(); }
    const T& top() const { return data_.front(); }

    void push(const T& v) {
        data_.push_back(T{});
        sift_up_hole(data_.size() - 1, v);
    }
    void pop() {
        T tail = std::move(data_.back());
        data_.pop_back();
        if (!data_.empty()) sift_down_hole(0, std::move(tail));
    }
};

template <typename F>
double time_ms(F&& f) {
    auto t0 = std::chrono::steady_clock::now();
    f();
    auto t1 = std::chrono::steady_clock::now();
    return std::chrono::duration<double, std::milli>(t1 - t0).count();
}

int main() {
    constexpr size_t N = 1'000'000;

    std::mt19937_64 rng(123);
    std::uniform_real_distribution<double> price(50.0, 250.0);
    std::vector<Order> orders;
    orders.reserve(N);
    for (size_t i = 0; i < N; ++i)
        orders.push_back(Order{price(rng), i, 100, static_cast<uint32_t>(i)});

    // Custom heap
    BinaryHeap<Order, OrderLess> bh(N);
    double bh_push = time_ms([&] { for (auto& o : orders) bh.push(o); });
    volatile double sink = 0;
    double bh_pop = time_ms([&] {
        while (!bh.empty()) { sink += bh.top().price; bh.pop(); }
    });

    // std::priority_queue
    std::priority_queue<Order, std::vector<Order>, OrderLess> pq;
    double pq_push = time_ms([&] { for (auto& o : orders) pq.push(o); });
    double pq_pop = time_ms([&] {
        while (!pq.empty()) { sink += pq.top().price; pq.pop(); }
    });

    // Mixed workload (push/pop interleaved as a matching engine would do)
    BinaryHeap<Order, OrderLess> bh2(N);
    std::priority_queue<Order, std::vector<Order>, OrderLess> pq2;
    double bh_mix = time_ms([&] {
        for (size_t i = 0; i < N; ++i) {
            bh2.push(orders[i]);
            if ((i & 1) && !bh2.empty()) { sink += bh2.top().price; bh2.pop(); }
        }
    });
    double pq_mix = time_ms([&] {
        for (size_t i = 0; i < N; ++i) {
            pq2.push(orders[i]);
            if ((i & 1) && !pq2.empty()) { sink += pq2.top().price; pq2.pop(); }
        }
    });

    std::printf("=== Part 2: Priority Queue (N=%zu) ===\n", N);
    std::printf("%-22s %12s %12s %12s\n", "structure", "push(ms)", "pop(ms)", "mixed(ms)");
    std::printf("%-22s %12.2f %12.2f %12.2f\n", "BinaryHeap", bh_push, bh_pop, bh_mix);
    std::printf("%-22s %12.2f %12.2f %12.2f\n", "std::priority_queue", pq_push, pq_pop, pq_mix);
    std::printf("speedup (push):  %.2fx\n", pq_push / bh_push);
    std::printf("speedup (pop):   %.2fx\n", pq_pop / bh_pop);
    std::printf("speedup (mixed): %.2fx\n", pq_mix / bh_mix);
    std::printf("sink=%f\n", (double)sink);
    return 0;
}
