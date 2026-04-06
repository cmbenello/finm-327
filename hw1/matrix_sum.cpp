#include <iostream>
#include <vector>
#include <chrono>
#include <random>
#include <numeric>

const int SIZE = 4096;

// --- basic (unoptimized) version from the starter code ---

int getElement(const std::vector<std::vector<int>>& matrix, int row, int col) {
    return matrix[row][col];
}

int add(int a, int b) {
    return a + b;
}

// this is slow mainly because we're calling two functions per element.
// that's 4096*4096 = ~16 million function calls just for getElement alone,
// plus another ~16M for add. each call has overhead from setting up the
// stack frame, jumping, etc. also the double vector indirection means
// we're chasing pointers a lot which isn't great for cache.
long long sumMatrixBasic(const std::vector<std::vector<int>>& matrix) {
    long long sum = 0;
    for (int i = 0; i < SIZE; ++i) {
        for (int j = 0; j < SIZE; ++j) {
            sum = add(sum, getElement(matrix, i, j));
        }
    }
    return sum;
}

// --- optimized version ---
//
// what i changed and why:
//
// 1) got rid of getElement() and add() - these are trivial one-liners
//    that don't need to be their own functions. calling a function ~16M
//    times when all it does is return a+b is just wasted cycles on stack
//    setup and teardown. basically manually inlining them.
//
// 2) used .data() to get a raw pointer to each row, then just walk through
//    it linearly. vector<vector<int>> allocates each row separately on
//    the heap, so the rows can be anywhere in memory. but within a single
//    row the ints are contiguous, so if we scan left to right with a pointer
//    we get nice sequential memory access and the prefetcher can do its thing.
//    also avoids the double indirection of matrix[i][j] on every access.
//
// 3) accumulate each row into its own local sum (rowSum) before adding to
//    total. this keeps the dependency chain shorter - the CPU doesn't have to
//    wait on the global accumulator every iteration. also helps the compiler
//    auto-vectorize since rowSum is a simple local variable.
//
// 4) unrolled the inner loop by 4. this means fewer loop condition checks
//    and fewer increments of j. with 4 adds grouped together the CPU can
//    pipeline them better since they're independent of each other. the
//    cleanup loop at the end handles cases where SIZE isn't divisible by 4
//    (doesn't matter here since 4096 % 4 == 0 but good to be safe).
//
long long sumMatrixOptimized(const std::vector<std::vector<int>>& matrix) {
    long long total = 0;

    for (int i = 0; i < SIZE; ++i) {
        const int* row = matrix[i].data(); // pointer to start of this row
        long long rowSum = 0;

        // unrolled by 4
        int j = 0;
        const int unrolled = SIZE - (SIZE % 4);
        for (; j < unrolled; j += 4) {
            rowSum += row[j]
                    + row[j + 1]
                    + row[j + 2]
                    + row[j + 3];
        }
        // leftovers
        for (; j < SIZE; ++j) {
            rowSum += row[j];
        }

        total += rowSum;
    }
    return total;
}

int main() {
    // set up random matrix
    std::vector<std::vector<int>> matrix(SIZE, std::vector<int>(SIZE));
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distrib(-100, 100);
    for (int i = 0; i < SIZE; ++i)
        for (int j = 0; j < SIZE; ++j)
            matrix[i][j] = distrib(gen);

    // time the basic version
    auto start = std::chrono::high_resolution_clock::now();
    long long sum = sumMatrixBasic(matrix);
    auto end = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    std::cout << "Basic Sum:      " << sum << std::endl;
    std::cout << "Basic Time:     " << duration.count() << " ms" << std::endl;

    // time the optimized version
    auto start_opt = std::chrono::high_resolution_clock::now();
    long long optimized_sum = sumMatrixOptimized(matrix);
    auto end_opt = std::chrono::high_resolution_clock::now();
    auto duration_opt = std::chrono::duration_cast<std::chrono::milliseconds>(end_opt - start_opt);

    std::cout << "Optimized Sum:  " << optimized_sum << std::endl;
    std::cout << "Optimized Time: " << duration_opt.count() << " ms" << std::endl;

    // make sure both give the same answer
    if (sum == optimized_sum) {
        std::cout << "\nCorrectness: PASS (sums match)" << std::endl;
    } else {
        std::cout << "\nCorrectness: FAIL (sums differ!)" << std::endl;
    }

    if (duration_opt.count() > 0) {
        double speedup = static_cast<double>(duration.count()) / duration_opt.count();
        std::cout << "Speedup:        " << speedup << "x" << std::endl;
    }

    return 0;
}
