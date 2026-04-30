#include "numeric.h"
#include "object.h"
#include "gc.h"
#include "symbolic.h"
#include "quantum.h"
#include "surreal.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include <stdio.h>

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
    Bignum *b = CURRY_NEW_ATOM(Bignum);
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
    Rational *r = CURRY_NEW_ATOM(Rational);
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
    Bignum *b = CURRY_NEW_ATOM(Bignum);
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

/* ---- Coercions ---- */

double num_to_double(val_t v) {
    if (vis_fixnum(v))   return (double)vunfix(v);
    if (vis_flonum(v))   return vfloat(v);
    if (vis_bignum(v))   return mpz_get_d(as_big(v)->z);
    if (vis_rational(v)) return mpq_get_d(as_rat(v)->q);
    if (vis_complex(v))  return num_to_double(as_cpx(v)->real); /* drop imag */
    if (vis_surreal(v))  return sur_to_double(v);
    assert(0 && "num_to_double: not a real number");
}

long num_to_long(val_t v) {
    if (vis_fixnum(v)) return vunfix(v);
    if (vis_bignum(v)) return mpz_get_si(as_big(v)->z);
    assert(0 && "num_to_long: not an exact integer");
}

val_t num_inexact(val_t v) {
    if (vis_flonum(v)) return v;
    return num_make_float(num_to_double(v));
}

val_t num_exact(val_t v) {
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
    if (vis_surreal(v))  return sur_is_zero(v);
    return false;
}

bool num_is_positive(val_t v) {
    if (vis_fixnum(v))   return vunfix(v) > 0;
    if (vis_flonum(v))   return vfloat(v) > 0.0;
    if (vis_bignum(v))   return mpz_sgn(as_big(v)->z) > 0;
    if (vis_rational(v)) return mpq_sgn(as_rat(v)->q) > 0;
    if (vis_surreal(v))  return sur_is_positive(v);
    return false;
}

bool num_is_negative(val_t v) {
    if (vis_fixnum(v))   return vunfix(v) < 0;
    if (vis_flonum(v))   return vfloat(v) < 0.0;
    if (vis_bignum(v))   return mpz_sgn(as_big(v)->z) < 0;
    if (vis_rational(v)) return mpq_sgn(as_rat(v)->q) < 0;
    if (vis_surreal(v))  return sur_is_negative(v);
    return false;
}

bool num_is_finite(val_t v)   {
    if (vis_surreal(v)) return sur_finite_p(v);
    return !vis_flonum(v) || isfinite(vfloat(v));
}

bool num_is_infinite(val_t v) {
    if (vis_surreal(v)) return sur_infinite_p(v);
    return vis_flonum(v) && isinf(vfloat(v));
}

bool num_is_nan(val_t v)      { return vis_flonum(v) && isnan(vfloat(v)); }

bool num_is_integer(val_t v) {
    if (vis_fixnum(v) || vis_bignum(v)) return true;
    if (vis_flonum(v)) { double d = vfloat(v); return isfinite(d) && d == trunc(d); }
    if (vis_rational(v)) return mpz_cmp_ui(mpq_denref(as_rat(v)->q), 1) == 0;
    return false;
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
        double qa[4] = {0,0,0,0}, qb[4] = {0,0,0,0};
        if (vis_quat(a)) { Quaternion *q = as_quat(a); qa[0]=q->a; qa[1]=q->b; qa[2]=q->c; qa[3]=q->d; }
        else qa[0] = num_to_double(a);
        if (vis_quat(b)) { Quaternion *q = as_quat(b); qb[0]=q->a; qb[1]=q->b; qb[2]=q->c; qb[3]=q->d; }
        else qb[0] = num_to_double(b);
        return num_make_quat(qa[0]+qb[0], qa[1]+qb[1], qa[2]+qb[2], qa[3]+qb[3]);
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
    if (vis_symbolic(a) || vis_symbolic(b)) return sx_sub(a, b);
    if (vis_quantum(a) || vis_quantum(b)) {
        if (vis_quantum(a) && vis_quantum(b)) return quantum_superpose(a, quantum_mul_scalar(b, vfix(-1)));
        return vis_quantum(a) ? quantum_sub_scalar(a, b) : quantum_sub_scalar(b, num_neg(a));
    }
    if (vis_surreal(a) || vis_surreal(b)) return sur_sub(a, b);
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
    if (vis_symbolic(a) || vis_symbolic(b)) return sx_mul(a, b);
    if (vis_quantum(a) || vis_quantum(b)) {
        if (vis_quantum(a) && vis_quantum(b)) return quantum_superpose(a, b);
        return vis_quantum(a) ? quantum_mul_scalar(a, b) : quantum_mul_scalar(b, a);
    }
    if (vis_surreal(a) || vis_surreal(b)) return sur_mul(a, b);
    /* Quaternion multiplication (non-commutative: Hamilton product) */
    if (vis_quat(a) || vis_quat(b)) {
        double a0,a1,a2,a3, b0,b1,b2,b3;
        if (vis_quat(a)) { a0=as_quat(a)->a; a1=as_quat(a)->b; a2=as_quat(a)->c; a3=as_quat(a)->d; }
        else { a0=num_to_double(a); a1=a2=a3=0; }
        if (vis_quat(b)) { b0=as_quat(b)->a; b1=as_quat(b)->b; b2=as_quat(b)->c; b3=as_quat(b)->d; }
        else { b0=num_to_double(b); b1=b2=b3=0; }
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
    if (vis_symbolic(a) || vis_symbolic(b)) return sx_div(a, b);
    if (vis_quantum(a) || vis_quantum(b)) {
        if (vis_quantum(a) && !vis_quantum(b)) return quantum_div_scalar(a, b);
        scm_raise(V_FALSE, "cannot divide by a quantum value");
    }
    if (vis_surreal(a) || vis_surreal(b)) return sur_div(a, b);
    if (vis_flonum(a) || vis_flonum(b))
        return num_make_float(num_to_double(a) / num_to_double(b));
    /* Exact division -> rational */
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
    if (vis_fixnum(a)) {
        intptr_t n = vunfix(a);
        return n < 0 ? (in_fixnum_range(-n) ? vfix(-n) : num_make_bignum_i(-n)) : a;
    }
    if (vis_flonum(a))   return num_make_float(fabs(vfloat(a)));
    if (vis_bignum(a))   { mpz_t z; mpz_init(z); mpz_abs(z, as_big(a)->z); val_t r=make_big_from_mpz(z); mpz_clear(z); return r; }
    if (vis_rational(a)) { mpq_t q; mpq_init(q); mpq_abs(q, as_rat(a)->q); val_t r=make_rat_from_mpq(q); mpq_clear(q); return r; }
    return a;
}

/* ---- Comparison ---- */
int num_cmp(val_t a, val_t b) {
    if (vis_surreal(a) || vis_surreal(b)) return sur_compare(a, b);
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
val_t num_min(val_t a, val_t b) { return num_le(a,b)?a:b; }
val_t num_max(val_t a, val_t b) { return num_ge(a,b)?a:b; }

/* ---- Integer division ---- */
val_t num_quotient(val_t a, val_t b) {
    if (vis_fixnum(a) && vis_fixnum(b)) {
        intptr_t ia = vunfix(a), ib = vunfix(b);
        return vfix(ia / ib);  /* C11 truncation towards zero */
    }
    mpz_t za,zb,zr; mpz_init(za); mpz_init(zb); mpz_init(zr);
    to_mpz(za,a); to_mpz(zb,b); mpz_tdiv_q(zr,za,zb);
    val_t r=make_big_from_mpz(zr); mpz_clear(za); mpz_clear(zb); mpz_clear(zr); return r;
}

val_t num_remainder(val_t a, val_t b) {
    if (vis_fixnum(a) && vis_fixnum(b)) return vfix(vunfix(a) % vunfix(b));
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
    if (vis_flonum(v)) return num_make_float(floor(vfloat(v)));
    if (vis_rational(v)) {
        mpz_t r; mpz_init(r);
        mpz_fdiv_q(r, mpq_numref(as_rat(v)->q), mpq_denref(as_rat(v)->q));
        val_t res = make_big_from_mpz(r); mpz_clear(r); return res;
    }
    return v; /* fixnum/bignum already exact integer */
}
val_t num_ceiling(val_t v) {
    if (vis_flonum(v)) return num_make_float(ceil(vfloat(v)));
    if (vis_rational(v)) {
        mpz_t r; mpz_init(r);
        mpz_cdiv_q(r, mpq_numref(as_rat(v)->q), mpq_denref(as_rat(v)->q));
        val_t res = make_big_from_mpz(r); mpz_clear(r); return res;
    }
    return v;
}
val_t num_truncate(val_t v) {
    if (vis_flonum(v)) return num_make_float(trunc(vfloat(v)));
    if (vis_rational(v)) {
        mpz_t r; mpz_init(r);
        mpz_tdiv_q(r, mpq_numref(as_rat(v)->q), mpq_denref(as_rat(v)->q));
        val_t res = make_big_from_mpz(r); mpz_clear(r); return res;
    }
    return v;
}
val_t num_round(val_t v) {
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
    if (vis_exact(base) && vis_fixnum(exp)) {
        long e = vunfix(exp);
        if (e == 0) return vfix(1);
        if (e > 0 && vis_fixnum(base)) {
            mpz_t z; mpz_init(z);
            mpz_set_si(z, vunfix(base));
            mpz_pow_ui(z, z, (unsigned long)e);
            val_t r = make_big_from_mpz(z); mpz_clear(z); return r;
        }
    }
    return num_make_float(pow(num_to_double(base), num_to_double(exp)));
}

val_t num_sqrt(val_t v) {
    if (vis_symbolic(v)) return sx_sqrt(v);
    if (vis_surreal(v))  return sur_expt(v, num_make_rational(vfix(1), vfix(2)));
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
    return num_make_float(sqrt(num_to_double(v)));
}

#define NUM_TRIG(fn) \
    val_t num_##fn(val_t v) { return num_make_float(fn(num_to_double(v))); }
#define NUM_TRIG_SX(fn) \
    val_t num_##fn(val_t v) { \
        if (vis_symbolic(v)) return sx_##fn(v); \
        return num_make_float(fn(num_to_double(v))); \
    }
NUM_TRIG_SX(exp) NUM_TRIG_SX(log) NUM_TRIG_SX(sin) NUM_TRIG_SX(cos) NUM_TRIG_SX(tan)
NUM_TRIG(asin) NUM_TRIG(acos) NUM_TRIG(atan)
val_t num_atan2(val_t y, val_t x) { return num_make_float(atan2(num_to_double(y), num_to_double(x))); }

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
val_t num_real_part(val_t v) { return vis_complex(v) ? as_cpx(v)->real : v; }
val_t num_imag_part(val_t v) { return vis_complex(v) ? as_cpx(v)->imag : vfix(0); }
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
        snprintf(buf, sizeof(buf), radix==16?"%lx":radix==8?"%lo":"%ld", (long)vunfix(v));
    } else if (vis_bignum(v)) {
        char *s = mpz_get_str(NULL, radix, as_big(v)->z);
        /* wrap in String */
        uint32_t len = (uint32_t)strlen(s);
        String *str = (String *)gc_alloc_atomic(sizeof(String) + len + 1);
        str->hdr.type=T_STRING; str->hdr.flags=0; str->len=len;
        memcpy(str->data, s, len+1); free(s); return vptr(str);
    } else if (vis_flonum(v)) {
        snprintf(buf, sizeof(buf), "%g", vfloat(v));
    } else {
        snprintf(buf, sizeof(buf), "#<number>");
    }
    uint32_t len = (uint32_t)strlen(buf);
    String *str = (String *)gc_alloc_atomic(sizeof(String) + len + 1);
    str->hdr.type=T_STRING; str->hdr.flags=0; str->len=len;
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
