#include "multivec.h"
#include "object.h"
#include "gc.h"
#include "numeric.h"
#include "port.h"
#include "env.h"
#include "symbol.h"
#include <string.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

extern void scm_raise(val_t kind, const char *fmt, ...) __attribute__((noreturn));

/* ---- Blade geometric product ---- */

/*
 * Computes eA * eB in Cl(p,q,r).
 *
 * Algorithm: process each bit k of B from lowest to highest.
 *   - Count bits in the accumulated blade above k → each is a swap → sign change.
 *   - If k is already in the accumulated blade: basis vector squares:
 *       k < p  → e_k² = +1   (no sign change)
 *       k < p+q → e_k² = -1  (sign change)
 *       else   → e_k² = 0    (null metric: entire product = 0)
 *   - Otherwise: add k to the accumulated blade.
 *
 * Returns 0 if the product is zero, +1 or -1 otherwise.
 * *out_blade is set to the result blade bitmap.
 */
int mv_blade_gp(int A, int B, int p, int q, int r, int *out_blade) {
    (void)r;
    int pq   = p + q;
    int sign = 1;
    int acc  = A;
    int b    = B;
    while (b) {
        int k   = __builtin_ctz(b);
        b      &= b - 1;
        int above = __builtin_popcount(acc >> (k + 1));
        if (above & 1) sign = -sign;
        if (acc & (1 << k)) {
            if (k >= pq) { *out_blade = 0; return 0; }   /* null: e_k² = 0 */
            if (k >= p)   sign = -sign;                   /* negative: e_k² = -1 */
            acc ^= (1 << k);
        } else {
            acc |= (1 << k);
        }
    }
    *out_blade = acc;
    return sign;
}

/* ---- Allocation ---- */

val_t mv_make(int p, int q, int r) {
    if (p < 0 || q < 0 || r < 0 || p + q + r > 8)
        scm_raise(V_FALSE, "make-mv: signature out of range (max n=8, got n=%d)", p+q+r);
    int n   = p + q + r;
    int dim = 1 << n;
    Multivector *mv = (Multivector *)gc_alloc_atomic(
        sizeof(Multivector) + (size_t)dim * sizeof(double));
    mv->hdr.type = T_MULTIVECTOR; mv->hdr.flags = 0;
    mv->p = (uint8_t)p; mv->q = (uint8_t)q; mv->r = (uint8_t)r;
    mv->n = (uint8_t)n; mv->dim = (uint32_t)dim;
    memset(mv->c, 0, (size_t)dim * sizeof(double));
    return vptr(mv);
}

static Multivector *mv_alloc_like(const Multivector *src) {
    Multivector *dst = (Multivector *)gc_alloc_atomic(
        sizeof(Multivector) + (size_t)src->dim * sizeof(double));
    memcpy(dst, src, sizeof(Multivector));
    memset(dst->c, 0, (size_t)src->dim * sizeof(double));
    return dst;
}

static Multivector *mv_clone_raw(val_t a) {
    Multivector *src = as_mv(a);
    Multivector *dst = (Multivector *)gc_alloc_atomic(
        sizeof(Multivector) + (size_t)src->dim * sizeof(double));
    memcpy(dst, src, sizeof(Multivector) + (size_t)src->dim * sizeof(double));
    return dst;
}

static void check_same_sig(val_t a, val_t b, const char *op) {
    Multivector *ma = as_mv(a), *mb = as_mv(b);
    if (ma->p != mb->p || ma->q != mb->q || ma->r != mb->r)
        scm_raise(V_FALSE, "%s: signature mismatch Cl(%d,%d,%d) vs Cl(%d,%d,%d)",
                  op, ma->p, ma->q, ma->r, mb->p, mb->q, mb->r);
}

/* ---- Arithmetic ---- */

val_t mv_add(val_t a, val_t b) {
    check_same_sig(a, b, "mv+");
    Multivector *ma = as_mv(a), *mb = as_mv(b);
    Multivector *res = mv_clone_raw(a);
    for (uint32_t i = 0; i < res->dim; i++) res->c[i] = ma->c[i] + mb->c[i];
    return vptr(res);
}

val_t mv_sub(val_t a, val_t b) {
    check_same_sig(a, b, "mv-");
    Multivector *ma = as_mv(a), *mb = as_mv(b);
    Multivector *res = mv_clone_raw(a);
    for (uint32_t i = 0; i < res->dim; i++) res->c[i] = ma->c[i] - mb->c[i];
    return vptr(res);
}

val_t mv_neg(val_t a) {
    Multivector *res = mv_clone_raw(a);
    for (uint32_t i = 0; i < res->dim; i++) res->c[i] = -res->c[i];
    return vptr(res);
}

val_t mv_scale(val_t a, double s) {
    Multivector *res = mv_clone_raw(a);
    for (uint32_t i = 0; i < res->dim; i++) res->c[i] *= s;
    return vptr(res);
}

val_t mv_geom(val_t a, val_t b) {
    check_same_sig(a, b, "mv*");
    Multivector *ma = as_mv(a), *mb = as_mv(b);
    Multivector *res = mv_alloc_like(ma);
    int p = ma->p, q = ma->q, r = ma->r;
    for (uint32_t i = 0; i < ma->dim; i++) {
        if (ma->c[i] == 0.0) continue;
        for (uint32_t j = 0; j < mb->dim; j++) {
            if (mb->c[j] == 0.0) continue;
            int out, sign = mv_blade_gp((int)i, (int)j, p, q, r, &out);
            if (sign == 0) continue;
            res->c[out] += sign * ma->c[i] * mb->c[j];
        }
    }
    return vptr(res);
}

val_t mv_wedge(val_t a, val_t b) {
    check_same_sig(a, b, "mv-wedge");
    Multivector *ma = as_mv(a), *mb = as_mv(b);
    Multivector *res = mv_alloc_like(ma);
    int p = ma->p, q = ma->q, r = ma->r;
    for (uint32_t i = 0; i < ma->dim; i++) {
        if (ma->c[i] == 0.0) continue;
        for (uint32_t j = 0; j < mb->dim; j++) {
            if (mb->c[j] == 0.0) continue;
            if (i & j) continue;   /* shared indices → outer product is zero */
            int out, sign = mv_blade_gp((int)i, (int)j, p, q, r, &out);
            if (sign == 0) continue;
            res->c[out] += sign * ma->c[i] * mb->c[j];
        }
    }
    return vptr(res);
}

val_t mv_lcontract(val_t a, val_t b) {
    /* Left contraction A⌋B: nonzero only when A ⊆ B (A is a subset of B) */
    check_same_sig(a, b, "mv-lcontract");
    Multivector *ma = as_mv(a), *mb = as_mv(b);
    Multivector *res = mv_alloc_like(ma);
    int p = ma->p, q = ma->q, r = ma->r;
    for (uint32_t i = 0; i < ma->dim; i++) {
        if (ma->c[i] == 0.0) continue;
        for (uint32_t j = 0; j < mb->dim; j++) {
            if (mb->c[j] == 0.0) continue;
            if ((i & j) != i) continue;   /* A must be subset of B */
            int out, sign = mv_blade_gp((int)i, (int)j, p, q, r, &out);
            if (sign == 0) continue;
            res->c[out] += sign * ma->c[i] * mb->c[j];
        }
    }
    return vptr(res);
}

/* ---- Involutions ---- */

static int blade_grade(int bitmap) { return __builtin_popcount(bitmap); }

val_t mv_reverse(val_t a) {
    /* Reversion: sign flip for grade k where k*(k-1)/2 is odd.
       Grades 0,1: +  |  2,3: −  |  4,5: +  |  6,7: −  ... */
    Multivector *res = mv_clone_raw(a);
    for (uint32_t i = 0; i < res->dim; i++) {
        int k = blade_grade((int)i);
        if ((k * (k - 1) / 2) & 1) res->c[i] = -res->c[i];
    }
    return vptr(res);
}

val_t mv_involute(val_t a) {
    /* Grade involution: multiply grade-k part by (−1)^k */
    Multivector *res = mv_clone_raw(a);
    for (uint32_t i = 0; i < res->dim; i++)
        if (blade_grade((int)i) & 1) res->c[i] = -res->c[i];
    return vptr(res);
}

val_t mv_conjugate(val_t a) {
    /* Clifford conjugate = involute ∘ reverse; sign (−1)^(k*(k+1)/2) */
    Multivector *res = mv_clone_raw(a);
    for (uint32_t i = 0; i < res->dim; i++) {
        int k = blade_grade((int)i);
        if ((k * (k + 1) / 2) & 1) res->c[i] = -res->c[i];
    }
    return vptr(res);
}

val_t mv_dual(val_t a) {
    /* Right complement: x * rev(I), where I = e₁∧e₂∧...∧eₙ */
    Multivector *ma = as_mv(a);
    val_t I    = mv_make(ma->p, ma->q, ma->r);
    as_mv(I)->c[ma->dim - 1] = 1.0;   /* I = blade with all bits set */
    val_t Irev = mv_reverse(I);
    return mv_geom(a, Irev);
}

val_t mv_grade(val_t a, int k) {
    Multivector *ma  = as_mv(a);
    Multivector *res = mv_alloc_like(ma);
    for (uint32_t i = 0; i < ma->dim; i++)
        if (blade_grade((int)i) == k) res->c[i] = ma->c[i];
    return vptr(res);
}

double mv_norm2_d(val_t a) {
    /* ⟨ã·a⟩₀ = scalar part of (reverse(a) * a) */
    return as_mv(mv_geom(mv_reverse(a), a))->c[0];
}

val_t mv_norm(val_t a) {
    return num_make_float(sqrt(fabs(mv_norm2_d(a))));
}

val_t mv_normalize(val_t a) {
    double n2 = mv_norm2_d(a);
    if (n2 == 0.0) scm_raise(V_FALSE, "mv-normalize: zero norm");
    return mv_scale(a, 1.0 / sqrt(fabs(n2)));
}

/* ---- Blade index helper ---- */

int mv_blade_index(val_t spec, int n) {
    if (vis_fixnum(spec)) {
        int b = (int)vunfix(spec);
        if (b < 0 || b >= (1 << n)) return -1;
        return b;
    }
    if (vis_nil(spec)) return 0;   /* scalar */
    if (vis_pair(spec)) {
        int bitmap = 0;
        val_t p = spec;
        while (vis_pair(p)) {
            val_t idx = vcar(p);
            if (!vis_fixnum(idx)) return -1;
            int k = (int)vunfix(idx) - 1;   /* 1-based → 0-based */
            if (k < 0 || k >= n) return -1;
            if (bitmap & (1 << k)) return -1;   /* repeated */
            bitmap |= (1 << k);
            p = vcdr(p);
        }
        return bitmap;
    }
    return -1;
}

/* ---- Write ---- */

void mv_write(val_t v, val_t port) {
    Multivector *mv = as_mv(v);
    char buf[128];
    int len = snprintf(buf, sizeof(buf), "#<mv Cl(%d,%d,%d)", mv->p, mv->q, mv->r);
    port_write_string(port, buf, (uint32_t)len);
    bool first = true;
    for (uint32_t i = 0; i < mv->dim; i++) {
        double c = mv->c[i];
        if (c == 0.0) continue;
        if (first) { port_write_string(port, ": ", 2); first = false; }
        else if (c > 0.0) port_write_char(port, '+');
        len = snprintf(buf, sizeof(buf), "%g", c);
        port_write_string(port, buf, (uint32_t)len);
        if (i == 0) continue;   /* scalar part: just the number */
        /* Print blade label: e followed by 1-based subscripts */
        port_write_char(port, 'e');
        for (int k = 0; k < mv->n; k++) {
            if (i & (1 << k)) {
                len = snprintf(buf, sizeof(buf), "%d", k + 1);
                port_write_string(port, buf, (uint32_t)len);
            }
        }
    }
    if (first) port_write_string(port, ": 0", 3);
    port_write_char(port, '>');
}

/* ---- Local helpers for builtins ---- */

static val_t mv_cons(val_t car, val_t cdr) {
    Pair *p = CURRY_NEW(Pair);
    p->hdr.type = T_PAIR; p->hdr.flags = 0;
    p->car = car; p->cdr = cdr;
    return vptr(p);
}

static void mv_def(val_t env, const char *name, PrimFn fn, int mn, int mx) {
    Primitive *p = CURRY_NEW(Primitive);
    p->hdr.type = T_PRIMITIVE; p->hdr.flags = 0;
    p->name = name; p->fn = fn; p->min_args = mn; p->max_args = mx; p->ud = NULL;
    env_define(env, sym_intern_cstr(name), vptr(p));
}

/* ---- Scheme builtins ---- */

static val_t prim_make_mv(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    return mv_make((int)vunfix(av[0]), (int)vunfix(av[1]), (int)vunfix(av[2]));
}

static val_t prim_mv_p(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    return vis_mv(av[0]) ? V_TRUE : V_FALSE;
}

static val_t prim_mv_sig(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_mv(av[0])) scm_raise(V_FALSE, "mv-signature: not a multivector");
    Multivector *mv = as_mv(av[0]);
    return mv_cons(vfix(mv->p), mv_cons(vfix(mv->q), mv_cons(vfix(mv->r), V_NIL)));
}

static val_t prim_mv_ref(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_mv(av[0])) scm_raise(V_FALSE, "mv-ref: not a multivector");
    Multivector *mv = as_mv(av[0]);
    int blade = mv_blade_index(av[1], mv->n);
    if (blade < 0 || (uint32_t)blade >= mv->dim)
        scm_raise(V_FALSE, "mv-ref: blade index out of range");
    return num_make_float(mv->c[blade]);
}

static val_t prim_mv_set(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_mv(av[0])) scm_raise(V_FALSE, "mv-set!: not a multivector");
    Multivector *mv = as_mv(av[0]);
    int blade = mv_blade_index(av[1], mv->n);
    if (blade < 0 || (uint32_t)blade >= mv->dim)
        scm_raise(V_FALSE, "mv-set!: blade index out of range");
    mv->c[blade] = num_to_double(av[2]);
    return V_VOID;
}

static val_t prim_mv_add(int argc, val_t *av, void *ud) {
    (void)ud;
    if (argc == 0) scm_raise(V_FALSE, "mv+: requires at least one argument");
    if (!vis_mv(av[0])) scm_raise(V_FALSE, "mv+: not a multivector");
    val_t acc = av[0];
    for (int i = 1; i < argc; i++) {
        if (!vis_mv(av[i])) scm_raise(V_FALSE, "mv+: not a multivector");
        acc = mv_add(acc, av[i]);
    }
    return acc;
}

static val_t prim_mv_sub(int argc, val_t *av, void *ud) {
    (void)ud;
    if (argc == 0) scm_raise(V_FALSE, "mv-: requires at least one argument");
    if (!vis_mv(av[0])) scm_raise(V_FALSE, "mv-: not a multivector");
    if (argc == 1) return mv_neg(av[0]);
    val_t acc = av[0];
    for (int i = 1; i < argc; i++) {
        if (!vis_mv(av[i])) scm_raise(V_FALSE, "mv-: not a multivector");
        acc = mv_sub(acc, av[i]);
    }
    return acc;
}

static val_t prim_mv_geom(int argc, val_t *av, void *ud) {
    (void)ud;
    if (argc == 0) scm_raise(V_FALSE, "mv*: requires at least one argument");
    if (!vis_mv(av[0])) scm_raise(V_FALSE, "mv*: not a multivector");
    val_t acc = av[0];
    for (int i = 1; i < argc; i++) {
        if (!vis_mv(av[i])) scm_raise(V_FALSE, "mv*: not a multivector");
        acc = mv_geom(acc, av[i]);
    }
    return acc;
}

static val_t prim_mv_scale(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_mv(av[0])) scm_raise(V_FALSE, "mv-scale: not a multivector");
    return mv_scale(av[0], num_to_double(av[1]));
}

static val_t prim_mv_wedge(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_mv(av[0]) || !vis_mv(av[1])) scm_raise(V_FALSE, "mv-wedge: not a multivector");
    return mv_wedge(av[0], av[1]);
}

static val_t prim_mv_lcontract(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_mv(av[0]) || !vis_mv(av[1])) scm_raise(V_FALSE, "mv-lcontract: not a multivector");
    return mv_lcontract(av[0], av[1]);
}

static val_t prim_mv_reverse(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_mv(av[0])) scm_raise(V_FALSE, "mv-reverse: not a multivector");
    return mv_reverse(av[0]);
}

static val_t prim_mv_involute(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_mv(av[0])) scm_raise(V_FALSE, "mv-involute: not a multivector");
    return mv_involute(av[0]);
}

static val_t prim_mv_conjugate(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_mv(av[0])) scm_raise(V_FALSE, "mv-conjugate: not a multivector");
    return mv_conjugate(av[0]);
}

static val_t prim_mv_dual(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_mv(av[0])) scm_raise(V_FALSE, "mv-dual: not a multivector");
    return mv_dual(av[0]);
}

static val_t prim_mv_grade(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_mv(av[0])) scm_raise(V_FALSE, "mv-grade: not a multivector");
    if (!vis_fixnum(av[1])) scm_raise(V_FALSE, "mv-grade: grade must be an integer");
    return mv_grade(av[0], (int)vunfix(av[1]));
}

static val_t prim_mv_scalar(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_mv(av[0])) scm_raise(V_FALSE, "mv-scalar: not a multivector");
    return num_make_float(as_mv(av[0])->c[0]);
}

static val_t prim_mv_norm2(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_mv(av[0])) scm_raise(V_FALSE, "mv-norm2: not a multivector");
    return num_make_float(mv_norm2_d(av[0]));
}

static val_t prim_mv_norm(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_mv(av[0])) scm_raise(V_FALSE, "mv-norm: not a multivector");
    return mv_norm(av[0]);
}

static val_t prim_mv_normalize(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_mv(av[0])) scm_raise(V_FALSE, "mv-normalize: not a multivector");
    return mv_normalize(av[0]);
}

/* (mv-e p q r idx...) — unit basis blade; indices are 1-based, any order */
static val_t prim_mv_e(int argc, val_t *av, void *ud) {
    (void)ud;
    if (argc < 3) scm_raise(V_FALSE, "mv-e: requires at least p q r");
    int p = (int)vunfix(av[0]), q = (int)vunfix(av[1]), r = (int)vunfix(av[2]);
    int n = p + q + r;
    val_t mv = mv_make(p, q, r);
    if (argc == 3) { as_mv(mv)->c[0] = 1.0; return mv; }   /* scalar = 1 */

    int nidx = argc - 3;
    if (nidx > 8) scm_raise(V_FALSE, "mv-e: too many indices");
    int indices[8];
    int bitmap = 0;
    for (int i = 0; i < nidx; i++) {
        if (!vis_fixnum(av[3 + i])) scm_raise(V_FALSE, "mv-e: index must be integer");
        int k = (int)vunfix(av[3 + i]) - 1;   /* 1-based → 0-based */
        if (k < 0 || k >= n) scm_raise(V_FALSE, "mv-e: index %d out of range for n=%d", k+1, n);
        if (bitmap & (1 << k)) scm_raise(V_FALSE, "mv-e: repeated index %d", k+1);
        bitmap   |= (1 << k);
        indices[i] = k;
    }
    /* Bubble-sort indices to canonical order, tracking sign */
    int sign = 1;
    for (int i = 0; i < nidx - 1; i++)
        for (int j = 0; j < nidx - i - 1; j++)
            if (indices[j] > indices[j + 1]) {
                int tmp = indices[j]; indices[j] = indices[j + 1]; indices[j + 1] = tmp;
                sign = -sign;
            }
    as_mv(mv)->c[bitmap] = (double)sign;
    return mv;
}

/* (mv-from-list p q r components) — components is a list of 2^n doubles */
static val_t prim_mv_from_list(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    int p = (int)vunfix(av[0]), q = (int)vunfix(av[1]), r = (int)vunfix(av[2]);
    val_t mv = mv_make(p, q, r);
    Multivector *m = as_mv(mv);
    uint32_t i = 0;
    for (val_t lst = av[3]; vis_pair(lst) && i < m->dim; lst = vcdr(lst))
        m->c[i++] = num_to_double(vcar(lst));
    return mv;
}

/*
 * (quaternion->mv q) or (quaternion->mv a b c d)
 *
 * Embeds a quaternion into the even subalgebra of Cl(3,0,0).
 * Convention: q = a + b*i + c*j + d*k maps to
 *   a·1  +  b·e₁₂  +  c·e₂₃  +  d·e₁₃
 *
 * Verify: i=e₁₂, j=e₂₃, k=e₁₃ in Cl(3,0,0):
 *   i*j = e₁₂*e₂₃ = e₁₃ = k  ✓
 *   j*k = e₂₃*e₁₃ = e₁₂ = i  ✓
 *   i²  = e₁₂² = −1           ✓
 */
static val_t prim_quat_to_mv(int argc, val_t *av, void *ud) {
    (void)ud;
    double a, b, c, d;
    if (argc == 1 && vis_quat(av[0])) {
        Quaternion *q = as_quat(av[0]);
        a = q->a; b = q->b; c = q->c; d = q->d;
    } else if (argc == 4) {
        a = num_to_double(av[0]); b = num_to_double(av[1]);
        c = num_to_double(av[2]); d = num_to_double(av[3]);
    } else {
        scm_raise(V_FALSE, "quaternion->mv: pass a quaternion or 4 components (a b c d)");
    }
    val_t mv = mv_make(3, 0, 0);
    Multivector *m = as_mv(mv);
    m->c[0] = a;  /* scalar           */
    m->c[3] = b;  /* e₁₂ = i, bitmap 0b011 */
    m->c[6] = c;  /* e₂₃ = j, bitmap 0b110 */
    m->c[5] = d;  /* e₁₃ = k, bitmap 0b101 */
    return mv;
}

/* (mv->quaternion mv) — extract even-grade part of Cl(3,0,0) as quaternion */
static val_t prim_mv_to_quat(int argc, val_t *av, void *ud) {
    (void)ud; (void)argc;
    if (!vis_mv(av[0])) scm_raise(V_FALSE, "mv->quaternion: not a multivector");
    Multivector *mv = as_mv(av[0]);
    if (mv->p != 3 || mv->q != 0 || mv->r != 0)
        scm_raise(V_FALSE, "mv->quaternion: requires Cl(3,0,0) multivector");
    return num_make_quat(mv->c[0], mv->c[3], mv->c[6], mv->c[5]);
}

/* ---- Registration ---- */

void mv_register_builtins(val_t env) {
    mv_def(env, "make-mv",        prim_make_mv,      3,  3);
    mv_def(env, "mv?",            prim_mv_p,         1,  1);
    mv_def(env, "mv-signature",   prim_mv_sig,       1,  1);
    mv_def(env, "mv-ref",         prim_mv_ref,       2,  2);
    mv_def(env, "mv-set!",        prim_mv_set,       3,  3);
    mv_def(env, "mv+",            prim_mv_add,       1, -1);
    mv_def(env, "mv-",            prim_mv_sub,       1, -1);
    mv_def(env, "mv*",            prim_mv_geom,      1, -1);
    mv_def(env, "mv-scale",       prim_mv_scale,     2,  2);
    mv_def(env, "mv-wedge",       prim_mv_wedge,     2,  2);
    mv_def(env, "mv-lcontract",   prim_mv_lcontract, 2,  2);
    mv_def(env, "mv-reverse",     prim_mv_reverse,   1,  1);
    mv_def(env, "mv-involute",    prim_mv_involute,  1,  1);
    mv_def(env, "mv-conjugate",   prim_mv_conjugate, 1,  1);
    mv_def(env, "mv-dual",        prim_mv_dual,      1,  1);
    mv_def(env, "mv-grade",       prim_mv_grade,     2,  2);
    mv_def(env, "mv-scalar",      prim_mv_scalar,    1,  1);
    mv_def(env, "mv-norm2",       prim_mv_norm2,     1,  1);
    mv_def(env, "mv-norm",        prim_mv_norm,      1,  1);
    mv_def(env, "mv-normalize",   prim_mv_normalize, 1,  1);
    mv_def(env, "mv-e",           prim_mv_e,         3, -1);
    mv_def(env, "mv-from-list",   prim_mv_from_list, 4,  4);
    mv_def(env, "quaternion->mv", prim_quat_to_mv,   1,  4);
    mv_def(env, "mv->quaternion", prim_mv_to_quat,   1,  1);
}
