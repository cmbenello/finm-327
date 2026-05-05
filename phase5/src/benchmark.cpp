#include <algorithm>
#include <cstdio>
#include <fstream>
#include <random>
#include <string>
#include <vector>

#include "OptimizedOrderBook.hpp"
#include "OrderBook.hpp"
#include "Timer.hpp"

using namespace phase5;

namespace {

struct Op {
    enum Kind : uint8_t { Add, Modify, Delete };
    Kind   kind;
    int    idx;       // index into ids_
    double price;
    int    qty;
    bool   isBuy;
};

// build a deterministic op stream so both books see the same workload.
std::vector<Op> build_ops(size_t n_add, size_t n_mod, size_t n_del, uint32_t seed) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> price(50.0, 100.0);
    std::uniform_int_distribution<int>     qty(1, 500);
    std::uniform_int_distribution<int>     coin(0, 1);

    std::vector<Op> ops;
    ops.reserve(n_add + n_mod + n_del);

    for (size_t i = 0; i < n_add; ++i) {
        ops.push_back({Op::Add, (int)i, price(rng), qty(rng), (bool)coin(rng)});
    }
    // interleave mods + deletes against existing ids
    std::uniform_int_distribution<int> idx(0, (int)n_add - 1);
    for (size_t i = 0; i < n_mod; ++i) {
        ops.push_back({Op::Modify, idx(rng), price(rng), qty(rng), false});
    }
    for (size_t i = 0; i < n_del; ++i) {
        ops.push_back({Op::Delete, idx(rng), 0.0, 0, false});
    }
    std::shuffle(ops.begin() + n_add, ops.end(), rng);  // keep adds first
    return ops;
}

std::vector<std::string> build_ids(size_t n) {
    std::vector<std::string> v;
    v.reserve(n);
    for (size_t i = 0; i < n; ++i) v.emplace_back("ORD" + std::to_string(i));
    return v;
}

template <typename Book>
double run_workload(Book& book, const std::vector<Op>& ops, const std::vector<std::string>& ids) {
    Timer t;
    for (const auto& op : ops) {
        switch (op.kind) {
            case Op::Add:    book.addOrder(ids[op.idx], op.price, op.qty, op.isBuy); break;
            case Op::Modify: book.modifyOrder(ids[op.idx], op.price, op.qty);        break;
            case Op::Delete: book.deleteOrder(ids[op.idx]);                          break;
        }
    }
    return t.elapsed_seconds();
}

template <typename Book>
double pure_add(Book& book, const std::vector<std::string>& ids, const std::vector<Op>& ops) {
    Timer t;
    for (size_t i = 0; i < ids.size(); ++i) {
        book.addOrder(ids[i], ops[i].price, ops[i].qty, ops[i].isBuy);
    }
    return t.elapsed_seconds();
}

} // namespace

int main(int argc, char** argv) {
    const std::vector<size_t> sizes = {1000, 5000, 10000, 50000, 100000, 500000, 1000000};
    const char* csv_path = argc > 1 ? argv[1] : "bench_results.csv";

    std::ofstream csv(csv_path);
    csv << "size,book,workload,seconds,ops_per_sec\n";

    std::printf("%-10s %-12s %-14s %14s %16s\n",
                "size", "book", "workload", "seconds", "ops/sec");
    std::printf("------------------------------------------------------------------------\n");

    for (size_t n : sizes) {
        const size_t n_mods = n / 4;
        const size_t n_dels = n / 4;
        auto ids = build_ids(n);
        auto ops = build_ops(n, n_mods, n_dels, /*seed=*/42);

        // -------- pure adds (same n, just the first n adds) --------
        {
            OrderBook b;
            double s = pure_add(b, ids, ops);
            csv << n << ",baseline,add," << s << "," << (double)n / s << "\n";
            std::printf("%-10zu %-12s %-14s %14.6f %16.0f\n", n, "baseline", "add", s, n / s);
        }
        {
            OptimizedOrderBook b(n + 16);
            double s = pure_add(b, ids, ops);
            csv << n << ",optimized,add," << s << "," << (double)n / s << "\n";
            std::printf("%-10zu %-12s %-14s %14.6f %16.0f\n", n, "optimized", "add", s, n / s);
        }

        // -------- mixed add/modify/delete --------
        {
            OrderBook b;
            double s = run_workload(b, ops, ids);
            csv << n << ",baseline,mixed," << s << "," << (double)ops.size() / s << "\n";
            std::printf("%-10zu %-12s %-14s %14.6f %16.0f\n",
                        n, "baseline", "mixed", s, ops.size() / s);
        }
        {
            OptimizedOrderBook b(n + 16);
            double s = run_workload(b, ops, ids);
            csv << n << ",optimized,mixed," << s << "," << (double)ops.size() / s << "\n";
            std::printf("%-10zu %-12s %-14s %14.6f %16.0f\n",
                        n, "optimized", "mixed", s, ops.size() / s);
        }
    }
    std::printf("\nresults written to %s\n", csv_path);
    return 0;
}
