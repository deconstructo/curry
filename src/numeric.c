#include "numeric.h"
#include "object.h"
#include "gc.h"
#include "eval.h"
#include "symbol.h"
#include "symbolic.h"
#include "quantum.h"
#include "surreal.h"
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
    Tuple *ta = as_tuple(a), *tb = as_tuple(b);
    if (ta->hdr.type != tb->hdr.type || ta->len != tb->len)
        scm_raise(V_FALSE, "tuple %s: type/dimension mismatch", ctx);
    val_t buf[256]; uint32_t n = ta->len < 256 ? ta->len : 256;
    for (uint32_t i = 0; i < n; i++) buf[i] = op(ta->data[i], tb->data[i]);
    return num_make_tuple((int)ta->hdr.type, n, buf);
}

/* Apply a unary numeric op element-wise to a tuple. */
static val_t tuple_unop(val_t a, val_t (*op)(val_t)) {
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
    if (vis_oct(a)) return num_oct_norm(a);
    if (vis_complex(a)) {
        /* Same computation num_magnitude already does for its complex
         * case — kept in sync here (rather than having num_abs simply
         * defer to num_magnitude) since num_magnitude's OWN non-complex
         * path is defined as "defer to num_abs", and having each call the
         * other would be circular. Consistent with the quaternion/octonion
         * cases just above: this file already treats num_abs as this
         * numeric tower's general absolute-value/norm operation across
         * every type it represents geometrically, not just R7RS reals —
         * found necessary when fixing this function's missing error
         * fallthrough (see below) surfaced that src/symbolic.c's sx_abs
         * calls num_abs directly for any vis_number operand, including
         * complex/octonion ones, and had been silently getting back the
         * operand unchanged (itself already a latent correctness bug, not
         * something this newly introduces) instead of a real answer. */
        double r = num_to_double(as_cpx(a)->real), i = num_to_double(as_cpx(a)->imag);
        return num_make_float(sqrt(r*r + i*i));
    }
    /* Unlike every sibling num_* op (num_to_double, to_mpz, ...), this used
     * to fall through silently, returning a's unrecognized value unchanged
     * instead of raising — found while auditing this file for OOP Layer 3.
     * Matches num_to_double's error shape (src/numeric.c) for consistency. */
    scm_raise(V_FALSE, "not a number: %s",
              vis_pair(a) ? "#<pair>" :
              vis_nil(a)  ? "()"      :
              (a == V_TRUE) ? "#t"    :
              (a == V_FALSE) ? "#f"   : "#<non-numeric>");
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
            uint32_t len = (uint32_t)strlen(s);
            String *str = (String *)gc_alloc_atomic(sizeof(String) + len + 1);
            str->hdr.type=T_STRING; str->hdr.flags=0; str->len=len; str->orig_cap=len; str->ext=NULL;
            memcpy(str->data, s, len+1); free(s); mpz_clear(z);
            return vptr(str);
        }
    } else if (vis_bignum(v)) {
        char *s = mpz_get_str(NULL, radix, as_big(v)->z);
        /* wrap in String */
        uint32_t len = (uint32_t)strlen(s);
        String *str = (String *)gc_alloc_atomic(sizeof(String) + len + 1);
        str->hdr.type=T_STRING; str->hdr.flags=0; str->len=len; str->orig_cap=len; str->ext=NULL;
        memcpy(str->data, s, len+1); free(s); return vptr(str);
    } else if (vis_flonum(v)) {
        snprintf(buf, sizeof(buf), "%g", vfloat(v));
#ifdef BUILD_MPFR
    } else if (vis_mpfr(v)) {
        return mpfr_to_string(v, 0, radix);
#endif
    } else {
        snprintf(buf, sizeof(buf), "#<number>");
    }
    uint32_t len = (uint32_t)strlen(buf);
    String *str = (String *)gc_alloc_atomic(sizeof(String) + len + 1);
    str->hdr.type=T_STRING; str->hdr.flags=0; str->len=len; str->orig_cap=len; str->ext=NULL;
    memcpy(str->data, buf, len+1);
    return vptr(str);
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

/* ---- UTF-8 helpers ---- */

/* Decode one codepoint from *s; advance *s past the bytes. Returns -1 at end. */
static int sex_decode_cp(const char **s) {
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

        for (int i = 0; i < max_frac && i < 32; i++) {
            frac_d *= 60.0;
            double fi; frac_d = modf(frac_d, &fi);
            out->frac_digs[out->nfrac++] = (int)fi;
            if (frac_d == 0.0) break;
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

/* Parse a cuneiform glyph string to an exact integer.
 * Groups (space-separated) are sexagesimal digits; within each group
 * count ASH (=1) and U (=10), or SHAR2 (=0). */
val_t sex_parse_cuneiform(const char *s) {
    mpz_t total; mpz_init_set_ui(total, 0);
    bool got_any = false;
    const char *p = s;

    while (*p) {
        /* Skip inter-group spaces */
        while (*p == ' ') p++;
        if (!*p) break;

        /* Decode first glyph of group */
        const char *before = p;
        int cp = sex_decode_cp(&p);
        if (cp < 0) break;
        uint32_t ucp = (uint32_t)cp;
        if (ucp != CP_ASH && ucp != CP_U && ucp != CP_SHAR2) {
            p = before; break; /* not a cuneiform glyph — stop */
        }

        /* Read the full group (until space or end) */
        int tens = 0, ones = 0;
        bool is_zero_ph = false;
        p = before; /* re-read from start of group */
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
        if (digit > 59) { mpz_clear(total); return V_FALSE; }

        if (got_any) mpz_mul_ui(total, total, 60);
        mpz_add_ui(total, total, (unsigned long)digit);
        got_any = true;
    }

    val_t r = make_big_from_mpz(total);
    mpz_clear(total);
    return got_any ? r : V_FALSE;
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

/* Format a number as cuneiform glyph string.
 * Returns a Scheme string. */
val_t sex_to_cuneiform(val_t v) {
    int max_frac = 6;
    if (vis_flonum(v)) {
        double d = vfloat(v);
        if (d != d || d == 1.0/0.0 || d == -1.0/0.0) return num_to_string(v, 10);
    }

    SexDigs digs;
    sex_get_digits(v, max_frac, &digs);
    if (!digs.valid) return num_to_string(v, 10);

    SexBuf b; sexbuf_init(&b);
    if (digs.neg) sexbuf_push(&b, '-');
    sex_render_cuneiform_digits(&b, digs.int_digs, digs.nint);
    if (digs.nfrac > 0) {
        /* Radix point indicated by '·' separator between integer and fractional groups */
        sexbuf_push(&b, ' ');
        sexbuf_push_cp(&b, 0xB7u); /* U+00B7 MIDDLE DOT as radix separator */
        sexbuf_push(&b, ' ');
        sex_render_cuneiform_digits(&b, digs.frac_digs, digs.nfrac);
    }
    return sexbuf_to_val(&b);
}
