#include "numeric.h"
#include "object.h"
#include "gc.h"
#include "eval.h"
#include "symbol.h"
#include "symbolic.h"
#include "quantum.h"
#include "surreal.h"
#include "port.h"
#include "multivec.h"
#ifdef BUILD_MPFR
#include "mpfr_num.h"
#endif
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>
#include <ctype.h>

/* Global: current display notation — 0 = decimal, else a Scheme symbol value */
val_t g_number_notation = 0;

/* ---- Helpers ---- */

static void bignum_finalize(void *obj, void *cd) {
    (void)cd;
    mpz_clear(((Bignum *)obj)->z);
}

static void rational_finalize(void *obj, void *cd) {
    (void)cd;
    mpq_clear(((Rational *)obj)->q);
}

val_t make_big_from_mpz(mpz_t z) {
    /* If it fits in a fixnum, return that instead */
    if (mpz_fits_slong_p(z)) {
        long n = mpz_get_si(z);
        if (in_fixnum_range(n)) return vfix(n);
    }
    Bignum *b = CURRY_NEW_PINNED_ATOM(Bignum);
    b->hdr.type  = T_BIGNUM;
    b->hdr.flags = 0;
    mpz_init_set(b->z, z);
    GC_register_finalizer_no_order(b, bignum_finalize, NULL, NULL, NULL);
    return vptr(b);
}

static val_t make_rat_from_mpq(mpq_t q) {
    mpq_canonicalize(q);
    /* If denominator is 1, return exact integer */
    if (mpz_cmp_ui(mpq_denref(q), 1) == 0)
        return make_big_from_mpz(mpq_numref(q));
    Rational *r = CURRY_NEW_PINNED_ATOM(Rational);
    r->hdr.type  = T_RATIONAL;
    r->hdr.flags = 0;
    mpq_init(r->q);
    mpq_set(r->q, q);
    GC_register_finalizer_no_order(r, rational_finalize, NULL, NULL, NULL);
    return vptr(r);
}

/* Coerce any number to mpz (exact integer required) */
static void to_mpz(mpz_t out, val_t v) {
    if (vis_fixnum(v)) { mpz_set_si(out, vunfix(v)); return; }
    if (vis_bignum(v)) { mpz_set(out, as_big(v)->z); return; }
    scm_raise(V_FALSE, "exact integer required, got %s",
              vis_rational(v) ? "rational" :
              vis_flonum(v)    ? "inexact real" :
              vis_complex(v)  ? "complex" : "non-numeric value");
}

/* Coerce any (real) number to mpq */
static void to_mpq(mpq_t out, val_t v) {
    if (vis_fixnum(v))   { mpq_set_si(out, vunfix(v), 1); return; }
    if (vis_bignum(v))   { mpq_set_z(out, as_big(v)->z);  return; }
    if (vis_rational(v)) { mpq_set(out, as_rat(v)->q);    return; }
    scm_raise(V_FALSE, "exact rational required, got %s",
              vis_flonum(v)   ? "inexact real" :
              vis_complex(v) ? "complex" : "non-numeric value");
}

/* ---- Constructors ---- */

void num_init(void) { /* GMP available; finalizers handle mpz/mpq cleanup */ }

val_t num_make_bignum_i(long n) {
    if (in_fixnum_range(n)) return vfix(n);
    Bignum *b = CURRY_NEW_PINNED_ATOM(Bignum);
    b->hdr.type  = T_BIGNUM;
    b->hdr.flags = 0;
    mpz_init_set_si(b->z, n);
    GC_register_finalizer_no_order(b, bignum_finalize, NULL, NULL, NULL);
    return vptr(b);
}

val_t num_make_bignum_str(const char *s, int base) {
    mpz_t z;
    mpz_init_set_str(z, s, base);
    val_t r = make_big_from_mpz(z);
    mpz_clear(z);
    return r;
}

val_t num_make_rational(val_t num, val_t den) {
    mpq_t q;
    mpq_init(q);
    mpz_t n, d;
    mpz_init(n); mpz_init(d);
    to_mpz(n, num);
    to_mpz(d, den);
    mpq_set_num(q, n);
    mpq_set_den(q, d);
    val_t result = make_rat_from_mpq(q);
    mpq_clear(q); mpz_clear(n); mpz_clear(d);
    return result;
}

val_t num_make_float(double d) {
    Flonum *f = CURRY_NEW_ATOM(Flonum);
    f->hdr.type  = T_FLONUM;
    f->hdr.flags = 0;
    f->value = d;
    return vptr(f);
}

val_t num_make_complex(val_t real, val_t imag) {
    if (vis_false(imag) || (vis_fixnum(imag) && vunfix(imag) == 0))
        return real;
    Complex *c = CURRY_NEW(Complex);
    c->hdr.type  = T_COMPLEX;
    c->hdr.flags = 0;
    c->real = real;
    c->imag = imag;
    return vptr(c);
}

val_t num_make_quat(double a, double b, double c, double d) {
    Quaternion *q = CURRY_NEW_ATOM(Quaternion);
    q->hdr.type  = T_QUATERNION;
    q->hdr.flags = 0;
    q->a = a; q->b = b; q->c = c; q->d = d;
    return vptr(q);
}

val_t num_make_oct(const double e[8]) {
    Octonion *o = CURRY_NEW_ATOM(Octonion);
    o->hdr.type  = T_OCTONION;
    o->hdr.flags = 0;
    memcpy(o->e, e, 8 * sizeof(double));
    return vptr(o);
}

val_t num_make_tuple(int type, uint32_t n, val_t *data) {
    Tuple *t = CURRY_NEW_FLEX(Tuple, n);
    t->hdr.type  = (uint32_t)type;
    t->hdr.flags = 0;
    t->len       = n;
    for (uint32_t i = 0; i < n; i++) t->data[i] = data[i];
    return vptr(t);
}

/* ---- Coercions ---- */

double num_to_double(val_t v) {
    if (vis_fixnum(v))   return (double)vunfix(v);
    if (vis_flonum(v))   return vfloat(v);
    if (vis_bignum(v))   return mpz_get_d(as_big(v)->z);
    if (vis_rational(v)) return mpq_get_d(as_rat(v)->q);
    if (vis_complex(v))  return num_to_double(as_cpx(v)->real); /* drop imag */
    if (vis_surreal(v))  return sur_to_double(v);
#ifdef BUILD_MPFR
    if (vis_mpfr(v))     return mpfr_to_double(v);
#endif
    scm_raise(V_FALSE, "not a number: %s",
              vis_pair(v) ? "#<pair>" :
              vis_nil(v)  ? "()"      :
              (v == V_TRUE) ? "#t"    :
              (v == V_FALSE) ? "#f"   : "#<non-numeric>");
}

long num_to_long(val_t v) {
    if (vis_fixnum(v)) return vunfix(v);
    if (vis_bignum(v)) return mpz_get_si(as_big(v)->z);
    scm_raise(V_FALSE, "not an exact integer: #<non-numeric>");
}

val_t num_inexact(val_t v) {
    if (vis_flonum(v)) return v;
#ifdef BUILD_MPFR
    if (vis_mpfr(v)) return v;
    if (tl_mpfr_prec > 53)
        return mpfr_coerce(v, tl_mpfr_prec);
#endif
    return num_make_float(num_to_double(v));
}

val_t num_exact(val_t v) {
#ifdef BUILD_MPFR
    if (vis_mpfr(v)) return mpfr_to_exact(v);
#endif
    if (!vis_flonum(v)) return v;
    double d = vfloat(v);
    /* Convert double to exact rational via GMP */
    mpq_t q;
    mpq_init(q);
    mpq_set_d(q, d);
    val_t r = make_rat_from_mpq(q);
    mpq_clear(q);
    return r;
}

/* ---- Predicates ---- */

bool num_is_zero(val_t v) {
    if (vis_fixnum(v))   return vunfix(v) == 0;
    if (vis_flonum(v))   return vfloat(v) == 0.0;
    if (vis_bignum(v))   return mpz_sgn(as_big(v)->z) == 0;
    if (vis_rational(v)) return mpq_sgn(as_rat(v)->q) == 0;
    if (vis_complex(v))  return num_is_zero(as_cpx(v)->real) && num_is_zero(as_cpx(v)->imag);
    if (vis_quat(v))    { Quaternion *q = as_quat(v); return q->a==0.0 && q->b==0.0 && q->c==0.0 && q->d==0.0; }
    if (vis_surreal(v))  return sur_is_zero(v);
#ifdef BUILD_MPFR
    if (vis_mpfr(v))     return mpfr_is_zero(v);
#endif
    return false;
}

bool num_is_one(val_t v) {
    if (vis_fixnum(v))   return vunfix(v) == 1;
    if (vis_flonum(v))   return vfloat(v) == 1.0;
    if (vis_bignum(v))   return mpz_cmp_si(as_big(v)->z, 1) == 0;
    if (vis_rational(v)) return mpq_cmp_si(as_rat(v)->q, 1, 1) == 0;
    if (vis_complex(v))  return num_is_one(as_cpx(v)->real) && num_is_zero(as_cpx(v)->imag);
    if (vis_quat(v))    { Quaternion *q = as_quat(v); return q->a==1.0 && q->b==0.0 && q->c==0.0 && q->d==0.0; }
    return false;
}

bool num_is_positive(val_t v) {
    if (vis_fixnum(v))   return vunfix(v) > 0;
    if (vis_flonum(v))   return vfloat(v) > 0.0;
    if (vis_bignum(v))   return mpz_sgn(as_big(v)->z) > 0;
    if (vis_rational(v)) return mpq_sgn(as_rat(v)->q) > 0;
    if (vis_surreal(v))  return sur_is_positive(v);
#ifdef BUILD_MPFR
    if (vis_mpfr(v))     return mpfr_is_positive(v);
#endif
    return false;
}

bool num_is_negative(val_t v) {
    if (vis_fixnum(v))   return vunfix(v) < 0;
    if (vis_flonum(v))   return vfloat(v) < 0.0;
    if (vis_bignum(v))   return mpz_sgn(as_big(v)->z) < 0;
    if (vis_rational(v)) return mpq_sgn(as_rat(v)->q) < 0;
    if (vis_surreal(v))  return sur_is_negative(v);
#ifdef BUILD_MPFR
    if (vis_mpfr(v))     return mpfr_is_negative(v);
#endif
    return false;
}

bool num_is_finite(val_t v)   {
    if (vis_surreal(v)) return sur_finite_p(v);
#ifdef BUILD_MPFR
    if (vis_mpfr(v))    return !mpfr_is_nan(v) && !mpfr_is_inf(v);
#endif
    return !vis_flonum(v) || isfinite(vfloat(v));
}

bool num_is_infinite(val_t v) {
    if (vis_surreal(v)) return sur_infinite_p(v);
#ifdef BUILD_MPFR
    if (vis_mpfr(v))    return mpfr_is_inf(v);
#endif
    return vis_flonum(v) && isinf(vfloat(v));
}

bool num_is_nan(val_t v)      {
#ifdef BUILD_MPFR
    if (vis_mpfr(v)) return mpfr_is_nan(v);
#endif
    return vis_flonum(v) && isnan(vfloat(v));
}

bool num_is_integer(val_t v) {
    if (vis_fixnum(v) || vis_bignum(v)) return true;
    if (vis_flonum(v)) { double d = vfloat(v); return isfinite(d) && d == trunc(d); }
    if (vis_rational(v)) return mpz_cmp_ui(mpq_denref(as_rat(v)->q), 1) == 0;
#ifdef BUILD_MPFR
    if (vis_mpfr(v))     return mpfr_is_integer(v);
#endif
    return false;
}

/* ---- Tuple helpers ---- */

/* Unpack a quaternion or scalar-coerced value into four doubles.
 * Scalar → (val, 0, 0, 0).  Used by num_add/sub/mul/div/cmp. */
#define UNPACK_QUAT(v, r0, r1, r2, r3) \
    do { if (vis_quat(v)) { Quaternion *_q = as_quat(v); \
             (r0)=_q->a; (r1)=_q->b; (r2)=_q->c; (r3)=_q->d; } \
         else { (r0)=num_to_double(v); (r1)=(r2)=(r3)=0.0; } } while (0)

/* Apply a binary numeric op element-wise to two same-type tuples. */
static val_t tuple_binop(val_t a, val_t b, val_t (*op)(val_t, val_t), const char *ctx) {
    /* Issue #134 (found via independent security review of the
     * symbolic-CAS fix): a nested tuple (built in O(1) per level via
     * `up`/`down`, with no simplification pass to cap depth the way
     * sx_simplify does) recurses here once per level of nesting when
     * `op` (num_neg/num_add/etc) dispatches back into a nested tuple
     * element, with no bound. Same guard, same class as symbolic.c's
     * own tree-walkers. */
    check_c_stack_depth("numeric");
    Tuple *ta = as_tuple(a), *tb = as_tuple(b);
    if (ta->hdr.type != tb->hdr.type || ta->len != tb->len)
        scm_raise(V_FALSE, "tuple %s: type/dimension mismatch", ctx);
    val_t buf[256]; uint32_t n = ta->len < 256 ? ta->len : 256;
    for (uint32_t i = 0; i < n; i++) buf[i] = op(ta->data[i], tb->data[i]);
    return num_make_tuple((int)ta->hdr.type, n, buf);
}

/* Apply a unary numeric op element-wise to a tuple. */
static val_t tuple_unop(val_t a, val_t (*op)(val_t)) {
    /* Same unbounded-recursion class as tuple_binop above (issue #134). */
    check_c_stack_depth("numeric");
    Tuple *t = as_tuple(a);
    val_t buf[256]; uint32_t n = t->len < 256 ? t->len : 256;
    for (uint32_t i = 0; i < n; i++) buf[i] = op(t->data[i]);
    return num_make_tuple((int)t->hdr.type, n, buf);
}

/* ---- Arithmetic helpers for exact/inexact promotion ---- */

/* Promote both operands to the same type and apply fn */
static val_t arith2(val_t a, val_t b,
                    val_t (*fix_fix)(intptr_t, intptr_t),
                    val_t (*big_big)(val_t, val_t),
                    val_t (*rat_rat)(val_t, val_t),
                    val_t (*flo_flo)(double, double)) {
    /* If either is inexact, both become inexact */
    if (vis_flonum(a) || vis_flonum(b))
        return flo_flo(num_to_double(a), num_to_double(b));
    /* Both exact */
    if (vis_fixnum(a) && vis_fixnum(b))
        return fix_fix(vunfix(a), vunfix(b));
    /* Promote to rational if needed */
    if (vis_rational(a) || vis_rational(b))
        return rat_rat(a, b);
    /* Both bignum or fixnum/bignum mix */
    return big_big(a, b);
}

/* ---- add ---- */

static val_t add_fix(intptr_t a, intptr_t b) {
    /* Overflow-safe addition */
    intptr_t r = a + b;
    if (in_fixnum_range(r)) return vfix(r);
    /* Overflow: promote to bignum */
    mpz_t z; mpz_init(z);
    mpz_set_si(z, a); mpz_add_ui(z, z, b > 0 ? (unsigned long)b : 0);
    /* simpler: use mpz directly */
    mpz_t za, zb; mpz_init_set_si(za, a); mpz_init_set_si(zb, b);
    mpz_add(z, za, zb);
    val_t res = make_big_from_mpz(z);
    mpz_clear(z); mpz_clear(za); mpz_clear(zb);
    return res;
}
static val_t add_big(val_t a, val_t b) {
    mpz_t za, zb, zr; mpz_init(za); mpz_init(zb); mpz_init(zr);
    to_mpz(za, a); to_mpz(zb, b); mpz_add(zr, za, zb);
    val_t r = make_big_from_mpz(zr);
    mpz_clear(za); mpz_clear(zb); mpz_clear(zr);
    return r;
}
static val_t add_rat(val_t a, val_t b) {
    mpq_t qa, qb, qr; mpq_init(qa); mpq_init(qb); mpq_init(qr);
    to_mpq(qa, a); to_mpq(qb, b); mpq_add(qr, qa, qb);
    val_t r = make_rat_from_mpq(qr);
    mpq_clear(qa); mpq_clear(qb); mpq_clear(qr);
    return r;
}
static val_t add_flo(double a, double b) { return num_make_float(a + b); }

val_t num_add(val_t a, val_t b) {
    /* Fast paths: fixnum check is a single tag-bit test (no memory dereference);
     * flonum check requires loading the type header — so fixnum always goes first. */
    if (vis_fixnum(a)) {
        if (vis_fixnum(b)) return add_fix(vunfix(a), vunfix(b));
        if (vis_flonum(b)) return num_make_float((double)vunfix(a) + vfloat(b));
    } else if (vis_flonum(a)) {
        if (vis_fixnum(b)) return num_make_float(vfloat(a) + (double)vunfix(b));
        if (vis_flonum(b)) return num_make_float(vfloat(a) + vfloat(b));
    }
#ifdef BUILD_MPFR
    if (vis_mpfr(a) || vis_mpfr(b)) return mpfr_num_add(a, b);
#endif
    /* Tuple addition distributes before symbolic so that 0+tuple and tuple+0
       work correctly when the zero is symbolic (e.g. from variadic accumulator). */
    if (vis_tuple(a) || vis_tuple(b)) {
        if (!vis_tuple(a) && num_is_zero(a)) return b;
        if (!vis_tuple(b) && num_is_zero(b)) return a;
        if (!vis_tuple(a) || !vis_tuple(b))
            scm_raise(V_FALSE, "tuple +: cannot add tuple with non-zero scalar");
        return tuple_binop(a, b, num_add, "+");
    }
    if (vis_symbolic(a) || vis_symbolic(b)) return sx_add(a, b);
    if (vis_quantum(a) || vis_quantum(b)) {
        if (vis_quantum(a) && vis_quantum(b)) return quantum_superpose(a, b);
        return vis_quantum(a) ? quantum_add_scalar(a, b) : quantum_add_scalar(b, a);
    }
    if (vis_surreal(a) || vis_surreal(b)) return sur_add(a, b);
    if (vis_complex(a) || vis_complex(b)) {
        val_t ar = vis_complex(a) ? as_cpx(a)->real : a;
        val_t ai = vis_complex(a) ? as_cpx(a)->imag : vfix(0);
        val_t br = vis_complex(b) ? as_cpx(b)->real : b;
        val_t bi = vis_complex(b) ? as_cpx(b)->imag : vfix(0);
        return num_make_complex(num_add(ar, br), num_add(ai, bi));
    }
    if (vis_quat(a) || vis_quat(b)) {
        double a0=0,a1=0,a2=0,a3=0, b0=0,b1=0,b2=0,b3=0;
        UNPACK_QUAT(a, a0,a1,a2,a3);
        UNPACK_QUAT(b, b0,b1,b2,b3);
        return num_make_quat(a0+b0, a1+b1, a2+b2, a3+b3);
    }
    if (vis_oct(a) || vis_oct(b)) {
        double e[8] = {0};
        if (vis_oct(a)) for(int i=0;i<8;i++) e[i] += as_oct(a)->e[i];
        else e[0] += num_to_double(a);
        if (vis_oct(b)) for(int i=0;i<8;i++) e[i] += as_oct(b)->e[i];
        else e[0] += num_to_double(b);
        return num_make_oct(e);
    }
    return arith2(a, b, add_fix, add_big, add_rat, add_flo);
}

/* ---- sub ---- */
val_t num_neg(val_t a) {
    if (vis_symbolic(a)) return sx_neg(a);
    if (vis_quantum(a))  return quantum_mul_scalar(a, vfix(-1));
    if (vis_surreal(a))  return sur_neg(a);
    if (vis_tuple(a))    return tuple_unop(a, num_neg);
#ifdef BUILD_MPFR
    if (vis_mpfr(a))     return mpfr_num_neg(a);
#endif
    return num_sub(vfix(0), a);
}

static val_t sub_fix(intptr_t a, intptr_t b) {
    intptr_t r = a - b;
    if (in_fixnum_range(r)) return vfix(r);
    mpz_t za, zb, zr; mpz_init_set_si(za,a); mpz_init_set_si(zb,b); mpz_init(zr);
    mpz_sub(zr, za, zb);
    val_t res = make_big_from_mpz(zr);
    mpz_clear(za); mpz_clear(zb); mpz_clear(zr);
    return res;
}
static val_t sub_big(val_t a, val_t b) {
    mpz_t za,zb,zr; mpz_init(za); mpz_init(zb); mpz_init(zr);
    to_mpz(za,a); to_mpz(zb,b); mpz_sub(zr,za,zb);
    val_t r = make_big_from_mpz(zr); mpz_clear(za); mpz_clear(zb); mpz_clear(zr); return r;
}
static val_t sub_rat(val_t a, val_t b) {
    mpq_t qa,qb,qr; mpq_init(qa); mpq_init(qb); mpq_init(qr);
    to_mpq(qa,a); to_mpq(qb,b); mpq_sub(qr,qa,qb);
    val_t r = make_rat_from_mpq(qr); mpq_clear(qa); mpq_clear(qb); mpq_clear(qr); return r;
}
static val_t sub_flo(double a, double b) { return num_make_float(a - b); }

val_t num_sub(val_t a, val_t b) {
    if (vis_fixnum(a)) {
        if (vis_fixnum(b)) return sub_fix(vunfix(a), vunfix(b));
        if (vis_flonum(b)) return num_make_float((double)vunfix(a) - vfloat(b));
    } else if (vis_flonum(a)) {
        if (vis_fixnum(b)) return num_make_float(vfloat(a) - (double)vunfix(b));
        if (vis_flonum(b)) return num_make_float(vfloat(a) - vfloat(b));
    }
#ifdef BUILD_MPFR
    if (vis_mpfr(a) || vis_mpfr(b)) return mpfr_num_sub(a, b);
#endif
    /* Tuple subtraction before symbolic for same reason as num_add. */
    if (vis_tuple(a) || vis_tuple(b)) {
        if (!vis_tuple(b) && num_is_zero(b)) return a;
        if (!vis_tuple(a) || !vis_tuple(b))
            scm_raise(V_FALSE, "tuple -: cannot subtract tuple and non-zero scalar");
        return tuple_binop(a, b, num_sub, "-");
    }
    if (vis_symbolic(a) || vis_symbolic(b)) return sx_sub(a, b);
    if (vis_quantum(a) || vis_quantum(b)) {
        if (vis_quantum(a) && vis_quantum(b)) return quantum_superpose(a, quantum_mul_scalar(b, vfix(-1)));
        return vis_quantum(a) ? quantum_sub_scalar(a, b) : quantum_sub_scalar(b, num_neg(a));
    }
    if (vis_surreal(a) || vis_surreal(b)) return sur_sub(a, b);
    if (vis_complex(a) || vis_complex(b)) {
        val_t ar = vis_complex(a) ? as_cpx(a)->real : a,  ai = vis_complex(a) ? as_cpx(a)->imag : vfix(0);
        val_t br = vis_complex(b) ? as_cpx(b)->real : b,  bi = vis_complex(b) ? as_cpx(b)->imag : vfix(0);
        return num_make_complex(num_sub(ar, br), num_sub(ai, bi));
    }
    if (vis_quat(a) || vis_quat(b)) {
        double a0=0,a1=0,a2=0,a3=0, b0=0,b1=0,b2=0,b3=0;
        UNPACK_QUAT(a, a0,a1,a2,a3);
        UNPACK_QUAT(b, b0,b1,b2,b3);
        return num_make_quat(a0-b0, a1-b1, a2-b2, a3-b3);
    }
    return arith2(a, b, sub_fix, sub_big, sub_rat, sub_flo);
}

/* ---- mul ---- */
static val_t mul_fix(intptr_t a, intptr_t b) {
    /* Check overflow with __int128 on GCC/Clang */
    __int128 r = (__int128)a * b;
    if (r >= FIXNUM_MIN && r <= FIXNUM_MAX) return vfix((intptr_t)r);
    mpz_t z; mpz_init(z); mpz_set_si(z, a); mpz_mul_si(z, z, b);
    val_t res = make_big_from_mpz(z); mpz_clear(z); return res;
}
static val_t mul_big(val_t a, val_t b) {
    mpz_t za,zb,zr; mpz_init(za); mpz_init(zb); mpz_init(zr);
    to_mpz(za,a); to_mpz(zb,b); mpz_mul(zr,za,zb);
    val_t r = make_big_from_mpz(zr); mpz_clear(za); mpz_clear(zb); mpz_clear(zr); return r;
}
static val_t mul_rat(val_t a, val_t b) {
    mpq_t qa,qb,qr; mpq_init(qa); mpq_init(qb); mpq_init(qr);
    to_mpq(qa,a); to_mpq(qb,b); mpq_mul(qr,qa,qb);
    val_t r = make_rat_from_mpq(qr); mpq_clear(qa); mpq_clear(qb); mpq_clear(qr); return r;
}
static val_t mul_flo(double a, double b) { return num_make_float(a * b); }

val_t num_mul(val_t a, val_t b) {
    if (vis_fixnum(a)) {
        if (vis_fixnum(b)) return mul_fix(vunfix(a), vunfix(b));
        if (vis_flonum(b)) return num_make_float((double)vunfix(a) * vfloat(b));
    } else if (vis_flonum(a)) {
        if (vis_fixnum(b)) return num_make_float(vfloat(a) * (double)vunfix(b));
        if (vis_flonum(b)) return num_make_float(vfloat(a) * vfloat(b));
    }
#ifdef BUILD_MPFR
    if (vis_mpfr(a) || vis_mpfr(b)) return mpfr_num_mul(a, b);
#endif
    /* Tuple distribution takes priority over symbolic so that sym-var × up-tuple
       distributes component-wise rather than wrapping in a CAS expression. */
    if (vis_tuple(a) || vis_tuple(b)) {
        /* Issue #134: unlike num_add/num_sub/num_neg (which all route
         * through the already-guarded tuple_binop/tuple_unop), this is
         * num_mul's own separate inline tuple-distribution loop, which
         * recurses into itself (num_mul(scalar, t->data[i])) once per
         * level of a nested (up-wrapped) tuple with no bound. Found via
         * independent security review after tuple_binop/tuple_unop's
         * own guards turned out not to cover this path. */
        check_c_stack_depth("numeric");
        if (vis_tuple(a) && vis_tuple(b)) {
            if (!vis_down(a) || !vis_up(b))
                scm_raise(V_FALSE, "tuple *: only (down)*(up) contraction is defined");
            Tuple *ta = as_tuple(a), *tb = as_tuple(b);
            if (ta->len != tb->len)
                scm_raise(V_FALSE, "tuple *: dimension mismatch in contraction");
            val_t acc = vfix(0);
            for (uint32_t i = 0; i < ta->len; i++)
                acc = num_add(acc, num_mul(ta->data[i], tb->data[i]));
            return acc;
        }
        if (!vis_tuple(a) && num_is_one(a)) return b;
        if (!vis_tuple(b) && num_is_one(b)) return a;
        val_t scalar = vis_tuple(a) ? b : a;
        Tuple *t     = vis_tuple(a) ? as_tuple(a) : as_tuple(b);
        val_t buf[256]; uint32_t n = t->len < 256 ? t->len : 256;
        for (uint32_t i = 0; i < n; i++) buf[i] = num_mul(scalar, t->data[i]);
        return num_make_tuple((int)t->hdr.type, n, buf);
    }
    if (vis_symbolic(a) || vis_symbolic(b)) return sx_mul(a, b);
    if (vis_quantum(a) || vis_quantum(b)) {
        if (vis_quantum(a) && vis_quantum(b)) return quantum_superpose(a, b);
        return vis_quantum(a) ? quantum_mul_scalar(a, b) : quantum_mul_scalar(b, a);
    }
    if (vis_surreal(a) || vis_surreal(b)) return sur_mul(a, b);
    /* Quaternion multiplication (non-commutative: Hamilton product) */
    if (vis_quat(a) || vis_quat(b)) {
        double a0=0,a1=0,a2=0,a3=0, b0=0,b1=0,b2=0,b3=0;
        UNPACK_QUAT(a, a0,a1,a2,a3);
        UNPACK_QUAT(b, b0,b1,b2,b3);
        return num_make_quat(
            a0*b0 - a1*b1 - a2*b2 - a3*b3,
            a0*b1 + a1*b0 + a2*b3 - a3*b2,
            a0*b2 - a1*b3 + a2*b0 + a3*b1,
            a0*b3 + a1*b2 - a2*b1 + a3*b0
        );
    }
    /* Complex multiplication */
    if (vis_complex(a) || vis_complex(b)) {
        val_t ar = vis_complex(a) ? as_cpx(a)->real : a, ai = vis_complex(a) ? as_cpx(a)->imag : vfix(0);
        val_t br = vis_complex(b) ? as_cpx(b)->real : b, bi = vis_complex(b) ? as_cpx(b)->imag : vfix(0);
        return num_make_complex(
            num_sub(num_mul(ar,br), num_mul(ai,bi)),
            num_add(num_mul(ar,bi), num_mul(ai,br))
        );
    }
    /* Octonion: Cayley multiplication table */
    if (vis_oct(a) || vis_oct(b)) {
        /* Full octonion multiplication via Cayley-Dickson construction */
        double x[8]={0}, y[8]={0}, z[8]={0};
        if (vis_oct(a)) memcpy(x, as_oct(a)->e, 8*sizeof(double)); else x[0]=num_to_double(a);
        if (vis_oct(b)) memcpy(y, as_oct(b)->e, 8*sizeof(double)); else y[0]=num_to_double(b);
        /* Multiplication table for e1..e7 (Graves/Cayley convention) */
        static const int8_t oct_mul[8][8][2] = {
            /* [a][b] = {index, sign} where e_a * e_b = sign * e_index */
            {{0,1},{1,1},{2,1},{3,1},{4,1},{5,1},{6,1},{7,1}},
            {{1,1},{0,-1},{3,1},{2,-1},{5,1},{4,-1},{7,-1},{6,1}},
            {{2,1},{3,-1},{0,-1},{1,1},{6,1},{7,1},{4,-1},{5,-1}},
            {{3,1},{2,1},{1,-1},{0,-1},{7,1},{6,-1},{5,1},{4,-1}},
            {{4,1},{5,-1},{6,-1},{7,-1},{0,-1},{1,1},{2,1},{3,1}},
            {{5,1},{4,1},{7,-1},{6,1},{1,-1},{0,-1},{3,-1},{2,1}},
            {{6,1},{7,1},{4,1},{5,-1},{2,-1},{3,1},{0,-1},{1,-1}},
            {{7,1},{6,-1},{5,1},{4,1},{3,-1},{2,-1},{1,1},{0,-1}},
        };
        for (int i=0;i<8;i++) for (int j=0;j<8;j++) {
            int idx = oct_mul[i][j][0];
            int sgn = oct_mul[i][j][1];
            z[idx] += sgn * x[i] * y[j];
        }
        return num_make_oct(z);
    }
    return arith2(a, b, mul_fix, mul_big, mul_rat, mul_flo);
}

/* ---- div ---- */
val_t num_div(val_t a, val_t b) {
    if (vis_fixnum(a) && vis_flonum(b)) return num_make_float((double)vunfix(a) / vfloat(b));
    if (vis_flonum(a)) {
        if (vis_fixnum(b)) return num_make_float(vfloat(a) / (double)vunfix(b));
        if (vis_flonum(b)) return num_make_float(vfloat(a) / vfloat(b));
    }
#ifdef BUILD_MPFR
    if (vis_mpfr(a) || vis_mpfr(b)) return mpfr_num_div(a, b);
#endif
    if (vis_symbolic(a) || vis_symbolic(b)) return sx_div(a, b);
    if (vis_quantum(a) || vis_quantum(b)) {
        if (vis_quantum(a) && !vis_quantum(b)) return quantum_div_scalar(a, b);
        scm_raise(V_FALSE, "cannot divide by a quantum value");
    }
    if (vis_surreal(a) || vis_surreal(b)) return sur_div(a, b);
    /* Quaternion division: a/b = a · conj(b) / ‖b‖² */
    if (vis_quat(a) || vis_quat(b)) {
        double a0=0,a1=0,a2=0,a3=0, b0=0,b1=0,b2=0,b3=0;
        UNPACK_QUAT(a, a0,a1,a2,a3);
        UNPACK_QUAT(b, b0,b1,b2,b3);
        double n2 = b0*b0 + b1*b1 + b2*b2 + b3*b3;
        return num_make_quat(
            ( a0*b0 + a1*b1 + a2*b2 + a3*b3) / n2,
            (-a0*b1 + a1*b0 - a2*b3 + a3*b2) / n2,
            (-a0*b2 + a1*b3 + a2*b0 - a3*b1) / n2,
            (-a0*b3 - a1*b2 + a2*b1 + a3*b0) / n2
        );
    }
    /* Complex division: (a+bi)/(c+di) = ((ac+bd)+(bc-ad)i)/(c²+d²) */
    if (vis_complex(a) || vis_complex(b)) {
        val_t ar = vis_complex(a) ? as_cpx(a)->real : a, ai = vis_complex(a) ? as_cpx(a)->imag : vfix(0);
        val_t br = vis_complex(b) ? as_cpx(b)->real : b, bi = vis_complex(b) ? as_cpx(b)->imag : vfix(0);
        val_t denom = num_add(num_mul(br, br), num_mul(bi, bi));
        return num_make_complex(
            num_div(num_add(num_mul(ar, br), num_mul(ai, bi)), denom),
            num_div(num_sub(num_mul(ai, br), num_mul(ar, bi)), denom)
        );
    }
    if (vis_flonum(a) || vis_flonum(b))
        return num_make_float(num_to_double(a) / num_to_double(b));
    /* Exact division -> rational. GMP's mpq_div on a zero divisor is
     * undefined behavior (SIGFPE on most platforms) — must check first. */
    if (num_is_zero(b))
        scm_raise_code(EC_DIVISION_BY_ZERO, "/: division by zero");
    mpq_t qa, qb, qr;
    mpq_init(qa); mpq_init(qb); mpq_init(qr);
    to_mpq(qa, a); to_mpq(qb, b);
    mpq_div(qr, qa, qb);
    val_t r = make_rat_from_mpq(qr);
    mpq_clear(qa); mpq_clear(qb); mpq_clear(qr);
    return r;
}

/* ---- abs ---- */
val_t num_abs(val_t a) {
    if (vis_symbolic(a)) return sx_abs(a);
    if (vis_surreal(a))  return sur_abs(a);
#ifdef BUILD_MPFR
    if (vis_mpfr(a))     return mpfr_num_abs(a);
#endif
    if (vis_fixnum(a)) {
        intptr_t n = vunfix(a);
        return n < 0 ? (in_fixnum_range(-n) ? vfix(-n) : num_make_bignum_i(-n)) : a;
    }
    if (vis_flonum(a))   return num_make_float(fabs(vfloat(a)));
    if (vis_bignum(a))   { mpz_t z; mpz_init(z); mpz_abs(z, as_big(a)->z); val_t r=make_big_from_mpz(z); mpz_clear(z); return r; }
    if (vis_rational(a)) { mpq_t q; mpq_init(q); mpq_abs(q, as_rat(a)->q); val_t r=make_rat_from_mpq(q); mpq_clear(q); return r; }
    if (vis_quat(a)) {
        Quaternion *q = as_quat(a);
        return num_make_float(sqrt(q->a*q->a + q->b*q->b + q->c*q->c + q->d*q->d));
    }
    return a;
}

/* ---- Comparison ---- */
int num_cmp(val_t a, val_t b) {
    /* Quaternions have no total order — only equality is meaningful */
    if (vis_quat(a) || vis_quat(b)) {
        double a0=0,a1=0,a2=0,a3=0, b0=0,b1=0,b2=0,b3=0;
        UNPACK_QUAT(a, a0,a1,a2,a3);
        UNPACK_QUAT(b, b0,b1,b2,b3);
        return (a0==b0 && a1==b1 && a2==b2 && a3==b3) ? 0 : 1;
    }
    /* Fast paths */
    if (vis_flonum(a) && vis_flonum(b)) {
        double da = vfloat(a), db = vfloat(b);
        return da < db ? -1 : da > db ? 1 : 0;
    }
    if (vis_fixnum(a) && vis_fixnum(b)) {
        intptr_t ia = vunfix(a), ib = vunfix(b);
        return ia < ib ? -1 : ia > ib ? 1 : 0;
    }
    if (vis_fixnum(a) && vis_flonum(b)) {
        double da = (double)vunfix(a), db = vfloat(b);
        return da < db ? -1 : da > db ? 1 : 0;
    }
    if (vis_flonum(a) && vis_fixnum(b)) {
        double da = vfloat(a), db = (double)vunfix(b);
        return da < db ? -1 : da > db ? 1 : 0;
    }
    if (vis_surreal(a) || vis_surreal(b)) return sur_compare(a, b);
#ifdef BUILD_MPFR
    if (vis_mpfr(a) || vis_mpfr(b)) return mpfr_num_cmp(a, b);
#endif
    if (vis_flonum(a) || vis_flonum(b)) {
        double da = num_to_double(a), db = num_to_double(b);
        return da < db ? -1 : da > db ? 1 : 0;
    }
    if (vis_fixnum(a) && vis_fixnum(b)) {
        intptr_t ia = vunfix(a), ib = vunfix(b);
        return ia < ib ? -1 : ia > ib ? 1 : 0;
    }
    if (vis_rational(a) || vis_rational(b)) {
        mpq_t qa, qb; mpq_init(qa); mpq_init(qb);
        to_mpq(qa, a); to_mpq(qb, b);
        int r = mpq_cmp(qa, qb);
        mpq_clear(qa); mpq_clear(qb);
        return r < 0 ? -1 : r > 0 ? 1 : 0;
    }
    mpz_t za, zb; mpz_init(za); mpz_init(zb);
    to_mpz(za, a); to_mpz(zb, b);
    int r = mpz_cmp(za, zb);
    mpz_clear(za); mpz_clear(zb);
    return r < 0 ? -1 : r > 0 ? 1 : 0;
}

bool num_eq(val_t a, val_t b) { return num_cmp(a,b)==0; }
bool num_lt(val_t a, val_t b) { return num_cmp(a,b)<0;  }
bool num_le(val_t a, val_t b) { return num_cmp(a,b)<=0; }
bool num_gt(val_t a, val_t b) { return num_cmp(a,b)>0;  }
bool num_ge(val_t a, val_t b) { return num_cmp(a,b)>=0; }
val_t num_min(val_t a, val_t b) {
    if (vis_complex(a) || vis_complex(b))
        scm_raise(V_FALSE, "no ordering on complex numbers");
    return num_le(a,b)?a:b;
}
val_t num_max(val_t a, val_t b) {
    if (vis_complex(a) || vis_complex(b))
        scm_raise(V_FALSE, "no ordering on complex numbers");
    return num_ge(a,b)?a:b;
}

/* ---- Integer division ---- */
val_t num_quotient(val_t a, val_t b) {
    if (vis_fixnum(a) && vis_fixnum(b)) {
        intptr_t ia = vunfix(a), ib = vunfix(b);
        if (ib == 0) scm_raise_code(EC_DIVISION_BY_ZERO, "quotient: division by zero");
        return vfix(ia / ib);  /* C11 truncation towards zero */
    }
    if (num_is_zero(b)) scm_raise_code(EC_DIVISION_BY_ZERO, "quotient: division by zero");
    mpz_t za,zb,zr; mpz_init(za); mpz_init(zb); mpz_init(zr);
    to_mpz(za,a); to_mpz(zb,b); mpz_tdiv_q(zr,za,zb);
    val_t r=make_big_from_mpz(zr); mpz_clear(za); mpz_clear(zb); mpz_clear(zr); return r;
}

val_t num_remainder(val_t a, val_t b) {
    if (vis_fixnum(a) && vis_fixnum(b)) {
        intptr_t ib = vunfix(b);
        if (ib == 0) scm_raise_code(EC_DIVISION_BY_ZERO, "remainder: division by zero");
        return vfix(vunfix(a) % ib);
    }
    if (num_is_zero(b)) scm_raise_code(EC_DIVISION_BY_ZERO, "remainder: division by zero");
    mpz_t za,zb,zr; mpz_init(za); mpz_init(zb); mpz_init(zr);
    to_mpz(za,a); to_mpz(zb,b); mpz_tdiv_r(zr,za,zb);
    val_t r=make_big_from_mpz(zr); mpz_clear(za); mpz_clear(zb); mpz_clear(zr); return r;
}

val_t num_modulo(val_t a, val_t b) {
    /* Modulo has the same sign as b */
    val_t r = num_remainder(a, b);
    if (!num_is_zero(r) && (num_is_negative(r) != num_is_negative(b)))
        r = num_add(r, b);
    return r;
}

val_t num_gcd(val_t a, val_t b) {
    mpz_t za,zb,zr; mpz_init(za); mpz_init(zb); mpz_init(zr);
    to_mpz(za,a); to_mpz(zb,b);
    mpz_abs(za,za); mpz_abs(zb,zb);
    mpz_gcd(zr,za,zb);
    val_t r=make_big_from_mpz(zr); mpz_clear(za); mpz_clear(zb); mpz_clear(zr); return r;
}

val_t num_lcm(val_t a, val_t b) {
    mpz_t za,zb,zr; mpz_init(za); mpz_init(zb); mpz_init(zr);
    to_mpz(za,a); to_mpz(zb,b);
    mpz_abs(za,za); mpz_abs(zb,zb);
    mpz_lcm(zr,za,zb);
    val_t r=make_big_from_mpz(zr); mpz_clear(za); mpz_clear(zb); mpz_clear(zr); return r;
}

/* ---- Rounding ---- */
val_t num_floor(val_t v) {
    if (vis_complex(v)) scm_raise(V_FALSE, "no ordering on complex numbers");
#ifdef BUILD_MPFR
    if (vis_mpfr(v))  return mpfr_num_floor(v);
#endif
    if (vis_flonum(v)) return num_make_float(floor(vfloat(v)));
    if (vis_rational(v)) {
        mpz_t r; mpz_init(r);
        mpz_fdiv_q(r, mpq_numref(as_rat(v)->q), mpq_denref(as_rat(v)->q));
        val_t res = make_big_from_mpz(r); mpz_clear(r); return res;
    }
    return v; /* fixnum/bignum already exact integer */
}
val_t num_ceiling(val_t v) {
    if (vis_complex(v)) scm_raise(V_FALSE, "no ordering on complex numbers");
#ifdef BUILD_MPFR
    if (vis_mpfr(v))  return mpfr_num_ceiling(v);
#endif
    if (vis_flonum(v)) return num_make_float(ceil(vfloat(v)));
    if (vis_rational(v)) {
        mpz_t r; mpz_init(r);
        mpz_cdiv_q(r, mpq_numref(as_rat(v)->q), mpq_denref(as_rat(v)->q));
        val_t res = make_big_from_mpz(r); mpz_clear(r); return res;
    }
    return v;
}
val_t num_truncate(val_t v) {
    if (vis_complex(v)) scm_raise(V_FALSE, "no ordering on complex numbers");
#ifdef BUILD_MPFR
    if (vis_mpfr(v))  return mpfr_num_truncate(v);
#endif
    if (vis_flonum(v)) return num_make_float(trunc(vfloat(v)));
    if (vis_rational(v)) {
        mpz_t r; mpz_init(r);
        mpz_tdiv_q(r, mpq_numref(as_rat(v)->q), mpq_denref(as_rat(v)->q));
        val_t res = make_big_from_mpz(r); mpz_clear(r); return res;
    }
    return v;
}
val_t num_round(val_t v) {
    if (vis_complex(v)) scm_raise(V_FALSE, "no ordering on complex numbers");
#ifdef BUILD_MPFR
    if (vis_mpfr(v))  return mpfr_num_round(v);
#endif
    if (vis_flonum(v)) {
        double d = vfloat(v);
        double rounded = round(d);
        /* Break tie: round to even */
        if (fabs(d - rounded) == 0.5 && fmod(rounded, 2.0) != 0.0)
            rounded -= (rounded > d) ? 1.0 : -1.0;
        return num_make_float(rounded);
    }
    if (vis_rational(v)) {
        /* Banker's rounding: compute floor(n/d), then check remainder * 2 vs d */
        mpz_t q, r, two_r, d; mpz_init(q); mpz_init(r); mpz_init(two_r); mpz_init(d);
        mpz_fdiv_qr(q, r, mpq_numref(as_rat(v)->q), mpq_denref(as_rat(v)->q));
        mpz_set(d, mpq_denref(as_rat(v)->q));
        mpz_mul_2exp(two_r, r, 1); /* two_r = 2*r */
        int cmp = mpz_cmp(two_r, d);
        if (cmp > 0) mpz_add_ui(q, q, 1);          /* round up */
        else if (cmp == 0 && mpz_odd_p(q)) mpz_add_ui(q, q, 1); /* tie: to even */
        val_t res = make_big_from_mpz(q);
        mpz_clear(q); mpz_clear(r); mpz_clear(two_r); mpz_clear(d);
        return res;
    }
    return v;
}

/* ---- Transcendentals ---- */
val_t num_expt(val_t base, val_t exp) {
    if (vis_symbolic(base) || vis_symbolic(exp)) return sx_expt(base, exp);
    if (vis_surreal(base)) return sur_expt(base, exp);
    /* Integer/rational base ^ fixnum exponent: use GMP for efficiency */
    if ((vis_fixnum(base) || vis_bignum(base) || vis_rational(base)) && vis_fixnum(exp)) {
        long e = vunfix(exp);
        if (e == 0) return vfix(1);
        if (e == 1) return base;
        if (e > 0) {
            if (vis_fixnum(base)) {
                mpz_t z; mpz_init(z);
                mpz_set_si(z, vunfix(base));
                mpz_pow_ui(z, z, (unsigned long)e);
                val_t r = make_big_from_mpz(z); mpz_clear(z); return r;
            }
            if (vis_bignum(base)) {
                mpz_t z; mpz_init(z);
                mpz_pow_ui(z, as_big(base)->z, (unsigned long)e);
                val_t r = make_big_from_mpz(z); mpz_clear(z); return r;
            }
            /* vis_rational */
            mpz_t n, d; mpz_init(n); mpz_init(d);
            mpz_pow_ui(n, mpq_numref(as_rat(base)->q), (unsigned long)e);
            mpz_pow_ui(d, mpq_denref(as_rat(base)->q), (unsigned long)e);
            val_t r = num_make_rational(make_big_from_mpz(n), make_big_from_mpz(d));
            mpz_clear(n); mpz_clear(d); return r;
        }
        /* e < 0: b^-n = 1 / b^n */
        return num_div(vfix(1), num_expt(base, vfix(-e)));
    }
    /* Any numeric base ^ fixnum exponent: repeated squaring via num_mul */
    if (vis_fixnum(exp) && !vis_surreal(base) && !vis_symbolic(base)) {
        long e = vunfix(exp);
        if (e == 0) return vfix(1);
        if (e == 1) return base;
        if (e < 0) return num_div(vfix(1), num_expt(base, vfix(-e)));
        val_t result = vfix(1), b = base;
        for (long n = e; n > 0; n >>= 1) {
            if (n & 1) result = num_mul(result, b);
            if (n > 1) b = num_mul(b, b);
        }
        return result;
    }
    return num_make_float(pow(num_to_double(base), num_to_double(exp)));
}

/* Rebuild a quaternion with the same unit-pure direction as q, but with the given
   scalar part and pure-part magnitude.  When q has no pure part (pure real), the
   pure result lands on the canonical +i axis so the principal root is well-defined. */
static val_t quat_assemble(const Quaternion *q, double scalar, double vscale) {
    double b = q->b, c = q->c, d = q->d;
    double vnorm2 = b*b + c*c + d*d;
    if (vnorm2 < 1e-300)
        return num_make_quat(scalar, vscale, 0.0, 0.0);
    double s = vscale / sqrt(vnorm2);
    return num_make_quat(scalar, b*s, c*s, d*s);
}

val_t num_sqrt(val_t v) {
    if (vis_symbolic(v)) return sx_sqrt(v);
    if (vis_surreal(v))  return sur_expt(v, num_make_rational(vfix(1), vfix(2)));
#ifdef BUILD_MPFR
    if (vis_mpfr(v)) {
        if (mpfr_sgn(as_mpfr(v)->x) < 0) {
            /* sqrt of negative real → imaginary result */
            val_t neg = mpfr_num_neg(v);
            return num_make_complex(num_make_float(0.0), mpfr_num_sqrt(neg));
        }
        return mpfr_num_sqrt(v);
    }
#endif
    if (vis_quat(v)) {
        /* sqrt(q) = sqrt((|q|+a)/2) + v̂·sqrt((|q|−a)/2)  (principal root) */
        Quaternion *q = as_quat(v);
        double a = q->a, b = q->b, c = q->c, d = q->d;
        double r = sqrt(a*a + b*b + c*c + d*d);
        return quat_assemble(q, sqrt((r + a) * 0.5), sqrt((r - a) * 0.5));
    }
    if (vis_complex(v)) {
        double a = num_to_double(as_cpx(v)->real), b = num_to_double(as_cpx(v)->imag);
        double r = sqrt(sqrt(a*a + b*b)), theta = atan2(b, a) / 2.0;
        return num_make_complex(num_make_float(r * cos(theta)), num_make_float(r * sin(theta)));
    }
    if (vis_exact(v)) {
        /* Try exact integer sqrt */
        if (vis_fixnum(v) || vis_bignum(v)) {
            mpz_t z, s; mpz_init(z); mpz_init(s);
            to_mpz(z, v);
            if (mpz_sgn(z) >= 0 && mpz_perfect_square_p(z)) {
                mpz_sqrt(s, z);
                val_t r = make_big_from_mpz(s);
                mpz_clear(z); mpz_clear(s); return r;
            }
            mpz_clear(z); mpz_clear(s);
        }
    }
    double x = num_to_double(v);
    if (x < 0.0) /* sqrt of negative real → imaginary result */
        return num_make_complex(num_make_float(0.0), num_make_float(sqrt(-x)));
    return num_make_float(sqrt(x));
}

val_t num_asin(val_t v) {
    if (vis_symbolic(v)) return sx_asin(v);
#ifdef BUILD_MPFR
    if (vis_mpfr(v)) return mpfr_num_asin(v);
#endif
    if (vis_quat(v)) {
        /* Project to complex plane of q, apply complex asin, reconstruct. */
        Quaternion *q = as_quat(v);
        double b = q->b, c = q->c, d = q->d;
        double vnorm = sqrt(b*b + c*c + d*d);
        val_t z = num_make_complex(num_make_float(q->a), num_make_float(vnorm));
        val_t w = num_asin(z);
        return quat_assemble(q, num_to_double(as_cpx(w)->real),
                                num_to_double(as_cpx(w)->imag));
    }
    if (vis_complex(v)) {
        /* asin(z) = -i · ln(iz + √(1−z²)) */
        val_t i  = num_make_complex(vfix(0), vfix(1));
        val_t ni = num_make_complex(vfix(0), vfix(-1));
        return num_mul(ni, num_log(num_add(num_mul(i, v),
                        num_sqrt(num_sub(vfix(1), num_mul(v, v))))));
    }
    return num_make_float(asin(num_to_double(v)));
}
val_t num_acos(val_t v) {
    if (vis_symbolic(v)) return sx_acos(v);
#ifdef BUILD_MPFR
    if (vis_mpfr(v)) return mpfr_num_acos(v);
#endif
    if (vis_quat(v)) {
        Quaternion *q = as_quat(v);
        double b = q->b, c = q->c, d = q->d;
        double vnorm = sqrt(b*b + c*c + d*d);
        val_t z = num_make_complex(num_make_float(q->a), num_make_float(vnorm));
        val_t w = num_acos(z);
        return quat_assemble(q, num_to_double(as_cpx(w)->real),
                                num_to_double(as_cpx(w)->imag));
    }
    if (vis_complex(v)) {
        /* acos(z) = -i · ln(z + i·√(1−z²)) */
        val_t i  = num_make_complex(vfix(0), vfix(1));
        val_t ni = num_make_complex(vfix(0), vfix(-1));
        return num_mul(ni, num_log(num_add(v,
                        num_mul(i, num_sqrt(num_sub(vfix(1), num_mul(v, v)))))));
    }
    return num_make_float(acos(num_to_double(v)));
}
val_t num_atan(val_t v) {
    if (vis_symbolic(v)) return sx_atan(v);
#ifdef BUILD_MPFR
    if (vis_mpfr(v)) return mpfr_num_atan(v);
#endif
    if (vis_quat(v)) {
        Quaternion *q = as_quat(v);
        double b = q->b, c = q->c, d = q->d;
        double vnorm = sqrt(b*b + c*c + d*d);
        val_t z = num_make_complex(num_make_float(q->a), num_make_float(vnorm));
        val_t w = num_atan(z);
        return quat_assemble(q, num_to_double(as_cpx(w)->real),
                                num_to_double(as_cpx(w)->imag));
    }
    if (vis_complex(v)) {
        /* atan(z) = (i/2) · ln((1−iz)/(1+iz)) */
        val_t i    = num_make_complex(vfix(0), vfix(1));
        val_t iz   = num_mul(i, v);
        val_t half = num_make_complex(vfix(0), num_make_float(0.5));
        return num_mul(half, num_log(num_div(num_sub(vfix(1), iz),
                                            num_add(vfix(1), iz))));
    }
    return num_make_float(atan(num_to_double(v)));
}

/* Complex-aware transcendentals */
val_t num_exp(val_t v) {
    if (vis_symbolic(v)) return sx_exp(v);
#ifdef BUILD_MPFR
    if (vis_mpfr(v)) return mpfr_num_exp(v);
#endif
    if (vis_quat(v)) {
        /* exp(q) = e^a · (cos‖v‖ + v̂·sin‖v‖) */
        Quaternion *q = as_quat(v);
        double a = q->a, b = q->b, c = q->c, d = q->d;
        double vnorm = sqrt(b*b + c*c + d*d);
        double ea = exp(a);
        return quat_assemble(q, ea * cos(vnorm), ea * sin(vnorm));
    }
    if (vis_complex(v)) {
        double a = num_to_double(as_cpx(v)->real), b = num_to_double(as_cpx(v)->imag);
        double ea = exp(a);
        return num_make_complex(num_make_float(ea * cos(b)), num_make_float(ea * sin(b)));
    }
    return num_make_float(exp(num_to_double(v)));
}
val_t num_log(val_t v) {
    if (vis_symbolic(v)) return sx_log(v);
#ifdef BUILD_MPFR
    if (vis_mpfr(v)) {
        if (mpfr_sgn(as_mpfr(v)->x) < 0) {
            val_t neg = mpfr_num_neg(v);
            return num_make_complex(mpfr_num_log(neg), num_make_float(M_PI));
        }
        return mpfr_num_log(v);
    }
#endif
    if (vis_quat(v)) {
        /* log(q) = ln‖q‖ + v̂·arccos(a/‖q‖)  (principal branch) */
        Quaternion *q = as_quat(v);
        double a = q->a, b = q->b, c = q->c, d = q->d;
        double qnorm = sqrt(a*a + b*b + c*c + d*d);
        return quat_assemble(q, log(qnorm), acos(a / qnorm));
    }
    if (vis_complex(v)) {
        double a = num_to_double(as_cpx(v)->real), b = num_to_double(as_cpx(v)->imag);
        return num_make_complex(num_make_float(log(sqrt(a*a + b*b))), num_make_float(atan2(b, a)));
    }
    double x = num_to_double(v);
    if (x < 0) /* log of negative real → complex result */
        return num_make_complex(num_make_float(log(-x)), num_make_float(M_PI));
    return num_make_float(log(x));
}
val_t num_sin(val_t v) {
    if (vis_symbolic(v)) return sx_sin(v);
#ifdef BUILD_MPFR
    if (vis_mpfr(v)) return mpfr_num_sin(v);
#endif
    if (vis_quat(v)) {
        /* sin(q) = sin(a)·cosh‖v‖ + v̂·cos(a)·sinh‖v‖ */
        Quaternion *q = as_quat(v);
        double a = q->a, b = q->b, c = q->c, d = q->d;
        double vnorm = sqrt(b*b + c*c + d*d);
        return quat_assemble(q, sin(a) * cosh(vnorm), cos(a) * sinh(vnorm));
    }
    if (vis_complex(v)) {
        double a = num_to_double(as_cpx(v)->real), b = num_to_double(as_cpx(v)->imag);
        return num_make_complex(num_make_float(sin(a)*cosh(b)), num_make_float(cos(a)*sinh(b)));
    }
    return num_make_float(sin(num_to_double(v)));
}
val_t num_cos(val_t v) {
    if (vis_symbolic(v)) return sx_cos(v);
#ifdef BUILD_MPFR
    if (vis_mpfr(v)) return mpfr_num_cos(v);
#endif
    if (vis_quat(v)) {
        /* cos(q) = cos(a)·cosh‖v‖ − v̂·sin(a)·sinh‖v‖ */
        Quaternion *q = as_quat(v);
        double a = q->a, b = q->b, c = q->c, d = q->d;
        double vnorm = sqrt(b*b + c*c + d*d);
        return quat_assemble(q, cos(a) * cosh(vnorm), -sin(a) * sinh(vnorm));
    }
    if (vis_complex(v)) {
        double a = num_to_double(as_cpx(v)->real), b = num_to_double(as_cpx(v)->imag);
        return num_make_complex(num_make_float(cos(a)*cosh(b)), num_make_float(-sin(a)*sinh(b) + 0.0));
    }
    return num_make_float(cos(num_to_double(v)));
}
val_t num_tan(val_t v) {
    if (vis_symbolic(v)) return sx_tan(v);
#ifdef BUILD_MPFR
    if (vis_mpfr(v)) return mpfr_num_tan(v);
#endif
    /* sin(q) and cos(q) share the same imaginary axis v̂, so they commute */
    if (vis_quat(v) || vis_complex(v)) return num_div(num_sin(v), num_cos(v));
    return num_make_float(tan(num_to_double(v)));
}
val_t num_atan2(val_t y, val_t x) {
#ifdef BUILD_MPFR
    if (vis_mpfr(y) || vis_mpfr(x)) return mpfr_num_atan2(y, x);
#endif
    return num_make_float(atan2(num_to_double(y), num_to_double(x)));
}

val_t num_sinh(val_t v) {
    if (vis_symbolic(v)) return sx_sinh(v);
#ifdef BUILD_MPFR
    if (vis_mpfr(v)) return mpfr_num_sinh(v);
#endif
    if (vis_quat(v)) {
        /* sinh(q) = sinh(a)·cos‖v‖ + v̂·cosh(a)·sin‖v‖ */
        Quaternion *q = as_quat(v);
        double a = q->a, b = q->b, c = q->c, d = q->d;
        double vnorm = sqrt(b*b + c*c + d*d);
        return quat_assemble(q, sinh(a) * cos(vnorm), cosh(a) * sin(vnorm));
    }
    if (vis_complex(v)) {
        double a = num_to_double(as_cpx(v)->real), b = num_to_double(as_cpx(v)->imag);
        return num_make_complex(num_make_float(sinh(a)*cos(b)), num_make_float(cosh(a)*sin(b)));
    }
    return num_make_float(sinh(num_to_double(v)));
}
val_t num_cosh(val_t v) {
    if (vis_symbolic(v)) return sx_cosh(v);
#ifdef BUILD_MPFR
    if (vis_mpfr(v)) return mpfr_num_cosh(v);
#endif
    if (vis_quat(v)) {
        /* cosh(q) = cosh(a)·cos‖v‖ + v̂·sinh(a)·sin‖v‖ */
        Quaternion *q = as_quat(v);
        double a = q->a, b = q->b, c = q->c, d = q->d;
        double vnorm = sqrt(b*b + c*c + d*d);
        return quat_assemble(q, cosh(a) * cos(vnorm), sinh(a) * sin(vnorm));
    }
    if (vis_complex(v)) {
        double a = num_to_double(as_cpx(v)->real), b = num_to_double(as_cpx(v)->imag);
        return num_make_complex(num_make_float(cosh(a)*cos(b)), num_make_float(sinh(a)*sin(b)));
    }
    return num_make_float(cosh(num_to_double(v)));
}
val_t num_tanh(val_t v) {
    if (vis_symbolic(v)) return sx_tanh(v);
#ifdef BUILD_MPFR
    if (vis_mpfr(v)) return mpfr_num_tanh(v);
#endif
    /* sinh(q) and cosh(q) share the same imaginary axis v̂, so they commute */
    if (vis_quat(v)) return num_div(num_sinh(v), num_cosh(v));
    if (vis_complex(v)) {
        double a = num_to_double(as_cpx(v)->real), b = num_to_double(as_cpx(v)->imag);
        double denom = cosh(2*a) + cos(2*b);
        return num_make_complex(num_make_float(sinh(2*a)/denom), num_make_float(sin(2*b)/denom));
    }
    return num_make_float(tanh(num_to_double(v)));
}
val_t num_asinh(val_t v) {
    if (vis_symbolic(v)) return sx_asinh(v);
#ifdef BUILD_MPFR
    if (vis_mpfr(v)) return mpfr_num_asinh(v);
#endif
    if (vis_quat(v) || vis_complex(v))
        /* asinh(q) = ln(q + √(q²+1)) — works for quaternions via num_log/num_sqrt */
        return num_log(num_add(v, num_sqrt(num_add(num_mul(v, v), vfix(1)))));
    return num_make_float(asinh(num_to_double(v)));
}
val_t num_acosh(val_t v) {
    if (vis_symbolic(v)) return sx_acosh(v);
#ifdef BUILD_MPFR
    if (vis_mpfr(v)) return mpfr_num_acosh(v);
#endif
    if (vis_quat(v) || vis_complex(v))
        /* acosh(q) = ln(q + √(q²−1)) */
        return num_log(num_add(v, num_sqrt(num_sub(num_mul(v, v), vfix(1)))));
    return num_make_float(acosh(num_to_double(v)));
}
val_t num_atanh(val_t v) {
    if (vis_symbolic(v)) return sx_atanh(v);
#ifdef BUILD_MPFR
    if (vis_mpfr(v)) return mpfr_num_atanh(v);
#endif
    if (vis_quat(v) || vis_complex(v))
        /* atanh(q) = (1/2) · ln((1+q)/(1−q)) */
        return num_mul(num_make_float(0.5),
                       num_log(num_div(num_add(vfix(1), v),
                                      num_sub(vfix(1), v))));
    return num_make_float(atanh(num_to_double(v)));
}
val_t num_cot(val_t v) {
    if (vis_symbolic(v)) return sx_cot(v);
    return num_div(num_cos(v), num_sin(v));
}
val_t num_sec(val_t v) {
    if (vis_symbolic(v)) return sx_sec(v);
    return num_div(vfix(1), num_cos(v));
}
val_t num_csc(val_t v) {
    if (vis_symbolic(v)) return sx_csc(v);
    return num_div(vfix(1), num_sin(v));
}

/* ---- Bitwise ---- */
val_t num_bitand(val_t a, val_t b) {
    if (vis_fixnum(a) && vis_fixnum(b)) return vfix(vunfix(a) & vunfix(b));
    mpz_t za,zb,zr; mpz_init(za); mpz_init(zb); mpz_init(zr);
    to_mpz(za,a); to_mpz(zb,b); mpz_and(zr,za,zb);
    val_t r=make_big_from_mpz(zr); mpz_clear(za); mpz_clear(zb); mpz_clear(zr); return r;
}
val_t num_bitor(val_t a, val_t b) {
    if (vis_fixnum(a) && vis_fixnum(b)) return vfix(vunfix(a) | vunfix(b));
    mpz_t za,zb,zr; mpz_init(za); mpz_init(zb); mpz_init(zr);
    to_mpz(za,a); to_mpz(zb,b); mpz_ior(zr,za,zb);
    val_t r=make_big_from_mpz(zr); mpz_clear(za); mpz_clear(zb); mpz_clear(zr); return r;
}
val_t num_bitxor(val_t a, val_t b) {
    if (vis_fixnum(a) && vis_fixnum(b)) return vfix(vunfix(a) ^ vunfix(b));
    mpz_t za,zb,zr; mpz_init(za); mpz_init(zb); mpz_init(zr);
    to_mpz(za,a); to_mpz(zb,b); mpz_xor(zr,za,zb);
    val_t r=make_big_from_mpz(zr); mpz_clear(za); mpz_clear(zb); mpz_clear(zr); return r;
}
val_t num_bitnot(val_t a) {
    if (vis_fixnum(a)) return vfix(~vunfix(a));
    mpz_t z; mpz_init(z); to_mpz(z,a); mpz_com(z,z);
    val_t r=make_big_from_mpz(z); mpz_clear(z); return r;
}
val_t num_shl(val_t a, int n) {
    if (n < 0) return num_shr(a, -n);
    if (vis_fixnum(a) && n < 30) {
        intptr_t r = vunfix(a) << n;
        if (in_fixnum_range(r)) return vfix(r);
    }
    mpz_t z; mpz_init(z); to_mpz(z,a); mpz_mul_2exp(z,z,(mp_bitcnt_t)n);
    val_t r=make_big_from_mpz(z); mpz_clear(z); return r;
}
val_t num_shr(val_t a, int n) {
    if (n < 0) return num_shl(a, -n);
    if (vis_fixnum(a)) return vfix(vunfix(a) >> n);
    mpz_t z; mpz_init(z); to_mpz(z,a); mpz_tdiv_q_2exp(z,z,(mp_bitcnt_t)n);
    val_t r=make_big_from_mpz(z); mpz_clear(z); return r;
}
val_t num_bitlen(val_t a) {
    if (vis_fixnum(a)) {
        intptr_t n = vunfix(a); if (n < 0) n = ~n;
        int bits = 0; while (n) { bits++; n >>= 1; }
        return vfix(bits);
    }
    mpz_t z; mpz_init(z); to_mpz(z,a);
    size_t len = mpz_sizeinbase(z, 2);
    mpz_clear(z); return vfix((intptr_t)len);
}

/* ---- Complex ---- */
val_t num_real_part(val_t v) {
    if (vis_symbolic(v)) return sx_real(v);
    return vis_complex(v) ? as_cpx(v)->real : v;
}
val_t num_imag_part(val_t v) {
    if (vis_symbolic(v)) return sx_imag(v);
    return vis_complex(v) ? as_cpx(v)->imag : vfix(0);
}
val_t num_magnitude(val_t v) {
    if (!vis_complex(v)) return num_abs(v);
    double r = num_to_double(as_cpx(v)->real), i = num_to_double(as_cpx(v)->imag);
    return num_make_float(sqrt(r*r + i*i));
}
val_t num_angle(val_t v) {
    if (!vis_complex(v)) return num_make_float(num_is_negative(v) ? M_PI : 0.0);
    return num_make_float(atan2(num_to_double(as_cpx(v)->imag), num_to_double(as_cpx(v)->real)));
}
val_t num_conjugate(val_t v) {
    if (vis_symbolic(v)) return sx_conj(v);
    if (vis_quat(v)) return num_quat_conjugate(v);
    if (!vis_complex(v)) return v;
    return num_make_complex(as_cpx(v)->real, num_neg(as_cpx(v)->imag));
}

/* ---- Quaternion ---- */
val_t num_quat_a(val_t v) { return num_make_float(as_quat(v)->a); }
val_t num_quat_b(val_t v) { return num_make_float(as_quat(v)->b); }
val_t num_quat_c(val_t v) { return num_make_float(as_quat(v)->c); }
val_t num_quat_d(val_t v) { return num_make_float(as_quat(v)->d); }

val_t num_quat_norm(val_t v) {
    Quaternion *q = as_quat(v);
    return num_make_float(sqrt(q->a*q->a + q->b*q->b + q->c*q->c + q->d*q->d));
}
val_t num_quat_normalize(val_t v) {
    Quaternion *q = as_quat(v);
    double n = sqrt(q->a*q->a + q->b*q->b + q->c*q->c + q->d*q->d);
    return num_make_quat(q->a/n, q->b/n, q->c/n, q->d/n);
}
val_t num_quat_conjugate(val_t v) {
    Quaternion *q = as_quat(v);
    return num_make_quat(q->a, -q->b, -q->c, -q->d);
}
val_t num_quat_inverse(val_t v) {
    Quaternion *q = as_quat(v);
    double n2 = q->a*q->a + q->b*q->b + q->c*q->c + q->d*q->d;
    return num_make_quat(q->a/n2, -q->b/n2, -q->c/n2, -q->d/n2);
}

/* Rotate 3-vector v3 (as quaternion 0+xi+yj+zk) by unit quaternion q */
val_t num_quat_rotate(val_t qv, val_t v3) {
    /* p' = q * p * q^-1 */
    val_t p   = v3;
    val_t qinv = num_quat_inverse(qv);
    return num_mul(num_mul(qv, p), qinv);
}

/* ---- Octonion ---- */
val_t num_oct_ref(val_t v, int i) { return num_make_float(as_oct(v)->e[i]); }
val_t num_oct_norm(val_t v) {
    double s = 0; for(int i=0;i<8;i++) s += as_oct(v)->e[i]*as_oct(v)->e[i];
    return num_make_float(sqrt(s));
}
val_t num_oct_conjugate(val_t v) {
    double e[8]; memcpy(e, as_oct(v)->e, 8*sizeof(double));
    for(int i=1;i<8;i++) e[i] = -e[i];
    return num_make_oct(e);
}
val_t num_oct_inverse(val_t v) {
    double e[8]; memcpy(e, as_oct(v)->e, 8*sizeof(double));
    double n2 = 0; for(int i=0;i<8;i++) n2 += e[i]*e[i];
    e[0] /= n2; for(int i=1;i<8;i++) e[i] = -e[i]/n2;
    return num_make_oct(e);
}

/* ---- Number <-> string ---- */

/* True iff every digit character in `s` after the first `keep` significant
 * digits is '0' -- i.e. reformatting at a higher precision than `keep`
 * added nothing but trailing zeros, not genuine extra digits. Skips the
 * sign, decimal point, and anything from an exponent marker onward. */
static bool num_only_trailing_zeros_after(const char *s, int keep) {
    int seen = 0;
    for (const char *p = s; *p; p++) {
        if (*p == 'e' || *p == 'E') break;
        if (*p < '0' || *p > '9') continue;
        if (++seen > keep && *p != '0') return false;
    }
    return true;
}

int num_flonum_to_shortest_cstr(double d, char *buf, size_t bufsize) {
    /* NaN never round-trips via == (NaN != NaN in IEEE), so the loop below
     * would spin all the way to precision 17 for no benefit -- short-
     * circuit both non-finite cases directly.
     *
     * NaN is spelled as a literal "nan" here, NOT delegated to %g: a
     * NaN's sign bit is implementation-defined (IEEE 754 doesn't specify
     * what payload/sign a computation like 0.0/0.0 produces), and glibc's
     * printf faithfully reflects that bit in its output ("-nan" vs
     * "nan"), while other libcs (macOS's, for the exact same computed
     * value) don't set it -- confirmed via CI: this codebase's own
     * NaN round-trip test passed on macOS and failed on Linux for
     * exactly this reason, for a value neither platform's C code
     * chooses the sign of. curry's own display convention (unlike inf,
     * where the sign IS meaningful and well-defined) doesn't distinguish
     * signed NaNs at all, so normalize to the same "nan" spelling
     * everywhere regardless of the underlying bit. +inf/-inf's sign is
     * unambiguous IEEE 754, not implementation-defined, so %g is fine
     * there. */
    if (d != d) {
        return snprintf(buf, bufsize, "nan");
    }
    if (d == 1.0/0.0 || d == -1.0/0.0)
        return snprintf(buf, bufsize, "%g", d);
    int p_min = 17;
    for (int prec = 1; prec <= 17; prec++) {
        snprintf(buf, bufsize, "%.*g", prec, d);
        if (strtod(buf, NULL) == d) { p_min = prec; break; }
    }
    /* %g switches to scientific notation whenever the value's decimal
     * exponent E satisfies E >= precision. For a "round" value that needs
     * few significant digits to round-trip (e.g. 100.0, p_min == 1), that
     * rule triggers scientific notation ("1e+02") purely because the
     * REQUIRED digit count is small -- not because the value's own
     * magnitude warrants it. Try bumping the precision used for the FINAL
     * formatting (not the round-trip search above, which must start low
     * to find the true minimal digit count) to also cover the integer
     * part's own digit count, so %g's notation choice reflects the
     * number's actual magnitude rather than the incidental digit count
     * needed to represent it exactly.
     *
     * This is only safe when the extra digits %g reveals at that higher
     * precision are genuine trailing zeros (true for a value that IS
     * decimal-clean in binary, like 100.0) rather than real bits of the
     * value's binary representation bleeding through (true for most
     * large-magnitude values that aren't exactly representable, e.g.
     * 1e300 -- (%.17g 1e300) is "1.0000000000000001e+300", not clean
     * zeros, because the double closest to 1e300 isn't exactly 10^300).
     * Bumping precision can never break the round-trip already
     * established above (more digits is always at least as accurate),
     * but it CAN accidentally turn a clean "1e+300" into 300 digits of
     * binary-conversion noise if applied blindly -- verify before using
     * it, and fall back to p_min's own natural formatting otherwise. */
    int exponent = (d == 0.0) ? 0 : (int)floor(log10(fabs(d)));
    int fmt_prec = p_min;
    if (exponent + 1 > fmt_prec) fmt_prec = exponent + 1;
    if (fmt_prec > 17) fmt_prec = 17;
    if (fmt_prec > p_min) {
        int n = snprintf(buf, bufsize, "%.*g", fmt_prec, d);
        if (num_only_trailing_zeros_after(buf, p_min)) return n;
    }
    return snprintf(buf, bufsize, "%.*g", p_min, d);
}

/* Copy len+1 bytes (including the NUL) of a C string into a freshly
 * allocated Curry String -- the "alloc a String, fill its fixed header
 * fields, memcpy the bytes in" sequence num_to_string's branches below
 * all repeated verbatim. Does not take ownership of `s`. */
static val_t string_from_cstr_len(const char *s, uint32_t len) {
    String *str = (String *)gc_alloc_atomic(sizeof(String) + len + 1);
    str->hdr.type = T_STRING; str->hdr.flags = 0;
    str->len = len; str->orig_cap = len; str->ext = NULL;
    memcpy(str->data, s, len + 1);
    return vptr(str);
}

/* Same, for a malloc'd C string this function takes ownership of and
 * frees -- covers the mpz_get_str/mpq_get_str result in each of
 * num_to_string's GMP-backed branches. */
static val_t string_from_owned_cstr(char *s) {
    val_t result = string_from_cstr_len(s, (uint32_t)strlen(s));
    free(s);
    return result;
}

val_t num_to_string(val_t v, int radix) {
    char buf[128];
    if (vis_fixnum(v)) {
        /* GMP handles every radix uniformly, including binary.  Earlier code
         * used printf's %ld for radix==2 which silently produced decimal. */
        if (radix == 10) {
            snprintf(buf, sizeof(buf), "%ld", (long)vunfix(v));
        } else {
            mpz_t z; mpz_init_set_si(z, (long)vunfix(v));
            char *s = mpz_get_str(NULL, radix, z);
            val_t result = string_from_owned_cstr(s);
            mpz_clear(z);
            return result;
        }
    } else if (vis_bignum(v)) {
        return string_from_owned_cstr(mpz_get_str(NULL, radix, as_big(v)->z));
    } else if (vis_flonum(v)) {
        num_flonum_to_shortest_cstr(vfloat(v), buf, sizeof(buf));
#ifdef BUILD_MPFR
    } else if (vis_mpfr(v)) {
        return mpfr_to_string(v, 0, radix);
#endif
    } else if (vis_rational(v)) {
        /* A dedicated branch, not the scm_write() fallback below: scm_write()
         * routes a rational through write_number_notation() (src/port.c),
         * which -- when current-number-notation is 'cuneiform/'neugebauer --
         * calls back into sex_to_cuneiform()/sex_to_neugebauer(). Either of
         * those falls back to num_to_string(v, 10) for a rational whose
         * integer part overflows the 64-base-60-digit cap
         * (mpz_to_base60_digits() returns -1), which would re-enter this
         * exact function and recurse forever (confirmed: segfault). Formatting
         * directly with GMP here, like the bignum branch above, sidesteps
         * write_number_notation() entirely. */
        return string_from_owned_cstr(mpq_get_str(NULL, radix, as_rat(v)->q));
    } else {
        /* Complex/quaternion/octonion/multivector/surreal/symbolic: radix is
         * meaningless for these, same as the flonum case above which already
         * ignores it. Delegate to the writer scm_write() already uses for
         * `display`/`write` rather than maintaining a second, partial printer
         * here -- safe from the rational's recursion risk above because none
         * of these route through write_number_notation() (see port.c's
         * scm_write: only flonum/bignum/rational do). */
        val_t p = port_open_output_string();
        scm_write(v, p);
        return port_get_output_string(p);
    }
    return string_from_cstr_len(buf, (uint32_t)strlen(buf));
}

val_t num_normalize(val_t v) {
    if (vis_bignum(v) && mpz_fits_slong_p(as_big(v)->z)) {
        long n = mpz_get_si(as_big(v)->z);
        if (in_fixnum_range(n)) return vfix(n);
    }
    return v;
}

/* ===================================================================
 * Sexagesimal (Babylonian/Neugebauer) number support — v1.2.5
 *
 * Two representations:
 *  1. Neugebauer notation: digits,separated,by,commas[;fractional,digits]
 *  2. Cuneiform Unicode:   𒁹 (ASH=1), 𒌋 (U=10), 𒑊 (SHAR2=0)
 *     digit groups separated by spaces; ASH and U additive within group.
 * =================================================================== */

/* Cuneiform Unicode codepoints */
#define CP_ASH   0x12079u   /* 𒁹 U+12079 CUNEIFORM SIGN ASH = 1 */
#define CP_U     0x1230Bu   /* 𒌋 U+1230B CUNEIFORM SIGN U    = 10 */
#define CP_SHAR2 0x1244Au   /* 𒑊 U+1244A CUNEIFORM NUMERIC SIGN TWO ASH TENU = 0
                             * (Seleucid zero placeholder; the glyph commonly
                             * labelled "𒑊" in Assyriology tools maps to U+1244A,
                             * not U+12469 SHAR2 which denotes 3600 in astronomy) */
#define CP_MIDDLE_DOT 0x00B7u /* · U+00B7 — the radix-point separator sex_to_cuneiform()
                               * renders between integer and fractional digit groups */

/* Extended-tower markers, chosen from real cuneiform syllabic/determinative
 * signs, reused here purely for their phonetic/symbolic value -- the same
 * way Babylonian scribes reused a sign's sound rather than its logographic
 * meaning. None collides with CP_ASH/CP_U/CP_SHAR2 (the digit glyphs).
 *
 * CP_SIGN_I and CP_DINGIR, however, are NOT unused elsewhere in Curry: 𒄿 is
 * also the sole cuneiform form of the `do` special form
 * (`AKK_SF("do", "alākum", "𒄿")` in akkadian_names.h) and appears within
 * several multi-glyph Akkadian words besides; 𒀭 (DINGIR) appears even more
 * often, being the divine determinative several coined procedure names are
 * built from (e.g. `surreal-real-part`'s cuneiform form is "𒀭𒄿" --
 * literally DINGIR + this same I sign). This is safe in practice today for
 * two independent reasons, not because the glyphs are reserved-word-free:
 * (1) `sex_parse_cuneiform`/`sex_parse_cuneiform_extended` never consult
 * `_akk_cuneiform_table` at all -- that whole-token reserved-keyword check
 * lives only in reader.c, upstream of `sex_parse_cuneiform` being called;
 * and (2) as of this writing reader.c's own tokenizer doesn't yet assemble
 * this extended notation into one token in the first place (only
 * `string->number` reaches this parser -- see module-sexagesimal.md), so no
 * currently-reachable bare source literal collides with `do` or with any
 * DINGIR-prefixed procedure alias. If reader.c is ever taught to tokenize
 * extended cuneiform literals directly, this reuse would need re-auditing
 * against the full akkadian_names.h table at that point -- it is not
 * checked or enforced anywhere today. Only CP_SIGN_E (𒂊) is actually unused
 * in akkadian_names.h. */
#define CP_SIGN_I 0x1213Fu  /* 𒄿 U+1213F CUNEIFORM SIGN I — marks the complex imaginary unit */
#define CP_SIGN_E 0x1208Au  /* 𒂊 U+1208A CUNEIFORM SIGN E — marks a basis blade e_n, followed
                             * by a cuneiform digit for n (1..8): quaternion/octonion/multivector */
#define CP_DINGIR 0x1202Du  /* 𒀭 U+1202D CUNEIFORM SIGN AN — the divine determinative real
                             * scribes prefixed to god-names, reused here to mark a surreal
                             * Hahn-series term's exponent (an "order of ω", i.e. an
                             * infinite/infinitesimal quantity) */

/* ---- UTF-8 helpers ---- */

/* Decode one codepoint from *s; advance *s past the bytes. Returns -1 at end.
 * Not static: symbolic_print.c's sx_write_cuneiform() reuses this (rather
 * than a second, laxer hand-rolled decoder) to find a symbol's first
 * codepoint when picking out a cuneiform-alias operator glyph. */
int sex_decode_cp(const char **s) {
    const unsigned char *p = (const unsigned char *)*s;
    if (!*p) return -1;
    uint32_t cp; int n;
    if      (*p < 0x80) { cp = *p;        n = 1; }
    else if (*p < 0xE0) { cp = *p & 0x1F; n = 2; }
    else if (*p < 0xF0) { cp = *p & 0x0F; n = 3; }
    else                { cp = *p & 0x07; n = 4; }
    for (int i = 1; i < n; i++) {
        if ((p[i] & 0xC0) != 0x80) return -1;
        cp = (cp << 6) | (p[i] & 0x3F);
    }
    *s += n;
    return (int)cp;
}

/* Encode one codepoint to UTF-8 bytes in buf (must have ≥5 bytes). Returns byte count. */
static int sex_encode_cp(uint32_t cp, char *buf) {
    if (cp < 0x80)    { buf[0]=(char)cp; return 1; }
    if (cp < 0x800)   { buf[0]=(char)(0xC0|(cp>>6));   buf[1]=(char)(0x80|(cp&0x3F)); return 2; }
    if (cp < 0x10000) { buf[0]=(char)(0xE0|(cp>>12));  buf[1]=(char)(0x80|((cp>>6)&0x3F)); buf[2]=(char)(0x80|(cp&0x3F)); return 3; }
    buf[0]=(char)(0xF0|(cp>>18)); buf[1]=(char)(0x80|((cp>>12)&0x3F));
    buf[2]=(char)(0x80|((cp>>6)&0x3F)); buf[3]=(char)(0x80|(cp&0x3F)); return 4;
}

/* ---- Dynamic string builder for sexagesimal output ---- */
typedef struct { char *buf; size_t len; size_t cap; } SexBuf;

static void sexbuf_init(SexBuf *b) {
    b->cap = 256; b->len = 0;
    b->buf = (char *)malloc(b->cap);
    b->buf[0] = '\0';
}
static void sexbuf_ensure(SexBuf *b, size_t extra) {
    while (b->len + extra + 1 >= b->cap) { b->cap *= 2; b->buf = (char *)realloc(b->buf, b->cap); }
}
static void sexbuf_push(SexBuf *b, char c) {
    sexbuf_ensure(b, 1); b->buf[b->len++] = c; b->buf[b->len] = '\0';
}
static void sexbuf_push_str(SexBuf *b, const char *s, size_t n) {
    sexbuf_ensure(b, n); memcpy(b->buf + b->len, s, n); b->len += n; b->buf[b->len] = '\0';
}
static void sexbuf_push_cp(SexBuf *b, uint32_t cp) {
    char tmp[5]; int n = sex_encode_cp(cp, tmp); sexbuf_push_str(b, tmp, (size_t)n);
}
static void sexbuf_push_int(SexBuf *b, int d) {
    char tmp[8]; int n = snprintf(tmp, sizeof(tmp), "%d", d);
    sexbuf_push_str(b, tmp, (size_t)n);
}
static val_t sexbuf_to_val(SexBuf *b) {
    uint32_t len = (uint32_t)b->len;
    String *str = (String *)gc_alloc_atomic(sizeof(String) + len + 1);
    str->hdr.type = T_STRING; str->hdr.flags = 0;
    str->len = len; str->hash = 0; str->orig_cap = len; str->ext = NULL;
    memcpy(str->data, b->buf, len + 1);
    free(b->buf); b->buf = NULL;
    return vptr(str);
}

/* ---- Base-60 digit extraction (MSB-first, using GMP) ---- */

/* Fill digits[] with base-60 representation of non-negative mpz n, MSB first.
 * Returns count, or -1 if n requires more than 64 base-60 digits (≥ 60^64).
 * digits[] must have room for at least 64 entries. */
static int mpz_to_base60_digits(mpz_t n, int *digits) {
    assert(mpz_sgn(n) >= 0);          /* caller must negate before calling */
    if (mpz_sgn(n) == 0) { digits[0] = 0; return 1; }
    int buf[64], cnt = 0;
    mpz_t tmp; mpz_init_set(tmp, n);
    while (mpz_sgn(tmp) > 0) {
        if (cnt >= 64) { mpz_clear(tmp); return -1; } /* overflow */
        buf[cnt++] = (int)mpz_tdiv_q_ui(tmp, tmp, 60);
    }
    mpz_clear(tmp);
    /* Reverse to get MSB first */
    for (int i = 0; i < cnt / 2; i++) { int t = buf[i]; buf[i] = buf[cnt-1-i]; buf[cnt-1-i] = t; }
    memcpy(digits, buf, (size_t)cnt * sizeof(int));
    return cnt;
}

/* ---- Rendering helpers ---- */

static void sex_render_neugebauer_digits(SexBuf *b, int *digs, int n) {
    for (int i = 0; i < n; i++) {
        if (i) sexbuf_push(b, ',');
        sexbuf_push_int(b, digs[i]);
    }
}

static void sex_render_cuneiform_digit(SexBuf *b, int digit) {
    if (digit == 0) { sexbuf_push_cp(b, CP_SHAR2); return; }
    for (int i = 0; i < digit / 10; i++) sexbuf_push_cp(b, CP_U);
    for (int i = 0; i < digit % 10; i++) sexbuf_push_cp(b, CP_ASH);
}

static void sex_render_cuneiform_digits(SexBuf *b, int *digs, int n) {
    for (int i = 0; i < n; i++) {
        if (i) sexbuf_push(b, ' ');
        sex_render_cuneiform_digit(b, digs[i]);
    }
}

/* ---- Shared digit-extraction for a Scheme number ---- */

/* digits[] must have room for 64 integer + 64 fractional entries each
 * (matches the buf[64] capacity in mpz_to_base60_digits). */
typedef struct {
    bool     neg;
    int      int_digs[64];
    int      nint;
    int      frac_digs[64];
    int      nfrac;
    bool     valid;
} SexDigs;

static void sex_get_digits(val_t v, int max_frac, SexDigs *out) {
    out->neg = false; out->nint = 0; out->nfrac = 0; out->valid = true;
    if (max_frac < 0) max_frac = 20;   /* auto = up to 20 fractional places */

    if (vis_fixnum(v) || vis_bignum(v)) {
        mpz_t tmp;
        if (vis_fixnum(v)) mpz_init_set_si(tmp, vunfix(v));
        else               mpz_init_set(tmp, as_big(v)->z);
        out->neg = mpz_sgn(tmp) < 0;
        if (out->neg) mpz_neg(tmp, tmp);
        out->nint = mpz_to_base60_digits(tmp, out->int_digs);
        mpz_clear(tmp);
        if (out->nint < 0) { out->valid = false; return; }
    } else if (vis_rational(v)) {
        mpq_t q; mpq_init(q); mpq_set(q, as_rat(v)->q);
        out->neg = mpq_sgn(q) < 0;
        if (out->neg) mpq_neg(q, q);

        mpz_t ip, num, den, rem;
        mpz_init(ip); mpz_init(num); mpz_init(den); mpz_init(rem);
        mpz_set(num, mpq_numref(q)); mpz_set(den, mpq_denref(q));
        mpz_fdiv_q(ip, num, den);
        /* rem = num - ip * den */
        mpz_mul(rem, ip, den); mpz_sub(rem, num, rem);

        out->nint = mpz_to_base60_digits(ip, out->int_digs);
        if (out->nint < 0) {
            mpz_clear(ip); mpz_clear(num); mpz_clear(den); mpz_clear(rem);
            mpq_clear(q); out->valid = false; return;
        }

        for (int i = 0; i < max_frac && i < 32; i++) {
            mpz_mul_ui(rem, rem, 60);
            mpz_t fd; mpz_init(fd);
            mpz_fdiv_q(fd, rem, den);
            out->frac_digs[out->nfrac++] = (int)mpz_get_ui(fd);
            mpz_mul(fd, fd, den); mpz_sub(rem, rem, fd);
            mpz_clear(fd);
            if (mpz_sgn(rem) == 0) break;
        }

        mpz_clear(ip); mpz_clear(num); mpz_clear(den); mpz_clear(rem);
        mpq_clear(q);
    } else if (vis_flonum(v)) {
        double d = vfloat(v);
        if (d != d || d == 1.0/0.0 || d == -1.0/0.0) { out->valid = false; return; }
        out->neg = d < 0; if (out->neg) d = -d;
        double int_d, frac_d = modf(d, &int_d);
        if (max_frac < 0 || max_frac > 4) max_frac = 4;  /* flonum default = 4 places */

        /* mpz_init_set_d, not (long)int_d: int_d can exceed LONG_MAX/LONG_MIN
         * for a large finite flonum (e.g. 1e300), and casting an
         * out-of-range double to long is undefined behavior. mpz_set_d
         * handles any finite double magnitude directly. */
        mpz_t tmp; mpz_init_set_d(tmp, int_d);
        out->nint = mpz_to_base60_digits(tmp, out->int_digs);
        mpz_clear(tmp);
        if (out->nint < 0) { out->valid = false; return; }

        /* frac_d == 0.0 here means d was exactly integral (e.g. 1.0) --
         * skip the loop entirely rather than running one iteration that
         * multiplies 0.0 by 60 and emits a spurious trailing "0" digit
         * (e.g. rendering 1.0 as "1;0" / "𒁹 · 𒑊" instead of plain "1"). */
        while (frac_d != 0.0 && out->nfrac < max_frac && out->nfrac < 32) {
            frac_d *= 60.0;
            double fi; frac_d = modf(frac_d, &fi);
            out->frac_digs[out->nfrac++] = (int)fi;
        }
    } else {
        out->valid = false;
    }
}

/* ---- Public API ---- */

/* Parse Neugebauer string "d0,d1,...[;f0,f1,...]" to a Scheme number.
 * No semicolon → pure base-60 integer.
 * With semicolon → integer.fractional exact rational. */
val_t sex_parse_neugebauer(const char *s) {
    /* Quick validation: only digits, commas, semicolons */
    for (const char *p = s; *p; p++) {
        if (!isdigit((unsigned char)*p) && *p != ',' && *p != ';') return V_FALSE;
    }

    const char *semi = strchr(s, ';');
    mpq_t result; mpq_init(result); mpq_set_ui(result, 0, 1);

    /* Integer digits */
    const char *p = s;
    const char *int_end = semi ? semi : (s + strlen(s));
    while (p < int_end) {
        char *next;
        long digit = strtol(p, &next, 10);
        if (next == p || digit < 0 || digit > 59) { mpq_clear(result); return V_FALSE; }
        mpq_t tmp; mpq_init(tmp);
        mpq_set_ui(tmp, 60, 1); mpq_mul(result, result, tmp);
        mpq_set_si(tmp, digit, 1); mpq_add(result, result, tmp);
        mpq_clear(tmp);
        p = next;
        if (p < int_end) {
            if (*p == ',') p++;
            else { mpq_clear(result); return V_FALSE; }
        }
    }

    /* Fractional digits */
    if (semi) {
        p = semi + 1;
        mpq_t denom; mpq_init(denom); mpq_set_ui(denom, 60, 1);
        bool ok = true;
        while (*p && ok) {
            char *next;
            long digit = strtol(p, &next, 10);
            if (next == p || digit < 0 || digit > 59) { ok = false; break; }
            mpq_t frac; mpq_init(frac);
            mpq_set_si(frac, digit, 1); mpq_div(frac, frac, denom);
            mpq_add(result, result, frac); mpq_clear(frac);
            mpq_t s60; mpq_init(s60); mpq_set_ui(s60, 60, 1);
            mpq_mul(denom, denom, s60); mpq_clear(s60);
            p = next;
            if (*p == ',') p++;
            else if (*p) { ok = false; break; }
        }
        mpq_clear(denom);
        if (!ok) { mpq_clear(result); return V_FALSE; }
    }

    val_t r = make_rat_from_mpq(result);
    mpq_clear(result);
    return r;
}

/* Parse one maximal run of space-separated sexagesimal digit-groups starting
 * at *pp (a group is ASH/U/SHAR2 glyphs, additive within the group). Fills
 * digits[] (MSB-first, up to max entries) and returns the count parsed.
 * Returns -1 on a malformed group (invalid glyph mid-group, digit > 59, or
 * more than max digits). Leaves *pp just past the last consumed group --
 * on a clean stop (end of string, or the next thing isn't a digit-group)
 * it does NOT consume the trailing spaces that led to that discovery, so
 * callers can inspect what follows (e.g. the '·' radix separator). */
static int sex_parse_cuneiform_digit_run(const char **pp, int *digits, int max) {
    const char *p = *pp;
    int n = 0;
    while (*p) {
        const char *before_spaces = p;
        while (*p == ' ') p++;
        if (!*p) { p = before_spaces; break; }

        const char *gstart = p;
        int cp = sex_decode_cp(&p);
        if (cp < 0) { p = before_spaces; break; }
        uint32_t ucp = (uint32_t)cp;
        if (ucp != CP_ASH && ucp != CP_U && ucp != CP_SHAR2) { p = before_spaces; break; }

        /* Re-read the full group (until space or end) */
        int tens = 0, ones = 0;
        bool is_zero_ph = false;
        p = gstart;
        while (*p && *p != ' ') {
            const char *gp = p;
            int gcp = sex_decode_cp(&p);
            if (gcp < 0) { p = gp; break; }
            uint32_t ugcp = (uint32_t)gcp;
            if      (ugcp == CP_U)     tens++;
            else if (ugcp == CP_ASH)   ones++;
            else if (ugcp == CP_SHAR2) is_zero_ph = true;
            else { p = gp; break; }
        }

        int digit = is_zero_ph ? 0 : tens * 10 + ones;
        if (digit > 59 || n >= max) return -1;
        digits[n++] = digit;
    }
    *pp = p;
    return n;
}

/* Parse a cuneiform glyph string to an exact number: a run of sexagesimal
 * digit-groups (space-separated) for the integer part, optionally followed
 * by " · " (the radix separator sex_to_cuneiform() emits) and another digit
 * run for the fractional part, optionally preceded by '-'. Returns V_FALSE
 * for anything that doesn't parse as a WHOLE (no partial-prefix successes --
 * see reader.c's cuneiform token dispatch, which relies on that to fall back
 * to interning an unrecognized token as a symbol, e.g. "𒁹𒈠" must fail
 * entirely rather than silently parsing as 1 and discarding "𒈠"). */
/* Parse one signed sexagesimal number (optional leading '-', an integer
 * digit-group run, optionally followed by " · " and a fractional digit-group
 * run) at *pp. Returns true and sets *out, advancing *pp past the number, on
 * success; false (leaving *pp unspecified) if there's no valid number there
 * at all. This is the core sex_parse_cuneiform() itself uses for a top-level
 * literal, and that the extended (complex/quaternion/octonion/multivector)
 * notation below reuses for the magnitude of each term. */
static bool sex_parse_cuneiform_number(const char **pp, val_t *out) {
    const char *p = *pp;
    bool neg = false;
    if (*p == '-') { neg = true; p++; }

    int int_digs[64];
    int nint = sex_parse_cuneiform_digit_run(&p, int_digs, 64);
    if (nint <= 0) return false;

    int frac_digs[64];
    int nfrac = 0;
    const char *after_int = p;
    while (*after_int == ' ') after_int++;
    const char *dot_end = after_int;
    int dcp = sex_decode_cp(&dot_end);
    if (dcp == (int)CP_MIDDLE_DOT) {
        const char *q = dot_end;
        while (*q == ' ') q++;
        int n = sex_parse_cuneiform_digit_run(&q, frac_digs, 64);
        /* If '·' isn't followed by digits, leave it unconsumed rather than
         * guessing -- the caller's own "whole string consumed" check will
         * reject it. */
        if (n > 0) { nfrac = n; p = q; }
    }

    if (nfrac == 0) {
        mpz_t total; mpz_init_set_ui(total, 0);
        for (int i = 0; i < nint; i++) {
            mpz_mul_ui(total, total, 60);
            mpz_add_ui(total, total, (unsigned long)int_digs[i]);
        }
        if (neg) mpz_neg(total, total);
        *out = make_big_from_mpz(total);
        mpz_clear(total);
    } else {
        mpq_t result; mpq_init(result); mpq_set_ui(result, 0, 1);
        mpq_t sixty; mpq_init(sixty); mpq_set_ui(sixty, 60, 1);
        for (int i = 0; i < nint; i++) {
            mpq_t tmp; mpq_init(tmp);
            mpq_mul(result, result, sixty);
            mpq_set_si(tmp, int_digs[i], 1); mpq_add(result, result, tmp);
            mpq_clear(tmp);
        }
        mpq_t denom; mpq_init(denom); mpq_set(denom, sixty);
        for (int i = 0; i < nfrac; i++) {
            mpq_t frac; mpq_init(frac);
            mpq_set_si(frac, frac_digs[i], 1); mpq_div(frac, frac, denom);
            mpq_add(result, result, frac); mpq_clear(frac);
            mpq_mul(denom, denom, sixty);
        }
        mpq_clear(denom); mpq_clear(sixty);
        if (neg) mpq_neg(result, result);
        *out = make_rat_from_mpq(result);
        mpq_clear(result);
    }
    *pp = p;
    return true;
}

/* Parse exactly one cuneiform digit-group (ASH/U/SHAR2 run, no leading
 * space) at *pp -- the single sexagesimal digit immediately following a
 * CP_SIGN_E basis marker (e.g. the "1" in "E1"). Returns the digit (0-59),
 * or -1 if there's none/it's invalid. Advances *pp past it on success. */
static int sex_parse_one_cuneiform_digit(const char **pp) {
    const char *p = *pp;
    int tens = 0, ones = 0;
    bool is_zero_ph = false, got = false;
    while (*p) {
        const char *gp = p;
        int gcp = sex_decode_cp(&p);
        if (gcp < 0) { p = gp; break; }
        uint32_t ugcp = (uint32_t)gcp;
        if      (ugcp == CP_U)     { tens++; got = true; }
        else if (ugcp == CP_ASH)   { ones++; got = true; }
        else if (ugcp == CP_SHAR2) { is_zero_ph = true; got = true; }
        else { p = gp; break; }
    }
    if (!got) return -1;
    int digit = is_zero_ph ? 0 : tens * 10 + ones;
    if (digit > 59) return -1;
    *pp = p;
    return digit;
}

/* One signed term of extended (complex/quaternion/octonion/multivector)
 * cuneiform notation: a sign, a magnitude, and the basis blade it multiplies
 * (a run of one-or-more CP_SIGN_E + digit units; empty for the scalar term,
 * which is handled separately by the caller). */
typedef struct { bool neg; val_t mag; int idxs[8]; int nidx; } CunTerm;

/* A multivector caps at 8 basis vectors (Cl(p,q,r), n=p+q+r<=8), i.e. up to
 * 2^8=256 blades, 255 of them non-scalar -- the true worst case for how many
 * extended-notation terms a single literal can carry (quaternion/octonion
 * only ever need 3/7, but the general-multivector fallback below needs
 * headroom for all of them or a legitimately-written literal with more than
 * 8 nonzero non-scalar blades would be rejected instead of round-tripping). */
#define CUN_MAX_TERMS 255

/* Parse the "('+'|'-') <magnitude> <marker>..." tail that follows a leading
 * scalar number in extended cuneiform notation. `first` is that already-
 * parsed leading number; `p` points just past it. Returns V_FALSE (fail
 * closed, same contract as sex_parse_cuneiform itself) on any malformed
 * term, a double-signed magnitude (e.g. "1--2i"), an out-of-range or
 * cross-term-repeated basis index (only 1..8 are valid: Multivector caps at
 * 8 basis vectors), or trailing unconsumed input. */
static val_t sex_parse_cuneiform_extended(val_t first, const char *p) {
    CunTerm terms[CUN_MAX_TERMS];   /* ~12KB on the stack -- actors run this
                                     * concurrently, so this must not be static */
    int nterms = 0;

    while (*p == '+' || *p == '-') {
        bool tneg = (*p == '-');
        p++;
        val_t mag;
        if (!sex_parse_cuneiform_number(&p, &mag)) return V_FALSE;
        /* A magnitude following an explicit sign token must not itself carry
         * one -- otherwise "1--2i" would parse as 1+2i instead of being
         * rejected (the two negatives silently cancelling). */
        if (num_is_negative(mag)) return V_FALSE;

        const char *mp = p;
        int mcp = sex_decode_cp(&mp);
        if (mcp == (int)CP_SIGN_I) {
            /* Complex: this must be the one and only term, and the marker
             * must end the string -- "a+bi" has exactly one imaginary part. */
            if (nterms != 0 || *mp != '\0') return V_FALSE;
            return num_make_complex(first, tneg ? num_neg(mag) : mag);
        }
        if (mcp != (int)CP_SIGN_E) return V_FALSE;

        int idxs[8], nidx = 0;
        while (mcp == (int)CP_SIGN_E) {
            p = mp; /* consume the E marker */
            int digit = sex_parse_one_cuneiform_digit(&p);
            if (digit < 1 || digit > 8 || nidx >= 8) return V_FALSE;
            idxs[nidx++] = digit;
            mp = p;
            mcp = sex_decode_cp(&mp);
        }
        if (nidx == 0 || nterms >= CUN_MAX_TERMS) return V_FALSE;
        terms[nterms].neg = tneg; terms[nterms].mag = mag;
        memcpy(terms[nterms].idxs, idxs, sizeof(idxs));
        terms[nterms].nidx = nidx;
        nterms++;
    }

    if (*p != '\0' || nterms == 0) return V_FALSE;

    /* Quaternion: a+b·E1+c·E2+d·E3, in that exact order, one index each --
     * the canonical form sex_to_cuneiform() writes for a T_QUATERNION.
     *
     * Note this treats i,j,k as grade-1 basis vectors e1,e2,e3 directly --
     * a different (and simpler, for notation purposes) convention from
     * `quaternion->mv`/`mv->quaternion` (multivec.c), which embed a
     * quaternion into Cl(3,0,0)'s *even* subalgebra via bivectors
     * (i=e12, j=e23, k=e13). The two conventions are unrelated: a
     * T_QUATERNION parsed here is never converted through a Multivector,
     * so this doesn't need to (and doesn't) agree with that embedding. */
    if (nterms == 3
        && terms[0].nidx == 1 && terms[0].idxs[0] == 1
        && terms[1].nidx == 1 && terms[1].idxs[0] == 2
        && terms[2].nidx == 1 && terms[2].idxs[0] == 3) {
        double a = num_to_double(first);
        double b = num_to_double(terms[0].mag) * (terms[0].neg ? -1.0 : 1.0);
        double c = num_to_double(terms[1].mag) * (terms[1].neg ? -1.0 : 1.0);
        double d = num_to_double(terms[2].mag) * (terms[2].neg ? -1.0 : 1.0);
        return num_make_quat(a, b, c, d);
    }

    /* Octonion: a+b·E1+...+h·E7, in that exact order, one index each. */
    if (nterms == 7) {
        bool ok = true;
        for (int i = 0; i < 7 && ok; i++)
            ok = (terms[i].nidx == 1 && terms[i].idxs[0] == i + 1);
        if (ok) {
            double e[8];
            e[0] = num_to_double(first);
            for (int i = 0; i < 7; i++)
                e[i + 1] = num_to_double(terms[i].mag) * (terms[i].neg ? -1.0 : 1.0);
            return num_make_oct(e);
        }
    }

    /* Otherwise: a general multivector. Cuneiform notation only records
     * coefficients per blade, never the metric signature (p,q,r aren't
     * observable from "E1"/"E2" alone) -- reconstruct as a Euclidean
     * Cl(n,0,0) sized to the highest basis index referenced. This loses
     * signature fidelity for non-Euclidean algebras (Cl(3,1,0), etc.);
     * acceptable for what this notation is for, but not a lossless
     * round-trip in general. */
    int n = 1;
    for (int i = 0; i < nterms; i++)
        for (int k = 0; k < terms[i].nidx; k++)
            if (terms[i].idxs[k] > n) n = terms[i].idxs[k];

    val_t mvv = mv_make(n, 0, 0);
    Multivector *mv = as_mv(mvv);
    mv->c[0] = num_to_double(first);
    bool blade_seen[256] = { false };   /* every blade but scalar starts unset;
                                         * catches a term repeating a blade
                                         * across DIFFERENT terms (not just
                                         * within one), which used to silently
                                         * overwrite instead of failing closed */
    for (int i = 0; i < nterms; i++) {
        uint32_t bitmap = 0;
        for (int k = 0; k < terms[i].nidx; k++) {
            uint32_t bit = 1u << (terms[i].idxs[k] - 1);
            if (bitmap & bit) return V_FALSE; /* repeated index within one blade */
            bitmap |= bit;
        }
        if (blade_seen[bitmap]) return V_FALSE; /* same blade written by an earlier term */
        blade_seen[bitmap] = true;
        double val = num_to_double(terms[i].mag);
        if (terms[i].neg) val = -val;
        mv->c[bitmap] = val;
    }
    return mvv;
}

val_t sex_parse_cuneiform(const char *s) {
    const char *p = s;
    val_t first;
    if (!sex_parse_cuneiform_number(&p, &first)) return V_FALSE;
    if (*p == '\0') return first;
    return sex_parse_cuneiform_extended(first, p);
}

/* Format a number in Neugebauer notation.
 * max_frac: max fractional sexagesimal places; <0 = auto (exact for rationals, 4 for floats).
 * Returns a Scheme string. */
val_t sex_to_neugebauer(val_t v, int max_frac) {
    /* Special flonum checks */
    if (vis_flonum(v)) {
        double d = vfloat(v);
        if (d != d)        return num_to_string(v, 10);  /* NaN */
        if (d == 1.0/0.0)  return num_to_string(v, 10);  /* +inf.0 */
        if (d == -1.0/0.0) return num_to_string(v, 10);  /* -inf.0 */
        if (max_frac < 0) max_frac = 4;
    }

    SexDigs digs;
    sex_get_digits(v, max_frac, &digs);
    if (!digs.valid) return num_to_string(v, 10);

    SexBuf b; sexbuf_init(&b);
    if (digs.neg) sexbuf_push(&b, '-');
    sex_render_neugebauer_digits(&b, digs.int_digs, digs.nint);
    if (digs.nfrac > 0) {
        sexbuf_push(&b, ';');
        sex_render_neugebauer_digits(&b, digs.frac_digs, digs.nfrac);
    }
    return sexbuf_to_val(&b);
}

/* Append one real (fixnum/bignum/rational/flonum) number's cuneiform digits
 * to b (with the "·" radix-point separator for a fractional part). Returns
 * false, leaving b unmodified in effect (caller discards it), if v is a
 * NaN/infinite flonum or otherwise not cuneiform-representable. */
static bool sex_append_cuneiform_number(SexBuf *b, val_t v, int max_frac) {
    if (vis_flonum(v)) {
        double d = vfloat(v);
        if (d != d || d == 1.0/0.0 || d == -1.0/0.0) return false;
    }
    SexDigs digs;
    sex_get_digits(v, max_frac, &digs);
    if (!digs.valid) return false;
    if (digs.neg) sexbuf_push(b, '-');
    sex_render_cuneiform_digits(b, digs.int_digs, digs.nint);
    if (digs.nfrac > 0) {
        sexbuf_push(b, ' ');
        sexbuf_push_cp(b, CP_MIDDLE_DOT);
        sexbuf_push(b, ' ');
        sex_render_cuneiform_digits(b, digs.frac_digs, digs.nfrac);
    }
    return true;
}

/* Append a signed extended-notation term's magnitude: "('+'|'-') <|mag|>". */
static bool sex_append_signed_term(SexBuf *b, double mag, int max_frac) {
    sexbuf_push(b, mag < 0.0 ? '-' : '+');
    return sex_append_cuneiform_number(b, num_make_float(fabs(mag)), max_frac);
}

/* Append the basis-blade suffix for a term: one CP_SIGN_E + digit per index. */
static void sex_append_basis_units(SexBuf *b, const int *idxs, int nidx) {
    for (int i = 0; i < nidx; i++) {
        sexbuf_push_cp(b, CP_SIGN_E);
        sex_render_cuneiform_digit(b, idxs[i]);
    }
}

/* Format a number as cuneiform glyph string.
 *
 * Real (fixnum/bignum/rational/flonum): plain sexagesimal digits, "·" radix
 * point for a fractional part (see sex_append_cuneiform_number).
 *
 * Complex/quaternion/octonion/multivector: "<scalar> ('+'|'-') <mag> <unit>"
 * repeated per component, where <unit> is CP_SIGN_I (the traditional single
 * imaginary axis) for complex, or one-or-more "CP_SIGN_E <digit>" basis-blade
 * units for the others (quaternion e1,e2,e3; octonion e1..e7; multivector
 * whatever blades are nonzero, digit = 1-based basis index, matching the
 * bitmap convention mv_write() already uses for its ASCII "e13" labels).
 * These four all round-trip through sex_parse_cuneiform() (see
 * sex_parse_cuneiform_extended) except that a general multivector's metric
 * signature (p,q,r) isn't recoverable from the notation and is reconstructed
 * as Euclidean on read-back.
 *
 * Anything else (surreal, symbolic, ...) falls back to num_to_string(),
 * same as an unrepresentable real number.
 */
val_t sex_to_cuneiform(val_t v) {
    int max_frac = 6;

    if (vis_surreal(v)) {
        Surreal *s = as_surreal(v);
        SexBuf b; sexbuf_init(&b);
        bool ok = (s->nterms > 0);
        for (int i = 0; i < s->nterms && ok; i++) {
            val_t exp = s->data[2*i], coeff = s->data[2*i + 1];
            if (i == 0) {
                ok = sex_append_cuneiform_number(&b, coeff, max_frac);
            } else {
                bool cneg = num_is_negative(coeff);
                sexbuf_push(&b, cneg ? '-' : '+');
                ok = sex_append_cuneiform_number(&b, cneg ? num_neg(coeff) : coeff, max_frac);
            }
            /* Real (exponent 0) terms are plain numbers; anything else is an
             * "order of ω" quantity, marked with the DINGIR determinative. */
            if (ok && !num_is_zero(exp)) {
                sexbuf_push_cp(&b, CP_DINGIR);
                ok = sex_append_cuneiform_number(&b, exp, max_frac);
            }
        }
        if (ok) return sexbuf_to_val(&b);
        free(b.buf);
        return num_to_string(v, 10);
    }

    if (vis_symbolic(v) || vis_symfn(v)) {
        /* Best-effort: writer only (see sx_write_cuneiform in
         * symbolic_print.c), no cuneiform-symbolic reader. */
        val_t p = port_open_output_string();
        sx_write_cuneiform(v, p);
        return port_get_output_string(p);
    }

    if (vis_complex(v)) {
        val_t re = as_cpx(v)->real, im = as_cpx(v)->imag;
        bool im_neg = num_is_negative(im);
        val_t im_mag = im_neg ? num_neg(im) : im;
        SexBuf b; sexbuf_init(&b);
        bool ok = sex_append_cuneiform_number(&b, re, max_frac);
        if (ok) { sexbuf_push(&b, im_neg ? '-' : '+'); ok = sex_append_cuneiform_number(&b, im_mag, max_frac); }
        if (ok) { sexbuf_push_cp(&b, CP_SIGN_I); return sexbuf_to_val(&b); }
        free(b.buf);
        return num_to_string(v, 10);
    }

    if (vis_quat(v)) {
        Quaternion *q = as_quat(v);
        SexBuf b; sexbuf_init(&b);
        bool ok = sex_append_cuneiform_number(&b, num_make_float(q->a), max_frac);
        if (ok) ok = sex_append_signed_term(&b, q->b, max_frac);
        if (ok) { int idx[] = {1}; sex_append_basis_units(&b, idx, 1); }
        if (ok) ok = sex_append_signed_term(&b, q->c, max_frac);
        if (ok) { int idx[] = {2}; sex_append_basis_units(&b, idx, 1); }
        if (ok) ok = sex_append_signed_term(&b, q->d, max_frac);
        if (ok) { int idx[] = {3}; sex_append_basis_units(&b, idx, 1); }
        if (ok) return sexbuf_to_val(&b);
        free(b.buf);
        return num_to_string(v, 10);
    }

    if (vis_oct(v)) {
        Octonion *o = as_oct(v);
        SexBuf b; sexbuf_init(&b);
        bool ok = sex_append_cuneiform_number(&b, num_make_float(o->e[0]), max_frac);
        for (int i = 1; ok && i <= 7; i++) {
            ok = sex_append_signed_term(&b, o->e[i], max_frac);
            if (ok) { int idx[] = {i}; sex_append_basis_units(&b, idx, 1); }
        }
        if (ok) return sexbuf_to_val(&b);
        free(b.buf);
        return num_to_string(v, 10);
    }

    if (vis_mv(v)) {
        Multivector *mv = as_mv(v);
        SexBuf b; sexbuf_init(&b);
        bool ok = sex_append_cuneiform_number(&b, num_make_float(mv->c[0]), max_frac);
        for (uint32_t i = 1; ok && i < mv->dim; i++) {
            double c = mv->c[i];
            if (c == 0.0) continue;
            ok = sex_append_signed_term(&b, c, max_frac);
            if (ok) {
                int idxs[8], nidx = 0;
                for (int k = 0; k < mv->n; k++) if (i & (1u << k)) idxs[nidx++] = k + 1;
                sex_append_basis_units(&b, idxs, nidx);
            }
        }
        if (ok) return sexbuf_to_val(&b);
        free(b.buf);
        return num_to_string(v, 10);
    }

    SexBuf b; sexbuf_init(&b);
    if (sex_append_cuneiform_number(&b, v, max_frac)) return sexbuf_to_val(&b);
    free(b.buf);
    return num_to_string(v, 10);
}
