#include "linalg.h"
#include <cstdlib>
#include <cstring>
#include <algorithm>

// mat-vec, row-major: matrix[i][j] = matrix[i*cols + j]
void multiply_mv_row_major(const double* matrix, int rows, int cols,
                           const double* vector, double* result) {
    if (!matrix || !vector || !result) return;

    for (int i = 0; i < rows; i++) {
        double sum = 0.0;
        for (int j = 0; j < cols; j++) {
            sum += matrix[i * cols + j] * vector[j];
        }
        result[i] = sum;
    }
}

// mat-vec, column-major: matrix[i][j] = matrix[j*rows + i]
// iterate by column so we at least walk memory sequentially within each col
void multiply_mv_col_major(const double* matrix, int rows, int cols,
                           const double* vector, double* result) {
    if (!matrix || !vector || !result) return;

    for (int i = 0; i < rows; i++) result[i] = 0.0;

    for (int j = 0; j < cols; j++) {
        double vj = vector[j];
        for (int i = 0; i < rows; i++) {
            result[i] += matrix[j * rows + i] * vj;
        }
    }
}

// naive ijk matmul, everything row-major
// B gets accessed column-wise in the inner loop which is bad for cache
void multiply_mm_naive(const double* matrixA, int rowsA, int colsA,
                       const double* matrixB, int rowsB, int colsB,
                       double* result) {
    if (!matrixA || !matrixB || !result) return;
    if (colsA != rowsB) return;

    std::memset(result, 0, rowsA * colsB * sizeof(double));

    for (int i = 0; i < rowsA; i++) {
        for (int j = 0; j < colsB; j++) {
            double sum = 0.0;
            for (int k = 0; k < colsA; k++) {
                sum += matrixA[i * colsA + k] * matrixB[k * colsB + j];
            }
            result[i * colsB + j] = sum;
        }
    }
}

// matmul with B pre-transposed so both A and B^T are walked row-wise
// C[i][j] = sum_k A[i*colsA + k] * B^T[j*rowsB + k]
void multiply_mm_transposed_b(const double* matrixA, int rowsA, int colsA,
                              const double* matrixB_transposed, int rowsB, int colsB,
                              double* result) {
    if (!matrixA || !matrixB_transposed || !result) return;
    if (colsA != rowsB) return;

    for (int i = 0; i < rowsA; i++) {
        for (int j = 0; j < colsB; j++) {
            double sum = 0.0;
            for (int k = 0; k < colsA; k++) {
                sum += matrixA[i * colsA + k] * matrixB_transposed[j * rowsB + k];
            }
            result[i * colsB + j] = sum;
        }
    }
}

// tiled matmul - break into blocks that fit in L1, use ikj loop order
// so A[i][k] gets loaded once and reused across the j sweep
static const int BLOCK_SIZE = 32;

void multiply_mm_tiled(const double* matrixA, int rowsA, int colsA,
                       const double* matrixB, int rowsB, int colsB,
                       double* result) {
    if (!matrixA || !matrixB || !result) return;
    if (colsA != rowsB) return;

    std::memset(result, 0, rowsA * colsB * sizeof(double));

    for (int ii = 0; ii < rowsA; ii += BLOCK_SIZE) {
        for (int kk = 0; kk < colsA; kk += BLOCK_SIZE) {
            for (int jj = 0; jj < colsB; jj += BLOCK_SIZE) {

                int i_end = std::min(ii + BLOCK_SIZE, rowsA);
                int k_end = std::min(kk + BLOCK_SIZE, colsA);
                int j_end = std::min(jj + BLOCK_SIZE, colsB);

                for (int i = ii; i < i_end; i++) {
                    for (int k = kk; k < k_end; k++) {
                        double a_ik = matrixA[i * colsA + k];
                        for (int j = jj; j < j_end; j++) {
                            result[i * colsB + j] += a_ik * matrixB[k * colsB + j];
                        }
                    }
                }
            }
        }
    }
}

double* alloc_aligned(size_t count, size_t alignment) {
    size_t bytes = count * sizeof(double);
    // aligned_alloc needs size to be a multiple of alignment
    size_t padded = ((bytes + alignment - 1) / alignment) * alignment;
    void* ptr = std::aligned_alloc(alignment, padded);
    if (!ptr) return nullptr;
    return static_cast<double*>(ptr);
}

void free_aligned(double* ptr) {
    std::free(ptr);
}

// not marked inline on purpose - at -O0 these stay as real function calls,
// at -O3 the compiler inlines them anyway
double mul_elem(double a, double b) { return a * b; }
double add_elem(double a, double b) { return a + b; }

void multiply_mv_row_major_helpers(const double* matrix, int rows, int cols,
                                   const double* vector, double* result) {
    if (!matrix || !vector || !result) return;

    for (int i = 0; i < rows; i++) {
        double sum = 0.0;
        for (int j = 0; j < cols; j++) {
            sum = add_elem(sum, mul_elem(matrix[i * cols + j], vector[j]));
        }
        result[i] = sum;
    }
}

void transpose(const double* src, int rows, int cols, double* dst) {
    for (int i = 0; i < rows; i++)
        for (int j = 0; j < cols; j++)
            dst[j * rows + i] = src[i * cols + j];
}
