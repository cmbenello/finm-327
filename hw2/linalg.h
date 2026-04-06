#ifndef LINALG_H
#define LINALG_H

#include <cstddef>

void multiply_mv_row_major(const double* matrix, int rows, int cols,
                           const double* vector, double* result);

void multiply_mv_col_major(const double* matrix, int rows, int cols,
                           const double* vector, double* result);

void multiply_mm_naive(const double* matrixA, int rowsA, int colsA,
                       const double* matrixB, int rowsB, int colsB,
                       double* result);

void multiply_mm_transposed_b(const double* matrixA, int rowsA, int colsA,
                              const double* matrixB_transposed, int rowsB, int colsB,
                              double* result);

void multiply_mm_tiled(const double* matrixA, int rowsA, int colsA,
                       const double* matrixB, int rowsB, int colsB,
                       double* result);

double* alloc_aligned(size_t count, size_t alignment = 64);
void free_aligned(double* ptr);

double mul_elem(double a, double b);
double add_elem(double a, double b);

// same as multiply_mv_row_major but goes through helper funcs (for inlining experiment)
void multiply_mv_row_major_helpers(const double* matrix, int rows, int cols,
                                   const double* vector, double* result);

void transpose(const double* src, int rows, int cols, double* dst);

#endif
