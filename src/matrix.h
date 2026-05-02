#ifndef CURRY_MATRIX_H
#define CURRY_MATRIX_H

/*
 * Matrices and tensors for Curry Scheme.
 *
 * Matrix: a 2D array of doubles stored in row-major order.
 * Tensor: an N-dimensional array of doubles, also row-major.
 *
 * Both types are GC-atomic (contain no val_t pointers; only doubles and
 * uint32_t shape information), so they use gc_alloc_atomic().
 *
 * These types use a separate namespace (mat-*, tensor-*) rather than
 * hooking into + and *, to avoid confusing dimension-mismatch errors.
 *
 * Scheme API:
 *   Matrices:
 *     (make-matrix rows cols)          -> zero matrix
 *     (make-matrix rows cols fill)     -> filled matrix
 *     (matrix-identity n)              -> n×n identity
 *     (matrix rows cols lst)           -> from flat list of values
 *     (matrix? v)
 *     (matrix-rows m)
 *     (matrix-cols m)
 *     (matrix-ref m i j)              ; 0-based
 *     (matrix-set! m i j v)
 *     (matrix-copy m)
 *     (mat+ a b)                       -> element-wise add
 *     (mat- a b) / (mat- a)            -> element-wise sub / negate
 *     (mat* a b)                       -> matrix product
 *     (mat-scale m s)                  -> scalar multiply
 *     (mat-transpose m)
 *     (mat-map f m)                    -> apply f to each element
 *     (mat-fold f init m)              -> fold over elements row-major
 *     (mat-row m i)                    -> row as list
 *     (mat-col m j)                    -> column as list
 *     (mat->list m)                    -> nested list of rows
 *     (mat-trace m)                    -> sum of diagonal
 *     (mat-frobenius m)                -> Frobenius norm sqrt(sum x²)
 *
 *   Tensors:
 *     (make-tensor shape)              -> zero tensor; shape is a list of dims
 *     (make-tensor shape fill)
 *     (tensor? v)
 *     (tensor-shape t)                 -> list of dimensions
 *     (tensor-ndim t)                  -> number of dimensions
 *     (tensor-size t)                  -> total number of elements
 *     (tensor-ref t i0 i1 ...)         -> get element (0-based indices)
 *     (tensor-set! t i0 i1 ... v)      -> set element
 *     (tensor-copy t)
 *     (tensor+ a b)                    -> element-wise add
 *     (tensor- a b) / (tensor- a)      -> element-wise sub / negate
 *     (tensor-scale t s)
 *     (tensor-map f t)
 *     (tensor->list t)                 -> nested lists matching shape
 *     (tensor-reshape t shape)         -> new tensor with same data, new shape
 *     (tensor-outer a b)               -> outer product; shape = concat shapes
 *     (matrix->tensor m)               -> 2D tensor from matrix
 *     (tensor->matrix t)               -> matrix from 2D tensor
 */

#include "value.h"
#include <stdint.h>

/* ---- Matrix operations ---- */

/* Allocate a rows×cols zero matrix */
val_t mat_make(uint32_t rows, uint32_t cols);

/* Arithmetic */
val_t mat_add(val_t a, val_t b);
val_t mat_sub(val_t a, val_t b);
val_t mat_neg(val_t a);
val_t mat_mul(val_t a, val_t b);     /* matrix product */
val_t mat_scale(val_t m, double s);
val_t mat_transpose(val_t m);

/* Norms */
double mat_trace_d(val_t m);
double mat_frobenius_d(val_t m);

/* Write a matrix to a port */
void mat_write(val_t v, val_t port);

/* ---- Tensor operations ---- */

/* Allocate a zero tensor with given shape (C array of ndim dims) */
val_t tensor_make(uint32_t ndim, const uint32_t *dims);

/* Arithmetic */
val_t tensor_add(val_t a, val_t b);
val_t tensor_sub(val_t a, val_t b);
val_t tensor_neg(val_t a);
val_t tensor_scale(val_t t, double s);
val_t tensor_outer(val_t a, val_t b);
val_t tensor_reshape(val_t t, uint32_t ndim, const uint32_t *new_dims);

/* Write a tensor to a port */
void tensor_write(val_t v, val_t port);

/* ---- Register all Scheme builtins ---- */
void mat_register_builtins(val_t env);

#endif /* CURRY_MATRIX_H */
