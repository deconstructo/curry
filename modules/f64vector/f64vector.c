/* f64vector.c — typed flat double[] arrays with bulk C arithmetic.
 *
 * NumPy's core insight applied to Curry: the interpreter handles control
 * flow, C handles the arithmetic.  Operations that would cost ~16 µs in
 * interpreted Scheme loops run in ~100 ns here for a 1 000-element vector.
 *
 * Types: #f64(1.0 2.0 3.0)
 *
 * (import (curry f64vector))
 */

#include <curry.h>
#include "object.h"
#include "gc.h"
#include "numeric.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

/* ---- allocation ---- */

static val_t alloc_f64v(uint32_t n) {
    F64Vec *fv = CURRY_NEW_FLEX_ATOM(F64Vec, n);
    fv->hdr.type  = T_F64VEC;
    fv->hdr.flags = 0;
    fv->len = n;
    return vptr(fv);
}

/* ---- argument helpers ---- */

static F64Vec *get_f64v(val_t v, const char *ctx) {
    if (!vis_f64vec(v)) curry_error("%s: expected f64vector", ctx);
    return as_f64v(v);
}

static double to_dbl(val_t v, const char *ctx) {
    if (vis_flonum(v)) return vfloat(v);
    if (vis_fixnum(v)) return (double)vunfix(v);
    curry_error("%s: expected real number", ctx);
}

static uint32_t to_idx(val_t v, uint32_t len, const char *ctx) {
    if (!vis_fixnum(v)) curry_error("%s: expected fixnum index", ctx);
    intptr_t i = vunfix(v);
    if (i < 0 || (uint32_t)i >= len)
        curry_error("%s: index %ld out of range [0, %u)", ctx, (long)i, (unsigned)len);
    return (uint32_t)i;
}

static void check_same_len(F64Vec *a, F64Vec *b, const char *ctx) {
    if (a->len != b->len)
        curry_error("%s: f64vector lengths differ (%u vs %u)", ctx, a->len, b->len);
}

/* ==== Constructors ==== */

static val_t fn_make_f64vector(int ac, val_t *av, void *ud) {
    (void)ud;
    if (!vis_fixnum(av[0])) curry_error("make-f64vector: length must be fixnum");
    intptr_t n = vunfix(av[0]);
    if (n < 0) curry_error("make-f64vector: negative length");
    double fill = ac >= 2 ? to_dbl(av[1], "make-f64vector") : 0.0;
    val_t r = alloc_f64v((uint32_t)n);
    double *d = as_f64v(r)->data;
    for (uint32_t i = 0; i < (uint32_t)n; i++) d[i] = fill;
    return r;
}

static val_t fn_f64vector(int ac, val_t *av, void *ud) {
    (void)ud;
    val_t r = alloc_f64v((uint32_t)ac);
    double *d = as_f64v(r)->data;
    for (int i = 0; i < ac; i++) d[i] = to_dbl(av[i], "f64vector");
    return r;
}

static val_t fn_f64vector_copy(int ac, val_t *av, void *ud) {
    (void)ud;
    F64Vec *src = get_f64v(av[0], "f64vector-copy");
    uint32_t start = 0, end = src->len;
    if (ac >= 2) {
        if (!vis_fixnum(av[1])) curry_error("f64vector-copy: start must be fixnum");
        intptr_t s = vunfix(av[1]);
        if (s < 0 || (uint32_t)s > src->len) curry_error("f64vector-copy: start out of range");
        start = (uint32_t)s;
    }
    if (ac >= 3) {
        if (!vis_fixnum(av[2])) curry_error("f64vector-copy: end must be fixnum");
        intptr_t e = vunfix(av[2]);
        if ((uint32_t)e < start || (uint32_t)e > src->len)
            curry_error("f64vector-copy: end out of range");
        end = (uint32_t)e;
    }
    uint32_t n = end - start;
    val_t r = alloc_f64v(n);
    memcpy(as_f64v(r)->data, src->data + start, n * sizeof(double));
    return r;
}

/* (f64vector-iota n [start [step]]) → #f64(start, start+step, ...) */
static val_t fn_f64vector_iota(int ac, val_t *av, void *ud) {
    (void)ud;
    if (!vis_fixnum(av[0])) curry_error("f64vector-iota: count must be fixnum");
    intptr_t n = vunfix(av[0]);
    if (n < 0) curry_error("f64vector-iota: negative count");
    double start = ac >= 2 ? to_dbl(av[1], "f64vector-iota") : 0.0;
    double step  = ac >= 3 ? to_dbl(av[2], "f64vector-iota") : 1.0;
    val_t r = alloc_f64v((uint32_t)n);
    double *d = as_f64v(r)->data;
    for (uint32_t i = 0; i < (uint32_t)n; i++) d[i] = start + i * step;
    return r;
}

/* (f64vector-linspace lo hi n) → n evenly spaced values in [lo, hi] */
static val_t fn_f64vector_linspace(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    double lo = to_dbl(av[0], "f64vector-linspace");
    double hi = to_dbl(av[1], "f64vector-linspace");
    if (!vis_fixnum(av[2])) curry_error("f64vector-linspace: count must be fixnum");
    intptr_t n = vunfix(av[2]);
    if (n < 0) curry_error("f64vector-linspace: negative count");
    val_t r = alloc_f64v((uint32_t)n);
    double *d = as_f64v(r)->data;
    if (n <= 1) { if (n == 1) d[0] = lo; return r; }
    for (uint32_t i = 0; i < (uint32_t)n; i++)
        d[i] = lo + (hi - lo) * i / (double)(n - 1);
    return r;
}

/* ==== Predicates / access ==== */

static val_t fn_f64vector_p(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    return vis_f64vec(av[0]) ? V_TRUE : V_FALSE;
}

static val_t fn_f64vector_length(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    return vfix((intptr_t)get_f64v(av[0], "f64vector-length")->len);
}

static val_t fn_f64vector_ref(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    F64Vec *fv = get_f64v(av[0], "f64vector-ref");
    return num_make_float(fv->data[to_idx(av[1], fv->len, "f64vector-ref")]);
}

static val_t fn_f64vector_set(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    F64Vec *fv = get_f64v(av[0], "f64vector-set!");
    fv->data[to_idx(av[1], fv->len, "f64vector-set!")] =
        to_dbl(av[2], "f64vector-set!");
    return V_VOID;
}

/* ==== Conversion ==== */

static val_t fn_f64vector_to_list(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    F64Vec *fv = get_f64v(av[0], "f64vector->list");
    val_t r = V_NIL;
    for (uint32_t i = fv->len; i-- > 0; )
        r = curry_make_pair(num_make_float(fv->data[i]), r);
    return r;
}

static val_t fn_list_to_f64vector(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    uint32_t n = 0;
    val_t lst = av[0];
    for (val_t p = lst; vis_pair(p); p = vcdr(p)) n++;
    val_t r = alloc_f64v(n);
    double *d = as_f64v(r)->data;
    for (uint32_t i = 0; vis_pair(lst); lst = vcdr(lst), i++)
        d[i] = to_dbl(vcar(lst), "list->f64vector");
    return r;
}

static val_t fn_f64vector_to_vector(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    F64Vec *fv = get_f64v(av[0], "f64vector->vector");
    val_t r = curry_make_vector(fv->len, curry_make_float(0.0));
    for (uint32_t i = 0; i < fv->len; i++)
        curry_vector_set(r, i, num_make_float(fv->data[i]));
    return r;
}

static val_t fn_vector_to_f64vector(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!vis_vector(av[0])) curry_error("vector->f64vector: expected vector");
    uint32_t n = curry_vector_length(av[0]);
    val_t r = alloc_f64v(n);
    double *d = as_f64v(r)->data;
    for (uint32_t i = 0; i < n; i++)
        d[i] = to_dbl(curry_vector_ref(av[0], i), "vector->f64vector");
    return r;
}

/* ==== Bulk in-place ops ==== */

static val_t fn_f64vector_fill(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    F64Vec *fv = get_f64v(av[0], "f64vector-fill!");
    double x = to_dbl(av[1], "f64vector-fill!");
    double *d = fv->data; uint32_t n = fv->len;
    for (uint32_t i = 0; i < n; i++) d[i] = x;
    return V_VOID;
}

static val_t fn_f64vector_scale(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    F64Vec *fv = get_f64v(av[0], "f64vector-scale!");
    double s = to_dbl(av[1], "f64vector-scale!");
    double *d = fv->data; uint32_t n = fv->len;
    for (uint32_t i = 0; i < n; i++) d[i] *= s;
    return V_VOID;
}

static val_t fn_f64vector_offset(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    F64Vec *fv = get_f64v(av[0], "f64vector-offset!");
    double s = to_dbl(av[1], "f64vector-offset!");
    double *d = fv->data; uint32_t n = fv->len;
    for (uint32_t i = 0; i < n; i++) d[i] += s;
    return V_VOID;
}

/* (f64vector-fma! v a b) → v[i] = v[i]*a + b  (scalar fused multiply-add) */
static val_t fn_f64vector_fma(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    F64Vec *fv = get_f64v(av[0], "f64vector-fma!");
    double a = to_dbl(av[1], "f64vector-fma!");
    double b = to_dbl(av[2], "f64vector-fma!");
    double *d = fv->data; uint32_t n = fv->len;
    for (uint32_t i = 0; i < n; i++) d[i] = d[i] * a + b;
    return V_VOID;
}

static val_t fn_f64vector_neg(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    F64Vec *fv = get_f64v(av[0], "f64vector-neg!");
    double *d = fv->data; uint32_t n = fv->len;
    for (uint32_t i = 0; i < n; i++) d[i] = -d[i];
    return V_VOID;
}

static val_t fn_f64vector_clamp(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    F64Vec *fv = get_f64v(av[0], "f64vector-clamp!");
    double lo = to_dbl(av[1], "f64vector-clamp!");
    double hi = to_dbl(av[2], "f64vector-clamp!");
    double *d = fv->data; uint32_t n = fv->len;
    for (uint32_t i = 0; i < n; i++) {
        double x = d[i];
        d[i] = x < lo ? lo : x > hi ? hi : x;
    }
    return V_VOID;
}

/* Unary math functions — each defined by a macro to avoid repetition */
#define DEF_UNARY_MATH(fname, schname, cfn) \
static val_t fname(int ac, val_t *av, void *ud) { \
    (void)ac; (void)ud; \
    F64Vec *fv = get_f64v(av[0], schname); \
    double *d = fv->data; uint32_t n = fv->len; \
    for (uint32_t i = 0; i < n; i++) d[i] = cfn(d[i]); \
    return V_VOID; \
}
DEF_UNARY_MATH(fn_f64vector_abs,  "f64vector-abs!",  fabs)
DEF_UNARY_MATH(fn_f64vector_sqrt, "f64vector-sqrt!", sqrt)
DEF_UNARY_MATH(fn_f64vector_exp,  "f64vector-exp!",  exp)
DEF_UNARY_MATH(fn_f64vector_log,  "f64vector-log!",  log)
DEF_UNARY_MATH(fn_f64vector_sin,  "f64vector-sin!",  sin)
DEF_UNARY_MATH(fn_f64vector_cos,  "f64vector-cos!",  cos)
DEF_UNARY_MATH(fn_f64vector_tan,  "f64vector-tan!",  tan)

/* Element-wise in-place binary ops */
#define DEF_BINOP(fname, schname, op) \
static val_t fname(int ac, val_t *av, void *ud) { \
    (void)ac; (void)ud; \
    F64Vec *a = get_f64v(av[0], schname); \
    F64Vec *b = get_f64v(av[1], schname); \
    check_same_len(a, b, schname); \
    double *da = a->data, *db = b->data; uint32_t n = a->len; \
    for (uint32_t i = 0; i < n; i++) da[i] op##= db[i]; \
    return V_VOID; \
}
DEF_BINOP(fn_f64vector_add, "f64vector-add!", +)
DEF_BINOP(fn_f64vector_sub, "f64vector-sub!", -)
DEF_BINOP(fn_f64vector_mul, "f64vector-mul!", *)
DEF_BINOP(fn_f64vector_div, "f64vector-div!", /)

/* ==== Reductions ==== */

static val_t fn_f64vector_sum(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    F64Vec *fv = get_f64v(av[0], "f64vector-sum");
    double acc = 0.0;
    double *d = fv->data; uint32_t n = fv->len;
    for (uint32_t i = 0; i < n; i++) acc += d[i];
    return num_make_float(acc);
}

static val_t fn_f64vector_product(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    F64Vec *fv = get_f64v(av[0], "f64vector-product");
    double acc = 1.0;
    double *d = fv->data; uint32_t n = fv->len;
    for (uint32_t i = 0; i < n; i++) acc *= d[i];
    return num_make_float(acc);
}

static val_t fn_f64vector_min(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    F64Vec *fv = get_f64v(av[0], "f64vector-min");
    if (fv->len == 0) curry_error("f64vector-min: empty vector");
    double m = fv->data[0];
    for (uint32_t i = 1; i < fv->len; i++)
        if (fv->data[i] < m) m = fv->data[i];
    return num_make_float(m);
}

static val_t fn_f64vector_max(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    F64Vec *fv = get_f64v(av[0], "f64vector-max");
    if (fv->len == 0) curry_error("f64vector-max: empty vector");
    double m = fv->data[0];
    for (uint32_t i = 1; i < fv->len; i++)
        if (fv->data[i] > m) m = fv->data[i];
    return num_make_float(m);
}

static val_t fn_f64vector_mean(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    F64Vec *fv = get_f64v(av[0], "f64vector-mean");
    if (fv->len == 0) curry_error("f64vector-mean: empty vector");
    double acc = 0.0;
    double *d = fv->data; uint32_t n = fv->len;
    for (uint32_t i = 0; i < n; i++) acc += d[i];
    return num_make_float(acc / n);
}

static val_t fn_f64vector_dot(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    F64Vec *a = get_f64v(av[0], "f64vector-dot");
    F64Vec *b = get_f64v(av[1], "f64vector-dot");
    check_same_len(a, b, "f64vector-dot");
    double acc = 0.0;
    double *da = a->data, *db = b->data; uint32_t n = a->len;
    for (uint32_t i = 0; i < n; i++) acc += da[i] * db[i];
    return num_make_float(acc);
}

static val_t fn_f64vector_norm(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    F64Vec *fv = get_f64v(av[0], "f64vector-norm");
    double acc = 0.0;
    double *d = fv->data; uint32_t n = fv->len;
    for (uint32_t i = 0; i < n; i++) acc += d[i] * d[i];
    return num_make_float(sqrt(acc));
}

static val_t fn_f64vector_argmin(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    F64Vec *fv = get_f64v(av[0], "f64vector-argmin");
    if (fv->len == 0) curry_error("f64vector-argmin: empty vector");
    uint32_t idx = 0;
    for (uint32_t i = 1; i < fv->len; i++)
        if (fv->data[i] < fv->data[idx]) idx = i;
    return vfix((intptr_t)idx);
}

static val_t fn_f64vector_argmax(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    F64Vec *fv = get_f64v(av[0], "f64vector-argmax");
    if (fv->len == 0) curry_error("f64vector-argmax: empty vector");
    uint32_t idx = 0;
    for (uint32_t i = 1; i < fv->len; i++)
        if (fv->data[i] > fv->data[idx]) idx = i;
    return vfix((intptr_t)idx);
}

/* ==== Producing new vectors ==== */

/* (f64vector-map proc v) — per-element Scheme call; flexible, not fast */
static val_t fn_f64vector_map(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!curry_is_procedure(av[0])) curry_error("f64vector-map: expected procedure");
    F64Vec *src = get_f64v(av[1], "f64vector-map");
    val_t r = alloc_f64v(src->len);
    double *d = as_f64v(r)->data;
    for (uint32_t i = 0; i < src->len; i++) {
        val_t arg = num_make_float(src->data[i]);
        d[i] = to_dbl(curry_apply(av[0], 1, &arg), "f64vector-map: proc must return number");
    }
    return r;
}

/* (f64vector-map2 proc v1 v2) — binary per-element Scheme call */
static val_t fn_f64vector_map2(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!curry_is_procedure(av[0])) curry_error("f64vector-map2: expected procedure");
    F64Vec *a = get_f64v(av[1], "f64vector-map2");
    F64Vec *b = get_f64v(av[2], "f64vector-map2");
    check_same_len(a, b, "f64vector-map2");
    val_t r = alloc_f64v(a->len);
    double *d = as_f64v(r)->data;
    for (uint32_t i = 0; i < a->len; i++) {
        val_t args[2] = { num_make_float(a->data[i]), num_make_float(b->data[i]) };
        d[i] = to_dbl(curry_apply(av[0], 2, args), "f64vector-map2: proc must return number");
    }
    return r;
}

/* (f64vector-for-each proc v) — side effects per element */
static val_t fn_f64vector_for_each(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    if (!curry_is_procedure(av[0])) curry_error("f64vector-for-each: expected procedure");
    F64Vec *fv = get_f64v(av[1], "f64vector-for-each");
    for (uint32_t i = 0; i < fv->len; i++) {
        val_t arg = num_make_float(fv->data[i]);
        curry_apply(av[0], 1, &arg);
    }
    return V_VOID;
}

static val_t fn_f64vector_append(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    F64Vec *a = get_f64v(av[0], "f64vector-append");
    F64Vec *b = get_f64v(av[1], "f64vector-append");
    uint32_t n = a->len + b->len;
    val_t r = alloc_f64v(n);
    double *d = as_f64v(r)->data;
    memcpy(d,            a->data, a->len * sizeof(double));
    memcpy(d + a->len,   b->data, b->len * sizeof(double));
    return r;
}

static val_t fn_f64vector_reverse(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    F64Vec *src = get_f64v(av[0], "f64vector-reverse");
    val_t r = alloc_f64v(src->len);
    double *d = as_f64v(r)->data;
    for (uint32_t i = 0; i < src->len; i++)
        d[i] = src->data[src->len - 1 - i];
    return r;
}

static int cmp_double(const void *a, const void *b) {
    double da = *(const double *)a, db = *(const double *)b;
    return da < db ? -1 : da > db ? 1 : 0;
}

static val_t fn_f64vector_sort(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    F64Vec *src = get_f64v(av[0], "f64vector-sort");
    val_t r = alloc_f64v(src->len);
    double *d = as_f64v(r)->data;
    memcpy(d, src->data, src->len * sizeof(double));
    qsort(d, src->len, sizeof(double), cmp_double);
    return r;
}

/* (f64vector= v1 v2) — exact bitwise equality */
static val_t fn_f64vector_eq(int ac, val_t *av, void *ud) {
    (void)ac; (void)ud;
    F64Vec *a = get_f64v(av[0], "f64vector=");
    F64Vec *b = get_f64v(av[1], "f64vector=");
    return (a->len == b->len &&
            memcmp(a->data, b->data, a->len * sizeof(double)) == 0)
           ? V_TRUE : V_FALSE;
}

/* ==== Module entry point ==== */

void curry_module_init(CurryVM *vm) {
    /* Constructors */
    curry_define_fn(vm, "make-f64vector",     fn_make_f64vector,     1, 2,  NULL);
    curry_define_fn(vm, "f64vector",          fn_f64vector,          0, -1, NULL);
    curry_define_fn(vm, "f64vector-copy",     fn_f64vector_copy,     1, 3,  NULL);
    curry_define_fn(vm, "f64vector-iota",     fn_f64vector_iota,     1, 3,  NULL);
    curry_define_fn(vm, "f64vector-linspace", fn_f64vector_linspace, 3, 3,  NULL);
    /* Predicates / access */
    curry_define_fn(vm, "f64vector?",         fn_f64vector_p,        1, 1,  NULL);
    curry_define_fn(vm, "f64vector-length",   fn_f64vector_length,   1, 1,  NULL);
    curry_define_fn(vm, "f64vector-ref",      fn_f64vector_ref,      2, 2,  NULL);
    curry_define_fn(vm, "f64vector-set!",     fn_f64vector_set,      3, 3,  NULL);
    /* Conversion */
    curry_define_fn(vm, "f64vector->list",    fn_f64vector_to_list,  1, 1,  NULL);
    curry_define_fn(vm, "list->f64vector",    fn_list_to_f64vector,  1, 1,  NULL);
    curry_define_fn(vm, "f64vector->vector",  fn_f64vector_to_vector,1, 1,  NULL);
    curry_define_fn(vm, "vector->f64vector",  fn_vector_to_f64vector,1, 1,  NULL);
    /* Bulk in-place */
    curry_define_fn(vm, "f64vector-fill!",    fn_f64vector_fill,     2, 2,  NULL);
    curry_define_fn(vm, "f64vector-scale!",   fn_f64vector_scale,    2, 2,  NULL);
    curry_define_fn(vm, "f64vector-offset!",  fn_f64vector_offset,   2, 2,  NULL);
    curry_define_fn(vm, "f64vector-fma!",     fn_f64vector_fma,      3, 3,  NULL);
    curry_define_fn(vm, "f64vector-neg!",     fn_f64vector_neg,      1, 1,  NULL);
    curry_define_fn(vm, "f64vector-clamp!",   fn_f64vector_clamp,    3, 3,  NULL);
    curry_define_fn(vm, "f64vector-abs!",     fn_f64vector_abs,      1, 1,  NULL);
    curry_define_fn(vm, "f64vector-sqrt!",    fn_f64vector_sqrt,     1, 1,  NULL);
    curry_define_fn(vm, "f64vector-exp!",     fn_f64vector_exp,      1, 1,  NULL);
    curry_define_fn(vm, "f64vector-log!",     fn_f64vector_log,      1, 1,  NULL);
    curry_define_fn(vm, "f64vector-sin!",     fn_f64vector_sin,      1, 1,  NULL);
    curry_define_fn(vm, "f64vector-cos!",     fn_f64vector_cos,      1, 1,  NULL);
    curry_define_fn(vm, "f64vector-tan!",     fn_f64vector_tan,      1, 1,  NULL);
    curry_define_fn(vm, "f64vector-add!",     fn_f64vector_add,      2, 2,  NULL);
    curry_define_fn(vm, "f64vector-sub!",     fn_f64vector_sub,      2, 2,  NULL);
    curry_define_fn(vm, "f64vector-mul!",     fn_f64vector_mul,      2, 2,  NULL);
    curry_define_fn(vm, "f64vector-div!",     fn_f64vector_div,      2, 2,  NULL);
    /* Reductions */
    curry_define_fn(vm, "f64vector-sum",      fn_f64vector_sum,      1, 1,  NULL);
    curry_define_fn(vm, "f64vector-product",  fn_f64vector_product,  1, 1,  NULL);
    curry_define_fn(vm, "f64vector-min",      fn_f64vector_min,      1, 1,  NULL);
    curry_define_fn(vm, "f64vector-max",      fn_f64vector_max,      1, 1,  NULL);
    curry_define_fn(vm, "f64vector-mean",     fn_f64vector_mean,     1, 1,  NULL);
    curry_define_fn(vm, "f64vector-dot",      fn_f64vector_dot,      2, 2,  NULL);
    curry_define_fn(vm, "f64vector-norm",     fn_f64vector_norm,     1, 1,  NULL);
    curry_define_fn(vm, "f64vector-argmin",   fn_f64vector_argmin,   1, 1,  NULL);
    curry_define_fn(vm, "f64vector-argmax",   fn_f64vector_argmax,   1, 1,  NULL);
    /* Producing */
    curry_define_fn(vm, "f64vector-map",      fn_f64vector_map,      2, 2,  NULL);
    curry_define_fn(vm, "f64vector-map2",     fn_f64vector_map2,     3, 3,  NULL);
    curry_define_fn(vm, "f64vector-for-each", fn_f64vector_for_each, 2, 2,  NULL);
    curry_define_fn(vm, "f64vector-slice",    fn_f64vector_copy,     3, 3,  NULL);
    curry_define_fn(vm, "f64vector-append",   fn_f64vector_append,   2, 2,  NULL);
    curry_define_fn(vm, "f64vector-reverse",  fn_f64vector_reverse,  1, 1,  NULL);
    curry_define_fn(vm, "f64vector-sort",     fn_f64vector_sort,     1, 1,  NULL);
    curry_define_fn(vm, "f64vector=",         fn_f64vector_eq,       2, 2,  NULL);
}
