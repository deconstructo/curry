#include "matrix.h"
#include "object.h"
#include "gc.h"
#include "numeric.h"
#include "port.h"
#include "env.h"
#include "eval.h"
#include "symbol.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

extern void scm_raise(val_t kind, const char *fmt, ...) __attribute__((noreturn));

/* =========================================================================
 * Memory layout
 *
 * Matrix is defined in object.h as:
 *   typedef struct { Hdr hdr; uint32_t rows, cols; double data[]; } Matrix;
 *
 * Tensor is defined in object.h as:
 *   typedef struct { Hdr hdr; uint32_t ndim, size; uint32_t dims[]; } Tensor;
 *   with double data[] packed immediately after dims[ndim].
 * ========================================================================= */

/* Tensor data pointer: packed after dims[ndim] */
static inline double *tensor_data(Tensor *t) {
    return (double *)(t->dims + t->ndim);
}

/* =========================================================================
 * Matrix allocation & helpers
 * ========================================================================= */

val_t mat_make(uint32_t rows, uint32_t cols) {
    if (rows == 0 || cols == 0)
        scm_raise(V_FALSE, "make-matrix: dimensions must be positive (got %ux%u)", rows, cols);
    Matrix *m = (Matrix *)gc_alloc_atomic(sizeof(Matrix) + (size_t)rows * cols * sizeof(double));
    m->hdr.type  = T_MATRIX;
    m->hdr.flags = 0;
    m->rows = rows;
    m->cols = cols;
    memset(m->data, 0, (size_t)rows * cols * sizeof(double));
    return vptr(m);
}

static Matrix *mat_clone(val_t v) {
    Matrix *src = as_matrix(v);
    size_t  nb  = (size_t)src->rows * src->cols * sizeof(double);
    Matrix *dst = (Matrix *)gc_alloc_atomic(sizeof(Matrix) + nb);
    memcpy(dst, src, sizeof(Matrix) + nb);
    return dst;
}

static void check_same_shape_mat(val_t a, val_t b, const char *op) {
    Matrix *ma = as_matrix(a), *mb = as_matrix(b);
    if (ma->rows != mb->rows || ma->cols != mb->cols)
        scm_raise(V_FALSE, "%s: shape mismatch (%ux%u vs %ux%u)",
                  op, ma->rows, ma->cols, mb->rows, mb->cols);
}

/* =========================================================================
 * Matrix arithmetic
 * ========================================================================= */

val_t mat_add(val_t a, val_t b) {
    check_same_shape_mat(a, b, "mat+");
    Matrix *ma = as_matrix(a), *mb = as_matrix(b);
    Matrix *res = mat_clone(a);
    size_t n = (size_t)res->rows * res->cols;
    for (size_t i = 0; i < n; i++) res->data[i] = ma->data[i] + mb->data[i];
    return vptr(res);
}

val_t mat_sub(val_t a, val_t b) {
    check_same_shape_mat(a, b, "mat-");
    Matrix *ma = as_matrix(a), *mb = as_matrix(b);
    Matrix *res = mat_clone(a);
    size_t n = (size_t)res->rows * res->cols;
    for (size_t i = 0; i < n; i++) res->data[i] = ma->data[i] - mb->data[i];
    return vptr(res);
}

val_t mat_neg(val_t a) {
    Matrix *res = mat_clone(a);
    size_t n = (size_t)res->rows * res->cols;
    for (size_t i = 0; i < n; i++) res->data[i] = -res->data[i];
    return vptr(res);
}

val_t mat_mul(val_t a, val_t b) {
    Matrix *ma = as_matrix(a), *mb = as_matrix(b);
    if (ma->cols != mb->rows)
        scm_raise(V_FALSE, "mat*: incompatible dimensions (%ux%u) * (%ux%u)",
                  ma->rows, ma->cols, mb->rows, mb->cols);
    val_t rv = mat_make(ma->rows, mb->cols);
    Matrix *res = as_matrix(rv);
    for (uint32_t i = 0; i < ma->rows; i++) {
        for (uint32_t j = 0; j < mb->cols; j++) {
            double sum = 0.0;
            for (uint32_t k = 0; k < ma->cols; k++)
                sum += ma->data[i * ma->cols + k] * mb->data[k * mb->cols + j];
            res->data[i * res->cols + j] = sum;
        }
    }
    return rv;
}

val_t mat_scale(val_t m, double s) {
    Matrix *res = mat_clone(m);
    size_t n = (size_t)res->rows * res->cols;
    for (size_t i = 0; i < n; i++) res->data[i] *= s;
    return vptr(res);
}

val_t mat_transpose(val_t m) {
    Matrix *src = as_matrix(m);
    val_t rv = mat_make(src->cols, src->rows);
    Matrix *dst = as_matrix(rv);
    for (uint32_t i = 0; i < src->rows; i++)
        for (uint32_t j = 0; j < src->cols; j++)
            dst->data[j * dst->cols + i] = src->data[i * src->cols + j];
    return rv;
}

double mat_trace_d(val_t m) {
    Matrix *mat = as_matrix(m);
    uint32_t diag = mat->rows < mat->cols ? mat->rows : mat->cols;
    double sum = 0.0;
    for (uint32_t i = 0; i < diag; i++) sum += mat->data[i * mat->cols + i];
    return sum;
}

double mat_frobenius_d(val_t m) {
    Matrix *mat = as_matrix(m);
    size_t n = (size_t)mat->rows * mat->cols;
    double sum = 0.0;
    for (size_t i = 0; i < n; i++) sum += mat->data[i] * mat->data[i];
    return sqrt(sum);
}

/* =========================================================================
 * Matrix display
 * ========================================================================= */

void mat_write(val_t v, val_t port) {
    Matrix *m = as_matrix(v);
    char buf[64];
    int len = snprintf(buf, sizeof(buf), "#<matrix %ux%u", m->rows, m->cols);
    port_write_string(port, buf, (uint32_t)len);
    port_write_string(port, " [", 2);
    for (uint32_t i = 0; i < m->rows; i++) {
        if (i > 0) port_write_string(port, " | ", 3);
        for (uint32_t j = 0; j < m->cols; j++) {
            if (j > 0) port_write_char(port, ' ');
            len = snprintf(buf, sizeof(buf), "%g", m->data[i * m->cols + j]);
            port_write_string(port, buf, (uint32_t)len);
        }
    }
    port_write_string(port, "]>", 2);
}

/* =========================================================================
 * Tensor allocation & helpers
 * ========================================================================= */

val_t tensor_make(uint32_t ndim, const uint32_t *dims) {
    if (ndim == 0) scm_raise(V_FALSE, "make-tensor: ndim must be positive");
    uint32_t size = 1;
    for (uint32_t i = 0; i < ndim; i++) {
        if (dims[i] == 0)
            scm_raise(V_FALSE, "make-tensor: dimension %u is zero", i);
        if (size > 0x7fffffff / dims[i])
            scm_raise(V_FALSE, "make-tensor: tensor too large");
        size *= dims[i];
    }
    /* Layout: Tensor header + dims[ndim] uint32_t + size doubles */
    size_t nb = sizeof(Tensor) + ndim * sizeof(uint32_t) + size * sizeof(double);
    Tensor *t = (Tensor *)gc_alloc_atomic(nb);
    t->hdr.type  = T_TENSOR;
    t->hdr.flags = 0;
    t->ndim = ndim;
    t->size = size;
    memcpy(t->dims, dims, ndim * sizeof(uint32_t));
    memset(tensor_data(t), 0, size * sizeof(double));
    return vptr(t);
}

static Tensor *tensor_clone_raw(val_t v) {
    Tensor *src = as_tensor(v);
    size_t nb = sizeof(Tensor) + src->ndim * sizeof(uint32_t) + src->size * sizeof(double);
    Tensor *dst = (Tensor *)gc_alloc_atomic(nb);
    memcpy(dst, src, nb);
    return dst;
}

/* Compute flat index from an array of per-dimension indices */
static uint32_t tensor_flat_index(Tensor *t, const uint32_t *idx) {
    uint32_t flat = 0;
    for (uint32_t d = 0; d < t->ndim; d++) {
        if (idx[d] >= t->dims[d])
            scm_raise(V_FALSE, "tensor-ref: index %u out of range for dim %u (size %u)",
                      idx[d], d, t->dims[d]);
        flat = flat * t->dims[d] + idx[d];
    }
    return flat;
}

static void check_same_shape_tensor(val_t a, val_t b, const char *op) {
    Tensor *ta = as_tensor(a), *tb = as_tensor(b);
    if (ta->ndim != tb->ndim || ta->size != tb->size)
        goto mismatch;
    for (uint32_t i = 0; i < ta->ndim; i++)
        if (ta->dims[i] != tb->dims[i]) goto mismatch;
    return;
mismatch:
    scm_raise(V_FALSE, "%s: shape mismatch", op);
}

/* =========================================================================
 * Tensor arithmetic
 * ========================================================================= */

val_t tensor_add(val_t a, val_t b) {
    check_same_shape_tensor(a, b, "tensor+");
    Tensor *ta = as_tensor(a), *tb = as_tensor(b);
    Tensor *res = tensor_clone_raw(a);
    double *da = tensor_data(ta), *db = tensor_data(tb), *dr = tensor_data(res);
    for (uint32_t i = 0; i < res->size; i++) dr[i] = da[i] + db[i];
    return vptr(res);
}

val_t tensor_sub(val_t a, val_t b) {
    check_same_shape_tensor(a, b, "tensor-");
    Tensor *ta = as_tensor(a), *tb = as_tensor(b);
    Tensor *res = tensor_clone_raw(a);
    double *da = tensor_data(ta), *db = tensor_data(tb), *dr = tensor_data(res);
    for (uint32_t i = 0; i < res->size; i++) dr[i] = da[i] - db[i];
    return vptr(res);
}

val_t tensor_neg(val_t a) {
    Tensor *res = tensor_clone_raw(a);
    double *dr = tensor_data(res);
    for (uint32_t i = 0; i < res->size; i++) dr[i] = -dr[i];
    return vptr(res);
}

val_t tensor_scale(val_t t, double s) {
    Tensor *res = tensor_clone_raw(t);
    double *dr = tensor_data(res);
    for (uint32_t i = 0; i < res->size; i++) dr[i] *= s;
    return vptr(res);
}

val_t tensor_outer(val_t a, val_t b) {
    Tensor *ta = as_tensor(a), *tb = as_tensor(b);
    /* Result shape: concat(ta->dims, tb->dims) */
    uint32_t ndim = ta->ndim + tb->ndim;
    uint32_t *dims = (uint32_t *)gc_alloc_atomic(ndim * sizeof(uint32_t));
    memcpy(dims,            ta->dims, ta->ndim * sizeof(uint32_t));
    memcpy(dims + ta->ndim, tb->dims, tb->ndim * sizeof(uint32_t));
    val_t rv = tensor_make(ndim, dims);
    Tensor *res = as_tensor(rv);
    double *da = tensor_data(ta), *db = tensor_data(tb), *dr = tensor_data(res);
    for (uint32_t i = 0; i < ta->size; i++)
        for (uint32_t j = 0; j < tb->size; j++)
            dr[i * tb->size + j] = da[i] * db[j];
    return rv;
}

val_t tensor_reshape(val_t t, uint32_t ndim, const uint32_t *new_dims) {
    Tensor *src = as_tensor(t);
    uint32_t new_size = 1;
    for (uint32_t i = 0; i < ndim; i++) {
        if (new_dims[i] == 0)
            scm_raise(V_FALSE, "tensor-reshape: dimension %u is zero", i);
        new_size *= new_dims[i];
    }
    if (new_size != src->size)
        scm_raise(V_FALSE, "tensor-reshape: size mismatch (%u vs %u)", new_size, src->size);
    size_t nb = sizeof(Tensor) + ndim * sizeof(uint32_t) + new_size * sizeof(double);
    Tensor *dst = (Tensor *)gc_alloc_atomic(nb);
    dst->hdr.type  = T_TENSOR;
    dst->hdr.flags = 0;
    dst->ndim = ndim;
    dst->size = new_size;
    memcpy(dst->dims, new_dims, ndim * sizeof(uint32_t));
    memcpy(tensor_data(dst), tensor_data(src), new_size * sizeof(double));
    return vptr(dst);
}

/* =========================================================================
 * Tensor display
 * ========================================================================= */

/* Recursively print nested lists for the given dimension range */
static void tensor_write_dim(val_t port, Tensor *t, uint32_t dim,
                              uint32_t *idx, char *buf, size_t bufsz) {
    if (dim == t->ndim) {
        int len = snprintf(buf, bufsz, "%g", tensor_data(t)[tensor_flat_index(t, idx)]);
        port_write_string(port, buf, (uint32_t)len);
        return;
    }
    port_write_char(port, '(');
    for (uint32_t i = 0; i < t->dims[dim]; i++) {
        if (i > 0) port_write_char(port, ' ');
        idx[dim] = i;
        tensor_write_dim(port, t, dim + 1, idx, buf, bufsz);
    }
    port_write_char(port, ')');
}

void tensor_write(val_t v, val_t port) {
    Tensor *t = as_tensor(v);
    char buf[64];
    int len = snprintf(buf, sizeof(buf), "#<tensor ");
    port_write_string(port, buf, (uint32_t)len);
    for (uint32_t i = 0; i < t->ndim; i++) {
        if (i > 0) port_write_char(port, 'x');
        len = snprintf(buf, sizeof(buf), "%u", t->dims[i]);
        port_write_string(port, buf, (uint32_t)len);
    }
    port_write_char(port, ' ');
    uint32_t *idx = (uint32_t *)gc_alloc_atomic(t->ndim * sizeof(uint32_t));
    memset(idx, 0, t->ndim * sizeof(uint32_t));
    tensor_write_dim(port, t, 0, idx, buf, sizeof(buf));
    port_write_char(port, '>');
}

/* =========================================================================
 * Scheme builtins — helpers
 * ========================================================================= */

static val_t mat_cons(val_t car, val_t cdr) {
    Pair *p = CURRY_NEW(Pair);
    p->hdr.type = T_PAIR; p->hdr.flags = 0;
    p->car = car; p->cdr = cdr;
    return vptr(p);
}

static void mat_def(val_t env, const char *name, PrimFn fn, int mn, int mx) {
    Primitive *p = CURRY_NEW(Primitive);
    p->hdr.type = T_PRIMITIVE; p->hdr.flags = 0;
    p->name = name; p->fn = fn; p->min_args = mn; p->max_args = mx; p->ud = NULL;
    env_define(env, sym_intern_cstr(name), vptr(p));
}

/* Parse a Scheme list of fixnums into a C array; returns count */
static uint32_t list_to_dims(val_t lst, uint32_t *out, uint32_t maxn, const char *who) {
    uint32_t n = 0;
    for (val_t p = lst; vis_pair(p); p = vcdr(p)) {
        if (n >= maxn)
            scm_raise(V_FALSE, "%s: too many dimensions (max %u)", who, maxn);
        val_t d = vcar(p);
        if (!vis_fixnum(d) || vunfix(d) <= 0)
            scm_raise(V_FALSE, "%s: dimension must be a positive integer", who);
        out[n++] = (uint32_t)vunfix(d);
    }
    if (n == 0) scm_raise(V_FALSE, "%s: shape list is empty", who);
    return n;
}

/* =========================================================================
 * Matrix builtins
 * ========================================================================= */

static val_t prim_make_matrix(int argc, val_t *av, void *ud) {
    (void)ud;
    if (!vis_fixnum(av[0]) || vunfix(av[0]) <= 0)
        scm_raise(V_FALSE, "make-matrix: rows must be a positive integer");
    if (!vis_fixnum(av[1]) || vunfix(av[1]) <= 0)
        scm_raise(V_FALSE, "make-matrix: cols must be a positive integer");
    uint32_t rows = (uint32_t)vunfix(av[0]);
    uint32_t cols = (uint32_t)vunfix(av[1]);
    val_t rv = mat_make(rows, cols);
    if (argc == 3) {
        double fill = num_to_double(av[2]);
        Matrix *m = as_matrix(rv);
        size_t n = (size_t)rows * cols;
        for (size_t i = 0; i < n; i++) m->data[i] = fill;
    }
    return rv;
}

static val_t prim_matrix_identity(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_fixnum(av[0]) || vunfix(av[0]) <= 0)
        scm_raise(V_FALSE, "matrix-identity: n must be a positive integer");
    uint32_t n = (uint32_t)vunfix(av[0]);
    val_t rv = mat_make(n, n);
    Matrix *m = as_matrix(rv);
    for (uint32_t i = 0; i < n; i++) m->data[i * n + i] = 1.0;
    return rv;
}

/* (matrix rows cols lst) — from flat list of values */
static val_t prim_matrix_from_list(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_fixnum(av[0]) || vunfix(av[0]) <= 0)
        scm_raise(V_FALSE, "matrix: rows must be a positive integer");
    if (!vis_fixnum(av[1]) || vunfix(av[1]) <= 0)
        scm_raise(V_FALSE, "matrix: cols must be a positive integer");
    uint32_t rows = (uint32_t)vunfix(av[0]);
    uint32_t cols = (uint32_t)vunfix(av[1]);
    val_t rv = mat_make(rows, cols);
    Matrix *m = as_matrix(rv);
    uint32_t i = 0, total = rows * cols;
    for (val_t p = av[2]; vis_pair(p) && i < total; p = vcdr(p))
        m->data[i++] = num_to_double(vcar(p));
    return rv;
}

static val_t prim_matrix_p(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    return vis_matrix(av[0]) ? V_TRUE : V_FALSE;
}

static val_t prim_matrix_rows(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_matrix(av[0])) scm_raise(V_FALSE, "matrix-rows: not a matrix");
    return vfix((intptr_t)as_matrix(av[0])->rows);
}

static val_t prim_matrix_cols(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_matrix(av[0])) scm_raise(V_FALSE, "matrix-cols: not a matrix");
    return vfix((intptr_t)as_matrix(av[0])->cols);
}

static val_t prim_matrix_ref(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_matrix(av[0])) scm_raise(V_FALSE, "matrix-ref: not a matrix");
    if (!vis_fixnum(av[1])) scm_raise(V_FALSE, "matrix-ref: row must be an integer");
    if (!vis_fixnum(av[2])) scm_raise(V_FALSE, "matrix-ref: col must be an integer");
    Matrix *m = as_matrix(av[0]);
    uint32_t i = (uint32_t)vunfix(av[1]);
    uint32_t j = (uint32_t)vunfix(av[2]);
    if (i >= m->rows || j >= m->cols)
        scm_raise(V_FALSE, "matrix-ref: index (%u,%u) out of range for %ux%u matrix", i, j, m->rows, m->cols);
    return num_make_float(m->data[i * m->cols + j]);
}

static val_t prim_matrix_set(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_matrix(av[0])) scm_raise(V_FALSE, "matrix-set!: not a matrix");
    if (!vis_fixnum(av[1])) scm_raise(V_FALSE, "matrix-set!: row must be an integer");
    if (!vis_fixnum(av[2])) scm_raise(V_FALSE, "matrix-set!: col must be an integer");
    Matrix *m = as_matrix(av[0]);
    uint32_t i = (uint32_t)vunfix(av[1]);
    uint32_t j = (uint32_t)vunfix(av[2]);
    if (i >= m->rows || j >= m->cols)
        scm_raise(V_FALSE, "matrix-set!: index (%u,%u) out of range for %ux%u matrix", i, j, m->rows, m->cols);
    m->data[i * m->cols + j] = num_to_double(av[3]);
    return V_VOID;
}

static val_t prim_matrix_copy(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_matrix(av[0])) scm_raise(V_FALSE, "matrix-copy: not a matrix");
    return vptr(mat_clone(av[0]));
}

static val_t prim_mat_add(int argc, val_t *av, void *ud) {
    (void)ud;
    if (argc == 0) scm_raise(V_FALSE, "mat+: requires at least one argument");
    if (!vis_matrix(av[0])) scm_raise(V_FALSE, "mat+: not a matrix");
    val_t acc = av[0];
    for (int i = 1; i < argc; i++) {
        if (!vis_matrix(av[i])) scm_raise(V_FALSE, "mat+: not a matrix");
        acc = mat_add(acc, av[i]);
    }
    return acc;
}

static val_t prim_mat_sub(int argc, val_t *av, void *ud) {
    (void)ud;
    if (argc == 0) scm_raise(V_FALSE, "mat-: requires at least one argument");
    if (!vis_matrix(av[0])) scm_raise(V_FALSE, "mat-: not a matrix");
    if (argc == 1) return mat_neg(av[0]);
    val_t acc = av[0];
    for (int i = 1; i < argc; i++) {
        if (!vis_matrix(av[i])) scm_raise(V_FALSE, "mat-: not a matrix");
        acc = mat_sub(acc, av[i]);
    }
    return acc;
}

static val_t prim_mat_mul(int argc, val_t *av, void *ud) {
    (void)ud;
    if (argc == 0) scm_raise(V_FALSE, "mat*: requires at least one argument");
    if (!vis_matrix(av[0])) scm_raise(V_FALSE, "mat*: not a matrix");
    val_t acc = av[0];
    for (int i = 1; i < argc; i++) {
        if (!vis_matrix(av[i])) scm_raise(V_FALSE, "mat*: not a matrix");
        acc = mat_mul(acc, av[i]);
    }
    return acc;
}

static val_t prim_mat_scale(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_matrix(av[0])) scm_raise(V_FALSE, "mat-scale: not a matrix");
    return mat_scale(av[0], num_to_double(av[1]));
}

static val_t prim_mat_transpose(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_matrix(av[0])) scm_raise(V_FALSE, "mat-transpose: not a matrix");
    return mat_transpose(av[0]);
}

static val_t prim_mat_map(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    /* av[0] = proc, av[1] = matrix */
    if (!vis_proc(av[0])) scm_raise(V_FALSE, "mat-map: first argument must be a procedure");
    if (!vis_matrix(av[1])) scm_raise(V_FALSE, "mat-map: second argument must be a matrix");
    Matrix *src = as_matrix(av[1]);
    val_t rv = mat_make(src->rows, src->cols);
    Matrix *dst = as_matrix(rv);
    size_t n = (size_t)src->rows * src->cols;
    for (size_t i = 0; i < n; i++) {
        val_t arg = num_make_float(src->data[i]);
        val_t result = apply(av[0], mat_cons(arg, V_NIL));
        dst->data[i] = num_to_double(result);
    }
    return rv;
}

static val_t prim_mat_fold(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_proc(av[0]))   scm_raise(V_FALSE, "mat-fold: first argument must be a procedure");
    if (!vis_matrix(av[2])) scm_raise(V_FALSE, "mat-fold: third argument must be a matrix");
    Matrix *m = as_matrix(av[2]);
    val_t acc = av[1];
    size_t n = (size_t)m->rows * m->cols;
    for (size_t i = 0; i < n; i++) {
        val_t arg = num_make_float(m->data[i]);
        acc = apply(av[0], mat_cons(acc, mat_cons(arg, V_NIL)));
    }
    return acc;
}

static val_t prim_mat_row(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_matrix(av[0])) scm_raise(V_FALSE, "mat-row: not a matrix");
    if (!vis_fixnum(av[1])) scm_raise(V_FALSE, "mat-row: row index must be an integer");
    Matrix *m = as_matrix(av[0]);
    uint32_t i = (uint32_t)vunfix(av[1]);
    if (i >= m->rows) scm_raise(V_FALSE, "mat-row: row %u out of range", i);
    val_t lst = V_NIL;
    for (uint32_t j = m->cols; j-- > 0; )
        lst = mat_cons(num_make_float(m->data[i * m->cols + j]), lst);
    return lst;
}

static val_t prim_mat_col(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_matrix(av[0])) scm_raise(V_FALSE, "mat-col: not a matrix");
    if (!vis_fixnum(av[1])) scm_raise(V_FALSE, "mat-col: col index must be an integer");
    Matrix *m = as_matrix(av[0]);
    uint32_t j = (uint32_t)vunfix(av[1]);
    if (j >= m->cols) scm_raise(V_FALSE, "mat-col: col %u out of range", j);
    val_t lst = V_NIL;
    for (uint32_t i = m->rows; i-- > 0; )
        lst = mat_cons(num_make_float(m->data[i * m->cols + j]), lst);
    return lst;
}

static val_t prim_mat_to_list(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_matrix(av[0])) scm_raise(V_FALSE, "mat->list: not a matrix");
    Matrix *m = as_matrix(av[0]);
    val_t rows = V_NIL;
    for (uint32_t i = m->rows; i-- > 0; ) {
        val_t row = V_NIL;
        for (uint32_t j = m->cols; j-- > 0; )
            row = mat_cons(num_make_float(m->data[i * m->cols + j]), row);
        rows = mat_cons(row, rows);
    }
    return rows;
}

static val_t prim_mat_trace(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_matrix(av[0])) scm_raise(V_FALSE, "mat-trace: not a matrix");
    return num_make_float(mat_trace_d(av[0]));
}

static val_t prim_mat_frobenius(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_matrix(av[0])) scm_raise(V_FALSE, "mat-frobenius: not a matrix");
    return num_make_float(mat_frobenius_d(av[0]));
}

/* =========================================================================
 * Tensor builtins
 * ========================================================================= */

#define MAX_TENSOR_DIMS 32

static val_t prim_make_tensor(int argc, val_t *av, void *ud) {
    (void)ud;
    uint32_t dims[MAX_TENSOR_DIMS];
    uint32_t ndim = list_to_dims(av[0], dims, MAX_TENSOR_DIMS, "make-tensor");
    val_t rv = tensor_make(ndim, dims);
    if (argc == 2) {
        double fill = num_to_double(av[1]);
        Tensor *t = as_tensor(rv);
        double *d = tensor_data(t);
        for (uint32_t i = 0; i < t->size; i++) d[i] = fill;
    }
    return rv;
}

static val_t prim_tensor_p(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    return vis_tensor(av[0]) ? V_TRUE : V_FALSE;
}

static val_t prim_tensor_shape(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_tensor(av[0])) scm_raise(V_FALSE, "tensor-shape: not a tensor");
    Tensor *t = as_tensor(av[0]);
    val_t lst = V_NIL;
    for (uint32_t i = t->ndim; i-- > 0; )
        lst = mat_cons(vfix((intptr_t)t->dims[i]), lst);
    return lst;
}

static val_t prim_tensor_ndim(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_tensor(av[0])) scm_raise(V_FALSE, "tensor-ndim: not a tensor");
    return vfix((intptr_t)as_tensor(av[0])->ndim);
}

static val_t prim_tensor_size(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_tensor(av[0])) scm_raise(V_FALSE, "tensor-size: not a tensor");
    return vfix((intptr_t)as_tensor(av[0])->size);
}

/* (tensor-ref t i0 i1 ...) — variadic indices */
static val_t prim_tensor_ref(int argc, val_t *av, void *ud) {
    (void)ud;
    if (!vis_tensor(av[0])) scm_raise(V_FALSE, "tensor-ref: not a tensor");
    Tensor *t = as_tensor(av[0]);
    if ((uint32_t)(argc - 1) != t->ndim)
        scm_raise(V_FALSE, "tensor-ref: expected %u indices, got %d", t->ndim, argc - 1);
    uint32_t idx[MAX_TENSOR_DIMS];
    for (uint32_t d = 0; d < t->ndim; d++) {
        if (!vis_fixnum(av[1 + d]))
            scm_raise(V_FALSE, "tensor-ref: index %u must be an integer", d);
        idx[d] = (uint32_t)vunfix(av[1 + d]);
    }
    return num_make_float(tensor_data(t)[tensor_flat_index(t, idx)]);
}

/* (tensor-set! t i0 i1 ... v) — last argument is the value */
static val_t prim_tensor_set(int argc, val_t *av, void *ud) {
    (void)ud;
    if (!vis_tensor(av[0])) scm_raise(V_FALSE, "tensor-set!: not a tensor");
    Tensor *t = as_tensor(av[0]);
    /* argc = 1 (tensor) + ndim (indices) + 1 (value) */
    if ((uint32_t)(argc - 2) != t->ndim)
        scm_raise(V_FALSE, "tensor-set!: expected %u indices, got %d", t->ndim, argc - 2);
    uint32_t idx[MAX_TENSOR_DIMS];
    for (uint32_t d = 0; d < t->ndim; d++) {
        if (!vis_fixnum(av[1 + d]))
            scm_raise(V_FALSE, "tensor-set!: index %u must be an integer", d);
        idx[d] = (uint32_t)vunfix(av[1 + d]);
    }
    tensor_data(t)[tensor_flat_index(t, idx)] = num_to_double(av[argc - 1]);
    return V_VOID;
}

static val_t prim_tensor_copy(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_tensor(av[0])) scm_raise(V_FALSE, "tensor-copy: not a tensor");
    return vptr(tensor_clone_raw(av[0]));
}

static val_t prim_tensor_add(int argc, val_t *av, void *ud) {
    (void)ud;
    if (argc == 0) scm_raise(V_FALSE, "tensor+: requires at least one argument");
    if (!vis_tensor(av[0])) scm_raise(V_FALSE, "tensor+: not a tensor");
    val_t acc = av[0];
    for (int i = 1; i < argc; i++) {
        if (!vis_tensor(av[i])) scm_raise(V_FALSE, "tensor+: not a tensor");
        acc = tensor_add(acc, av[i]);
    }
    return acc;
}

static val_t prim_tensor_sub(int argc, val_t *av, void *ud) {
    (void)ud;
    if (argc == 0) scm_raise(V_FALSE, "tensor-: requires at least one argument");
    if (!vis_tensor(av[0])) scm_raise(V_FALSE, "tensor-: not a tensor");
    if (argc == 1) return tensor_neg(av[0]);
    val_t acc = av[0];
    for (int i = 1; i < argc; i++) {
        if (!vis_tensor(av[i])) scm_raise(V_FALSE, "tensor-: not a tensor");
        acc = tensor_sub(acc, av[i]);
    }
    return acc;
}

static val_t prim_tensor_scale(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_tensor(av[0])) scm_raise(V_FALSE, "tensor-scale: not a tensor");
    return tensor_scale(av[0], num_to_double(av[1]));
}

static val_t prim_tensor_map(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_proc(av[0]))   scm_raise(V_FALSE, "tensor-map: first argument must be a procedure");
    if (!vis_tensor(av[1])) scm_raise(V_FALSE, "tensor-map: second argument must be a tensor");
    Tensor *src = as_tensor(av[1]);
    val_t rv = tensor_make(src->ndim, src->dims);
    Tensor *dst = as_tensor(rv);
    double *sd = tensor_data(src), *dd = tensor_data(dst);
    for (uint32_t i = 0; i < src->size; i++) {
        val_t arg = num_make_float(sd[i]);
        val_t result = apply(av[0], mat_cons(arg, V_NIL));
        dd[i] = num_to_double(result);
    }
    return rv;
}

static val_t prim_tensor_reshape(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_tensor(av[0])) scm_raise(V_FALSE, "tensor-reshape: not a tensor");
    uint32_t new_dims[MAX_TENSOR_DIMS];
    uint32_t ndim = list_to_dims(av[1], new_dims, MAX_TENSOR_DIMS, "tensor-reshape");
    return tensor_reshape(av[0], ndim, new_dims);
}

static val_t prim_tensor_outer(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_tensor(av[0])) scm_raise(V_FALSE, "tensor-outer: not a tensor");
    if (!vis_tensor(av[1])) scm_raise(V_FALSE, "tensor-outer: not a tensor");
    return tensor_outer(av[0], av[1]);
}

/* Recursively build nested lists from tensor */
static val_t tensor_to_nested(Tensor *t, uint32_t dim, uint32_t *idx) {
    if (dim == t->ndim)
        return num_make_float(tensor_data(t)[tensor_flat_index(t, idx)]);
    val_t lst = V_NIL;
    for (uint32_t i = t->dims[dim]; i-- > 0; ) {
        idx[dim] = i;
        lst = mat_cons(tensor_to_nested(t, dim + 1, idx), lst);
    }
    return lst;
}

static val_t prim_tensor_to_list(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_tensor(av[0])) scm_raise(V_FALSE, "tensor->list: not a tensor");
    Tensor *t = as_tensor(av[0]);
    uint32_t *idx = (uint32_t *)gc_alloc_atomic(t->ndim * sizeof(uint32_t));
    memset(idx, 0, t->ndim * sizeof(uint32_t));
    return tensor_to_nested(t, 0, idx);
}

/* (matrix->tensor m) — wrap a matrix as a 2D tensor */
static val_t prim_matrix_to_tensor(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_matrix(av[0])) scm_raise(V_FALSE, "matrix->tensor: not a matrix");
    Matrix *m = as_matrix(av[0]);
    uint32_t dims[2] = { m->rows, m->cols };
    val_t rv = tensor_make(2, dims);
    Tensor *t = as_tensor(rv);
    memcpy(tensor_data(t), m->data, (size_t)m->rows * m->cols * sizeof(double));
    return rv;
}

/* (tensor->matrix t) — require exactly 2 dimensions */
static val_t prim_tensor_to_matrix(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_tensor(av[0])) scm_raise(V_FALSE, "tensor->matrix: not a tensor");
    Tensor *t = as_tensor(av[0]);
    if (t->ndim != 2)
        scm_raise(V_FALSE, "tensor->matrix: tensor must be 2-dimensional (got %u dims)", t->ndim);
    val_t rv = mat_make(t->dims[0], t->dims[1]);
    Matrix *m = as_matrix(rv);
    memcpy(m->data, tensor_data(t), (size_t)t->size * sizeof(double));
    return rv;
}

/* =========================================================================
 * Registration
 * ========================================================================= */

void mat_register_matrix_builtins(val_t env) {
    mat_def(env, "make-matrix",      prim_make_matrix,      2,  3);
    mat_def(env, "matrix-identity",  prim_matrix_identity,  1,  1);
    mat_def(env, "matrix",           prim_matrix_from_list, 3,  3);
    mat_def(env, "matrix?",          prim_matrix_p,         1,  1);
    mat_def(env, "matrix-rows",      prim_matrix_rows,      1,  1);
    mat_def(env, "matrix-cols",      prim_matrix_cols,      1,  1);
    mat_def(env, "matrix-ref",       prim_matrix_ref,       3,  3);
    mat_def(env, "matrix-set!",      prim_matrix_set,       4,  4);
    mat_def(env, "matrix-copy",      prim_matrix_copy,      1,  1);
    mat_def(env, "mat+",             prim_mat_add,          1, -1);
    mat_def(env, "mat-",             prim_mat_sub,          1, -1);
    mat_def(env, "mat*",             prim_mat_mul,          1, -1);
    mat_def(env, "mat-scale",        prim_mat_scale,        2,  2);
    mat_def(env, "mat-transpose",    prim_mat_transpose,    1,  1);
    mat_def(env, "mat-map",          prim_mat_map,          2,  2);
    mat_def(env, "mat-fold",         prim_mat_fold,         3,  3);
    mat_def(env, "mat-row",          prim_mat_row,          2,  2);
    mat_def(env, "mat-col",          prim_mat_col,          2,  2);
    mat_def(env, "mat->list",        prim_mat_to_list,      1,  1);
    mat_def(env, "mat-trace",        prim_mat_trace,        1,  1);
    mat_def(env, "mat-frobenius",    prim_mat_frobenius,    1,  1);
}

void mat_register_tensor_builtins(val_t env) {
    mat_def(env, "make-tensor",      prim_make_tensor,      1,  2);
    mat_def(env, "tensor?",          prim_tensor_p,         1,  1);
    mat_def(env, "tensor-shape",     prim_tensor_shape,     1,  1);
    mat_def(env, "tensor-ndim",      prim_tensor_ndim,      1,  1);
    mat_def(env, "tensor-size",      prim_tensor_size,      1,  1);
    mat_def(env, "tensor-ref",       prim_tensor_ref,       2, -1);
    mat_def(env, "tensor-set!",      prim_tensor_set,       3, -1);
    mat_def(env, "tensor-copy",      prim_tensor_copy,      1,  1);
    mat_def(env, "tensor+",          prim_tensor_add,       1, -1);
    mat_def(env, "tensor-",          prim_tensor_sub,       1, -1);
    mat_def(env, "tensor-scale",     prim_tensor_scale,     2,  2);
    mat_def(env, "tensor-map",       prim_tensor_map,       2,  2);
    mat_def(env, "tensor-reshape",   prim_tensor_reshape,   2,  2);
    mat_def(env, "tensor-outer",     prim_tensor_outer,     2,  2);
    mat_def(env, "tensor->list",     prim_tensor_to_list,   1,  1);
    mat_def(env, "matrix->tensor",   prim_matrix_to_tensor, 1,  1);
    mat_def(env, "tensor->matrix",   prim_tensor_to_matrix, 1,  1);
}

void mat_register_builtins(val_t env) {
    mat_register_matrix_builtins(env);
    mat_register_tensor_builtins(env);
}
