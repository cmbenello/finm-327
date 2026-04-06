#include "linalg.h"
#include <iostream>
#include <cmath>
#include <cstring>

const double TOL = 1e-10;
int tests_passed = 0;
int tests_failed = 0;

bool approx_eq(const double* a, const double* b, int n) {
    for (int i = 0; i < n; i++) {
        if (std::fabs(a[i] - b[i]) > TOL) {
            std::cerr << "  mismatch at " << i
                      << ": got " << a[i] << " expected " << b[i] << "\n";
            return false;
        }
    }
    return true;
}

void check(const std::string& name, bool ok) {
    std::cout << (ok ? "  PASS: " : "  FAIL: ") << name << "\n";
    ok ? tests_passed++ : tests_failed++;
}

void test_mv_row_major() {
    std::cout << "\nmultiply_mv_row_major\n";

    // | 1 2 3 |   | 1 |   | 14 |
    // | 4 5 6 | * | 2 | = | 32 |
    //              | 3 |
    double mat[] = {1, 2, 3, 4, 5, 6};
    double vec[] = {1, 2, 3};
    double res[2], expected[] = {14, 32};
    multiply_mv_row_major(mat, 2, 3, vec, res);
    check("2x3 basic", approx_eq(res, expected, 2));

    double eye[] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    double v3[] = {7, 8, 9};
    double res3[3];
    multiply_mv_row_major(eye, 3, 3, v3, res3);
    check("3x3 identity", approx_eq(res3, v3, 3));

    double one[] = {5.0}, v1[] = {3.0}, r1[1], e1[] = {15.0};
    multiply_mv_row_major(one, 1, 1, v1, r1);
    check("1x1 scalar", approx_eq(r1, e1, 1));
}

void test_mv_col_major() {
    std::cout << "\nmultiply_mv_col_major\n";

    // same matrix but col-major: col0={1,4} col1={2,5} col2={3,6}
    double mat[] = {1, 4, 2, 5, 3, 6};
    double vec[] = {1, 2, 3};
    double res[2], expected[] = {14, 32};
    multiply_mv_col_major(mat, 2, 3, vec, res);
    check("2x3 basic", approx_eq(res, expected, 2));

    double eye[] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
    double v3[] = {7, 8, 9}, res3[3];
    multiply_mv_col_major(eye, 3, 3, v3, res3);
    check("3x3 identity", approx_eq(res3, v3, 3));
}

void test_mm_naive() {
    std::cout << "\nmultiply_mm_naive\n";

    // | 1 2 | * | 5 6 | = | 19 22 |
    // | 3 4 |   | 7 8 |   | 43 50 |
    double A[] = {1, 2, 3, 4}, B[] = {5, 6, 7, 8};
    double C[4], expected[] = {19, 22, 43, 50};
    multiply_mm_naive(A, 2, 2, B, 2, 2, C);
    check("2x2 basic", approx_eq(C, expected, 4));

    double A2[] = {1, 2, 3, 4, 5, 6}, B2[] = {7, 8, 9, 10, 11, 12};
    double C2[4], expected2[] = {58, 64, 139, 154};
    multiply_mm_naive(A2, 2, 3, B2, 3, 2, C2);
    check("2x3 * 3x2", approx_eq(C2, expected2, 4));

    double eye[] = {1, 0, 0, 1}, C3[4];
    multiply_mm_naive(A, 2, 2, eye, 2, 2, C3);
    check("A * I = A", approx_eq(C3, A, 4));
}

void test_mm_transposed_b() {
    std::cout << "\nmultiply_mm_transposed_b\n";

    double A[] = {1, 2, 3, 4}, B[] = {5, 6, 7, 8}, BT[4];
    transpose(B, 2, 2, BT);
    double C[4], expected[] = {19, 22, 43, 50};
    multiply_mm_transposed_b(A, 2, 2, BT, 2, 2, C);
    check("2x2 basic", approx_eq(C, expected, 4));

    double A2[] = {1, 2, 3, 4, 5, 6}, B2[] = {7, 8, 9, 10, 11, 12}, BT2[6];
    transpose(B2, 3, 2, BT2);
    double C2[4], expected2[] = {58, 64, 139, 154};
    multiply_mm_transposed_b(A2, 2, 3, BT2, 3, 2, C2);
    check("2x3 * 3x2", approx_eq(C2, expected2, 4));
}

void test_mm_tiled() {
    std::cout << "\nmultiply_mm_tiled\n";

    double A[] = {1, 2, 3, 4}, B[] = {5, 6, 7, 8};
    double C_naive[4], C_tiled[4];
    multiply_mm_naive(A, 2, 2, B, 2, 2, C_naive);
    multiply_mm_tiled(A, 2, 2, B, 2, 2, C_tiled);
    check("2x2 matches naive", approx_eq(C_tiled, C_naive, 4));

    // bigger random-ish test
    const int N = 100;
    double* Ab = new double[N * N];
    double* Bb = new double[N * N];
    double* Cn = new double[N * N];
    double* Ct = new double[N * N];
    for (int i = 0; i < N * N; i++) {
        Ab[i] = (i * 17 + 3) % 100 - 50.0;
        Bb[i] = (i * 13 + 7) % 100 - 50.0;
    }
    multiply_mm_naive(Ab, N, N, Bb, N, N, Cn);
    multiply_mm_tiled(Ab, N, N, Bb, N, N, Ct);
    check("100x100 matches naive", approx_eq(Ct, Cn, N * N));
    delete[] Ab; delete[] Bb; delete[] Cn; delete[] Ct;
}

void test_helpers() {
    std::cout << "\nmultiply_mv_row_major_helpers\n";
    double mat[] = {1, 2, 3, 4, 5, 6}, vec[] = {1, 2, 3};
    double res1[2], res2[2];
    multiply_mv_row_major(mat, 2, 3, vec, res1);
    multiply_mv_row_major_helpers(mat, 2, 3, vec, res2);
    check("matches direct version", approx_eq(res1, res2, 2));
}

void test_alignment() {
    std::cout << "\nmemory alignment\n";
    double* p = alloc_aligned(100, 64);
    check("64-byte aligned", (reinterpret_cast<uintptr_t>(p) % 64) == 0);
    free_aligned(p);

    double* p2 = alloc_aligned(100, 32);
    check("32-byte aligned", (reinterpret_cast<uintptr_t>(p2) % 32) == 0);
    free_aligned(p2);
}

int main() {
    std::cout << "Running correctness tests...\n";

    test_mv_row_major();
    test_mv_col_major();
    test_mm_naive();
    test_mm_transposed_b();
    test_mm_tiled();
    test_helpers();
    test_alignment();

    std::cout << "\n" << tests_passed << " passed, " << tests_failed << " failed\n";
    return tests_failed > 0 ? 1 : 0;
}
