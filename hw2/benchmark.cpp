#include "linalg.h"
#include <iostream>
#include <iomanip>
#include <chrono>
#include <random>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <functional>

struct BenchResult {
    double avg_ms;
    double stddev_ms;
    int runs;
};

// first run is warmup, rest get averaged
BenchResult benchmark(std::function<void()> fn, int num_runs = 10) {
    std::vector<double> times;
    times.reserve(num_runs);

    for (int r = 0; r < num_runs + 1; r++) {
        auto t0 = std::chrono::high_resolution_clock::now();
        fn();
        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
        if (r > 0) times.push_back(ms);
    }

    double sum = 0;
    for (double t : times) sum += t;
    double avg = sum / times.size();

    double var = 0;
    for (double t : times) var += (t - avg) * (t - avg);
    double stddev = std::sqrt(var / times.size());

    return {avg, stddev, num_runs};
}

void fill_random(double* data, size_t n, unsigned seed = 42) {
    std::mt19937 gen(seed);
    std::uniform_real_distribution<double> dist(-1.0, 1.0);
    for (size_t i = 0; i < n; i++) data[i] = dist(gen);
}

void print_result(const std::string& label, int n, const BenchResult& res) {
    std::cout << std::left << std::setw(35) << label
              << "  n=" << std::setw(6) << n
              << std::fixed << std::setprecision(3)
              << "  avg=" << std::setw(10) << res.avg_ms << " ms"
              << "  std=" << std::setw(10) << res.stddev_ms << " ms"
              << "\n";
}

void bench_matvec(const std::vector<int>& sizes) {
    std::cout << "\n-- Matrix-Vector: Row-Major vs Column-Major --\n\n";

    for (int n : sizes) {
        double* mat_row = new double[n * n];
        double* mat_col = new double[n * n];
        double* vec = new double[n];
        double* res = new double[n];

        fill_random(mat_row, n * n, 1);
        fill_random(vec, n, 2);

        for (int i = 0; i < n; i++)
            for (int j = 0; j < n; j++)
                mat_col[j * n + i] = mat_row[i * n + j];

        auto res_row = benchmark([&]() {
            multiply_mv_row_major(mat_row, n, n, vec, res);
        });
        auto res_col = benchmark([&]() {
            multiply_mv_col_major(mat_col, n, n, vec, res);
        });

        print_result("row-major", n, res_row);
        print_result("col-major", n, res_col);
        if (res_col.avg_ms > 0)
            std::cout << "  -> row/col ratio: "
                      << std::setprecision(2) << res_row.avg_ms / res_col.avg_ms << "x\n";
        std::cout << "\n";

        delete[] mat_row; delete[] mat_col; delete[] vec; delete[] res;
    }
}

void bench_matmul(const std::vector<int>& sizes) {
    std::cout << "\n-- Matrix-Matrix: Naive vs Transposed-B vs Tiled --\n\n";

    for (int n : sizes) {
        double* A = new double[n * n];
        double* B = new double[n * n];
        double* BT = new double[n * n];
        double* C = new double[n * n];

        fill_random(A, n * n, 10);
        fill_random(B, n * n, 20);
        transpose(B, n, n, BT);

        int runs = (n <= 512) ? 10 : 3;

        auto res_naive = benchmark([&]() {
            multiply_mm_naive(A, n, n, B, n, n, C);
        }, runs);
        auto res_trans = benchmark([&]() {
            multiply_mm_transposed_b(A, n, n, BT, n, n, C);
        }, runs);
        auto res_tiled = benchmark([&]() {
            multiply_mm_tiled(A, n, n, B, n, n, C);
        }, runs);

        print_result("naive (ijk)", n, res_naive);
        print_result("transposed-B", n, res_trans);
        print_result("tiled (blocked)", n, res_tiled);

        if (res_naive.avg_ms > 0) {
            std::cout << "  -> naive/transposed: "
                      << std::setprecision(2) << res_naive.avg_ms / res_trans.avg_ms << "x\n";
            std::cout << "  -> naive/tiled:      "
                      << std::setprecision(2) << res_naive.avg_ms / res_tiled.avg_ms << "x\n";
        }
        std::cout << "\n";

        delete[] A; delete[] B; delete[] BT; delete[] C;
    }
}

void bench_alignment(const std::vector<int>& sizes) {
    std::cout << "\n-- Memory Alignment: Aligned (64B) vs Regular new[] --\n\n";

    for (int n : sizes) {
        double* A_un = new double[n * n];
        double* B_un = new double[n * n];
        double* C_un = new double[n * n];

        double* A_al = alloc_aligned(n * n, 64);
        double* B_al = alloc_aligned(n * n, 64);
        double* C_al = alloc_aligned(n * n, 64);

        fill_random(A_un, n * n, 30);
        fill_random(B_un, n * n, 40);
        std::memcpy(A_al, A_un, n * n * sizeof(double));
        std::memcpy(B_al, B_un, n * n * sizeof(double));

        int runs = (n <= 512) ? 10 : 3;

        auto res_unaligned = benchmark([&]() {
            multiply_mm_naive(A_un, n, n, B_un, n, n, C_un);
        }, runs);
        auto res_aligned = benchmark([&]() {
            multiply_mm_naive(A_al, n, n, B_al, n, n, C_al);
        }, runs);

        print_result("unaligned (new[])", n, res_unaligned);
        print_result("aligned (64B)", n, res_aligned);
        std::cout << "  -> addresses: A_un=" << (void*)A_un
                  << " A_al=" << (void*)A_al << "\n";
        if (res_aligned.avg_ms > 0)
            std::cout << "  -> unaligned/aligned: "
                      << std::setprecision(3) << res_unaligned.avg_ms / res_aligned.avg_ms << "x\n";
        std::cout << "\n";

        delete[] A_un; delete[] B_un; delete[] C_un;
        free_aligned(A_al); free_aligned(B_al); free_aligned(C_al);
    }
}

void bench_inlining(const std::vector<int>& sizes) {
    std::cout << "\n-- Inlining: Direct ops vs Helper functions --\n";
    std::cout << "(compile with -O0 to see the difference, -O3 should be similar)\n\n";

    for (int n : sizes) {
        double* mat = new double[n * n];
        double* vec = new double[n];
        double* res = new double[n];
        fill_random(mat, n * n, 50);
        fill_random(vec, n, 60);

        auto res_direct = benchmark([&]() {
            multiply_mv_row_major(mat, n, n, vec, res);
        });
        auto res_helpers = benchmark([&]() {
            multiply_mv_row_major_helpers(mat, n, n, vec, res);
        });

        print_result("direct operations", n, res_direct);
        print_result("via helper funcs", n, res_helpers);
        if (res_direct.avg_ms > 0)
            std::cout << "  -> helpers/direct: "
                      << std::setprecision(2) << res_helpers.avg_ms / res_direct.avg_ms << "x\n";
        std::cout << "\n";

        delete[] mat; delete[] vec; delete[] res;
    }
}

int main() {
    std::cout << "Linear Algebra Benchmarks\n\n";

    #ifdef __OPTIMIZE__
    std::cout << "Compiler optimizations: ON\n";
    #else
    std::cout << "Compiler optimizations: OFF\n";
    #endif

    bench_matvec({64, 256, 1024, 2048, 4096});
    bench_matmul({64, 256, 512, 1024});
    bench_alignment({256, 512, 1024});
    bench_inlining({256, 1024, 4096});

    std::cout << "\ndone.\n";
    return 0;
}
