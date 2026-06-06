#include "mpfr_num.h"

#ifdef BUILD_MPFR
#include "object.h"
#include "gc.h"
#include "numeric.h"
#include "eval.h"
#include <gmp.h>
#include <mpfr.h>
#include <math.h>
#include <string.h>
#include <stdlib.h>

/* Thread-local precision/rounding context. */
_Thread_local mpfr_prec_t tl_mpfr_prec = 0;
_Thread_local mpfr_rnd_t  tl_mpfr_rnd  = MPFR_RNDN;

/* ---------------------------------------------------------------------- */
/* GC finalizer — releases the mpfr_t limb storage.                       */
/* ---------------------------------------------------------------------- */
static void mpfr_finalize(void *obj, void *cd) {
    (void)cd;
    mpfr_clear(((Mpfr *)obj)->x);
}

/* ---------------------------------------------------------------------- */
/* Result precision helper: pick the maximum of the operands and the      */
/* thread-local context precision (with a sane default).                  */
/* ---------------------------------------------------------------------- */
static mpfr_prec_t bin_prec(val_t a, val_t b) {
    mpfr_prec_t pa = vis_mpfr(a) ? mpfr_val_prec(a) : 0;
    mpfr_prec_t pb = vis_mpfr(b) ? mpfr_val_prec(b) : 0;
    mpfr_prec_t pc = tl_mpfr_prec;
    mpfr_prec_t p  = pa > pb ? pa : pb;
    if (pc > p) p = pc;
    if (p == 0) p = MPFR_DEFAULT_PREC;
    return p;
}

static mpfr_prec_t un_prec(val_t a) {
    mpfr_prec_t pa = vis_mpfr(a) ? mpfr_val_prec(a) : 0;
    mpfr_prec_t pc = tl_mpfr_prec;
    mpfr_prec_t p  = pa > pc ? pa : pc;
    if (p == 0) p = MPFR_DEFAULT_PREC;
    return p;
}

/* ---------------------------------------------------------------------- */
/* Constructors                                                           */
/* ---------------------------------------------------------------------- */

val_t mpfr_make(mpfr_prec_t prec) {
    Mpfr *m = CURRY_NEW_ATOM(Mpfr);
    m->hdr.type  = T_MPFR;
    m->hdr.flags = 0;
    mpfr_init2(m->x, prec);
    GC_register_finalizer_no_order(m, mpfr_finalize, NULL, NULL, NULL);
    return vptr(m);
}

val_t mpfr_make_from_d(double d, mpfr_prec_t prec) {
    val_t v = mpfr_make(prec);
    mpfr_set_d(as_mpfr(v)->x, d, tl_mpfr_rnd);
    return v;
}

val_t mpfr_make_from_z(mpz_t z, mpfr_prec_t prec) {
    val_t v = mpfr_make(prec);
    mpfr_set_z(as_mpfr(v)->x, z, tl_mpfr_rnd);
    return v;
}

val_t mpfr_make_from_q(mpq_t q, mpfr_prec_t prec) {
    val_t v = mpfr_make(prec);
    mpfr_set_q(as_mpfr(v)->x, q, tl_mpfr_rnd);
    return v;
}

val_t mpfr_make_from_si(long n, mpfr_prec_t prec) {
    val_t v = mpfr_make(prec);
    mpfr_set_si(as_mpfr(v)->x, n, tl_mpfr_rnd);
    return v;
}

val_t mpfr_make_from_str(const char *s, int base, mpfr_prec_t prec) {
    val_t v = mpfr_make(prec);
    if (mpfr_set_str(as_mpfr(v)->x, s, base, tl_mpfr_rnd) != 0)
        scm_raise(V_FALSE, "mpfr: invalid numeric string '%s'", s);
    return v;
}

val_t mpfr_make_copy(val_t src, mpfr_prec_t prec) {
    if (!vis_mpfr(src)) scm_raise(V_FALSE, "mpfr-copy: not an mpfr value");
    val_t v = mpfr_make(prec);
    mpfr_set(as_mpfr(v)->x, as_mpfr(src)->x, tl_mpfr_rnd);
    return v;
}

/* Coerce any numeric val_t (fixnum/bignum/rational/flonum/MPFR) to MPFR. */
val_t mpfr_coerce(val_t v, mpfr_prec_t prec) {
    if (prec == 0) prec = mpfr_ctx_prec();
    if (vis_fixnum(v))   return mpfr_make_from_si((long)vunfix(v), prec);
    if (vis_flonum(v))   return mpfr_make_from_d(vfloat(v), prec);
    if (vis_bignum(v))   return mpfr_make_from_z(as_big(v)->z, prec);
    if (vis_rational(v)) return mpfr_make_from_q(as_rat(v)->q, prec);
    if (vis_mpfr(v))     return mpfr_make_copy(v, prec);
    scm_raise(V_FALSE, "mpfr-coerce: not a real number");
}

/* ---------------------------------------------------------------------- */
/* Binary arithmetic                                                      */
/* ---------------------------------------------------------------------- */

#define MPFR_BINOP(NAME, MPFR_FN)                                              \
val_t NAME(val_t a, val_t b) {                                                 \
    mpfr_prec_t p = bin_prec(a, b);                                            \
    val_t av = vis_mpfr(a) ? a : mpfr_coerce(a, p);                            \
    val_t bv = vis_mpfr(b) ? b : mpfr_coerce(b, p);                            \
    val_t r  = mpfr_make(p);                                                   \
    MPFR_FN(as_mpfr(r)->x, as_mpfr(av)->x, as_mpfr(bv)->x, tl_mpfr_rnd);       \
    return r;                                                                  \
}

MPFR_BINOP(mpfr_num_add, mpfr_add)
MPFR_BINOP(mpfr_num_sub, mpfr_sub)
MPFR_BINOP(mpfr_num_mul, mpfr_mul)
MPFR_BINOP(mpfr_num_div, mpfr_div)

val_t mpfr_num_hypot(val_t a, val_t b) {
    mpfr_prec_t p = bin_prec(a, b);
    val_t av = vis_mpfr(a) ? a : mpfr_coerce(a, p);
    val_t bv = vis_mpfr(b) ? b : mpfr_coerce(b, p);
    val_t r  = mpfr_make(p);
    mpfr_hypot(as_mpfr(r)->x, as_mpfr(av)->x, as_mpfr(bv)->x, tl_mpfr_rnd);
    return r;
}

val_t mpfr_num_atan2(val_t y, val_t x) {
    mpfr_prec_t p = bin_prec(y, x);
    val_t yv = vis_mpfr(y) ? y : mpfr_coerce(y, p);
    val_t xv = vis_mpfr(x) ? x : mpfr_coerce(x, p);
    val_t r  = mpfr_make(p);
    mpfr_atan2(as_mpfr(r)->x, as_mpfr(yv)->x, as_mpfr(xv)->x, tl_mpfr_rnd);
    return r;
}

val_t mpfr_num_fma(val_t a, val_t b, val_t c) {
    mpfr_prec_t pa = vis_mpfr(a) ? mpfr_val_prec(a) : 0;
    mpfr_prec_t pb = vis_mpfr(b) ? mpfr_val_prec(b) : 0;
    mpfr_prec_t pc = vis_mpfr(c) ? mpfr_val_prec(c) : 0;
    mpfr_prec_t p = pa > pb ? pa : pb; if (pc > p) p = pc;
    if (tl_mpfr_prec > p) p = tl_mpfr_prec;
    if (p == 0) p = MPFR_DEFAULT_PREC;
    val_t av = vis_mpfr(a) ? a : mpfr_coerce(a, p);
    val_t bv = vis_mpfr(b) ? b : mpfr_coerce(b, p);
    val_t cv = vis_mpfr(c) ? c : mpfr_coerce(c, p);
    val_t r  = mpfr_make(p);
    mpfr_fma(as_mpfr(r)->x, as_mpfr(av)->x, as_mpfr(bv)->x, as_mpfr(cv)->x, tl_mpfr_rnd);
    return r;
}

val_t mpfr_num_expt(val_t base, val_t exp) {
    mpfr_prec_t p = bin_prec(base, exp);
    val_t bv = vis_mpfr(base) ? base : mpfr_coerce(base, p);
    val_t ev = vis_mpfr(exp)  ? exp  : mpfr_coerce(exp,  p);
    val_t r  = mpfr_make(p);
    mpfr_pow(as_mpfr(r)->x, as_mpfr(bv)->x, as_mpfr(ev)->x, tl_mpfr_rnd);
    return r;
}

/* ---------------------------------------------------------------------- */
/* Unary arithmetic                                                       */
/* ---------------------------------------------------------------------- */

#define MPFR_UNOP(NAME, MPFR_FN)                                               \
val_t NAME(val_t a) {                                                          \
    mpfr_prec_t p = un_prec(a);                                                \
    val_t av = vis_mpfr(a) ? a : mpfr_coerce(a, p);                            \
    val_t r  = mpfr_make(p);                                                   \
    MPFR_FN(as_mpfr(r)->x, as_mpfr(av)->x, tl_mpfr_rnd);                       \
    return r;                                                                  \
}

MPFR_UNOP(mpfr_num_neg,    mpfr_neg)
MPFR_UNOP(mpfr_num_abs,    mpfr_abs)
MPFR_UNOP(mpfr_num_sqrt,   mpfr_sqrt)
MPFR_UNOP(mpfr_num_exp,    mpfr_exp)
MPFR_UNOP(mpfr_num_log,    mpfr_log)
MPFR_UNOP(mpfr_num_log2,   mpfr_log2)
MPFR_UNOP(mpfr_num_log10,  mpfr_log10)
MPFR_UNOP(mpfr_num_sin,    mpfr_sin)
MPFR_UNOP(mpfr_num_cos,    mpfr_cos)
MPFR_UNOP(mpfr_num_tan,    mpfr_tan)
MPFR_UNOP(mpfr_num_asin,   mpfr_asin)
MPFR_UNOP(mpfr_num_acos,   mpfr_acos)
MPFR_UNOP(mpfr_num_atan,   mpfr_atan)
MPFR_UNOP(mpfr_num_sinh,   mpfr_sinh)
MPFR_UNOP(mpfr_num_cosh,   mpfr_cosh)
MPFR_UNOP(mpfr_num_tanh,   mpfr_tanh)
MPFR_UNOP(mpfr_num_asinh,  mpfr_asinh)
MPFR_UNOP(mpfr_num_acosh,  mpfr_acosh)
MPFR_UNOP(mpfr_num_atanh,  mpfr_atanh)
MPFR_UNOP(mpfr_num_gamma,  mpfr_gamma)
MPFR_UNOP(mpfr_num_zeta,   mpfr_zeta)
MPFR_UNOP(mpfr_num_erf,    mpfr_erf)
MPFR_UNOP(mpfr_num_erfc,   mpfr_erfc)
MPFR_UNOP(mpfr_num_j0,     mpfr_j0)
MPFR_UNOP(mpfr_num_j1,     mpfr_j1)

/* lgamma takes a sign-of-gamma int* parameter; ignore. */
val_t mpfr_num_lgamma(val_t a) {
    mpfr_prec_t p = un_prec(a);
    val_t av = vis_mpfr(a) ? a : mpfr_coerce(a, p);
    val_t r  = mpfr_make(p);
    int sign;
    mpfr_lgamma(as_mpfr(r)->x, &sign, as_mpfr(av)->x, tl_mpfr_rnd);
    return r;
}

/* Rounding: result is still MPFR (to preserve precision). */
#define MPFR_ROUND(NAME, MPFR_FN)                                              \
val_t NAME(val_t a) {                                                          \
    mpfr_prec_t p = un_prec(a);                                                \
    val_t av = vis_mpfr(a) ? a : mpfr_coerce(a, p);                            \
    val_t r  = mpfr_make(p);                                                   \
    MPFR_FN(as_mpfr(r)->x, as_mpfr(av)->x);                                    \
    return r;                                                                  \
}
MPFR_ROUND(mpfr_num_floor,    mpfr_floor)
MPFR_ROUND(mpfr_num_ceiling,  mpfr_ceil)
MPFR_ROUND(mpfr_num_truncate, mpfr_trunc)

val_t mpfr_num_round(val_t a) {
    mpfr_prec_t p = un_prec(a);
    val_t av = vis_mpfr(a) ? a : mpfr_coerce(a, p);
    val_t r  = mpfr_make(p);
    /* mpfr_rint with RNDN = round half to even, matching curry's banker's rounding. */
    mpfr_rint(as_mpfr(r)->x, as_mpfr(av)->x, MPFR_RNDN);
    return r;
}

/* ---------------------------------------------------------------------- */
/* Constants                                                              */
/* ---------------------------------------------------------------------- */

val_t mpfr_c_pi(mpfr_prec_t prec) {
    if (prec == 0) prec = mpfr_ctx_prec();
    val_t r = mpfr_make(prec);
    mpfr_const_pi(as_mpfr(r)->x, tl_mpfr_rnd);
    return r;
}

val_t mpfr_c_e(mpfr_prec_t prec) {
    if (prec == 0) prec = mpfr_ctx_prec();
    /* e = exp(1) — no dedicated constant in libmpfr. */
    val_t r = mpfr_make(prec);
    mpfr_t one; mpfr_init2(one, prec); mpfr_set_ui(one, 1, tl_mpfr_rnd);
    mpfr_exp(as_mpfr(r)->x, one, tl_mpfr_rnd);
    mpfr_clear(one);
    return r;
}

val_t mpfr_c_phi(mpfr_prec_t prec) {
    if (prec == 0) prec = mpfr_ctx_prec();
    /* φ = (1 + √5) / 2 */
    val_t r = mpfr_make(prec);
    mpfr_t five, sq; mpfr_init2(five, prec); mpfr_init2(sq, prec);
    mpfr_set_ui(five, 5, tl_mpfr_rnd);
    mpfr_sqrt(sq, five, tl_mpfr_rnd);
    mpfr_add_ui(as_mpfr(r)->x, sq, 1, tl_mpfr_rnd);
    mpfr_div_ui(as_mpfr(r)->x, as_mpfr(r)->x, 2, tl_mpfr_rnd);
    mpfr_clear(five); mpfr_clear(sq);
    return r;
}

val_t mpfr_c_log2(mpfr_prec_t prec) {
    if (prec == 0) prec = mpfr_ctx_prec();
    val_t r = mpfr_make(prec);
    mpfr_const_log2(as_mpfr(r)->x, tl_mpfr_rnd);
    return r;
}

val_t mpfr_c_euler(mpfr_prec_t prec) {
    if (prec == 0) prec = mpfr_ctx_prec();
    val_t r = mpfr_make(prec);
    mpfr_const_euler(as_mpfr(r)->x, tl_mpfr_rnd);
    return r;
}

val_t mpfr_c_catalan(mpfr_prec_t prec) {
    if (prec == 0) prec = mpfr_ctx_prec();
    val_t r = mpfr_make(prec);
    mpfr_const_catalan(as_mpfr(r)->x, tl_mpfr_rnd);
    return r;
}

val_t mpfr_c_apery(mpfr_prec_t prec) {
    if (prec == 0) prec = mpfr_ctx_prec();
    /* Apéry's constant ζ(3). */
    val_t r = mpfr_make(prec);
    mpfr_t three; mpfr_init2(three, prec); mpfr_set_ui(three, 3, tl_mpfr_rnd);
    mpfr_zeta(as_mpfr(r)->x, three, tl_mpfr_rnd);
    mpfr_clear(three);
    return r;
}

/* ---------------------------------------------------------------------- */
/* Comparison                                                             */
/* ---------------------------------------------------------------------- */

int mpfr_num_cmp(val_t a, val_t b) {
    mpfr_prec_t p = bin_prec(a, b);
    val_t av = vis_mpfr(a) ? a : mpfr_coerce(a, p);
    val_t bv = vis_mpfr(b) ? b : mpfr_coerce(b, p);
    int c = mpfr_cmp(as_mpfr(av)->x, as_mpfr(bv)->x);
    return c < 0 ? -1 : c > 0 ? 1 : 0;
}

bool mpfr_num_eq(val_t a, val_t b) { return mpfr_num_cmp(a, b) == 0; }

/* ---------------------------------------------------------------------- */
/* Predicates                                                             */
/* ---------------------------------------------------------------------- */

bool mpfr_is_nan(val_t v)      { return vis_mpfr(v) && mpfr_nan_p(as_mpfr(v)->x);      }
bool mpfr_is_inf(val_t v)      { return vis_mpfr(v) && mpfr_inf_p(as_mpfr(v)->x);      }
bool mpfr_is_zero(val_t v)     { return vis_mpfr(v) && mpfr_zero_p(as_mpfr(v)->x);     }
bool mpfr_is_positive(val_t v) { return vis_mpfr(v) && mpfr_sgn(as_mpfr(v)->x) > 0;    }
bool mpfr_is_negative(val_t v) { return vis_mpfr(v) && mpfr_sgn(as_mpfr(v)->x) < 0;    }
bool mpfr_is_integer(val_t v)  { return vis_mpfr(v) && mpfr_integer_p(as_mpfr(v)->x);  }

/* ---------------------------------------------------------------------- */
/* Conversion                                                             */
/* ---------------------------------------------------------------------- */

double mpfr_to_double(val_t v) {
    if (!vis_mpfr(v)) scm_raise(V_FALSE, "mpfr->double: not an mpfr");
    return mpfr_get_d(as_mpfr(v)->x, tl_mpfr_rnd);
}

val_t mpfr_to_exact(val_t v) {
    if (!vis_mpfr(v)) scm_raise(V_FALSE, "mpfr->exact: not an mpfr");
    if (mpfr_nan_p(as_mpfr(v)->x) || mpfr_inf_p(as_mpfr(v)->x))
        scm_raise(V_FALSE, "mpfr->exact: not a finite value");
    /* Use mpfr_get_q (added in mpfr 4.2) if available; otherwise build via
     * the integer mantissa and the binary exponent returned by mpfr_get_z_2exp. */
    mpz_t mantissa; mpz_init(mantissa);
    mpfr_exp_t exp2 = mpfr_get_z_2exp(mantissa, as_mpfr(v)->x);
    mpq_t q; mpq_init(q);
    if (exp2 >= 0) {
        mpz_mul_2exp(mantissa, mantissa, (mp_bitcnt_t)exp2);
        mpq_set_z(q, mantissa);
    } else {
        mpq_set_z(q, mantissa);
        mpz_t den; mpz_init(den);
        mpz_set_ui(den, 1);
        mpz_mul_2exp(den, den, (mp_bitcnt_t)(-exp2));
        mpq_set_den(q, den);
        mpz_clear(den);
        mpq_canonicalize(q);
    }
    val_t r = num_make_rational(make_big_from_mpz(mpq_numref(q)),
                                make_big_from_mpz(mpq_denref(q)));
    mpz_clear(mantissa); mpq_clear(q);
    return r;
}

val_t mpfr_to_string(val_t v, int digits, int base) {
    if (!vis_mpfr(v)) scm_raise(V_FALSE, "mpfr->string: not an mpfr");
    if (base < 2 || base > 62) base = 10;
    /* digits=0 → enough digits for round-trip (mpfr_get_str with n=0). */
    mpfr_exp_t exp;
    char *raw = mpfr_get_str(NULL, &exp, base, (size_t)digits, as_mpfr(v)->x, tl_mpfr_rnd);
    if (!raw) scm_raise(V_FALSE, "mpfr->string: conversion failed");
    /* Build a decimal-with-exponent-or-point representation: "d.ddde±N" */
    size_t rlen = strlen(raw);
    char *signptr = raw;
    int sign = 0;
    if (raw[0] == '-') { sign = 1; signptr = raw + 1; rlen -= 1; }
    else if (raw[0] == '+') { signptr = raw + 1; rlen -= 1; }

    /* Special cases: "@NaN@", "@Inf@", "Inf", "NaN", "0" */
    if (strncmp(signptr, "@NaN@", 5) == 0 || strncmp(signptr, "NaN", 3) == 0) {
        mpfr_free_str(raw);
        const char *s = "+nan.0";
        size_t L = strlen(s);
        String *str = (String *)gc_alloc_atomic(sizeof(String) + L + 1);
        str->hdr.type=T_STRING; str->hdr.flags=0; str->len=(uint32_t)L;
        memcpy(str->data, s, L+1); return vptr(str);
    }
    if (strncmp(signptr, "@Inf@", 5) == 0 || strncmp(signptr, "Inf", 3) == 0) {
        mpfr_free_str(raw);
        const char *s = sign ? "-inf.0" : "+inf.0";
        size_t L = strlen(s);
        String *str = (String *)gc_alloc_atomic(sizeof(String) + L + 1);
        str->hdr.type=T_STRING; str->hdr.flags=0; str->len=(uint32_t)L;
        memcpy(str->data, s, L+1); return vptr(str);
    }

    /* Build textual form "[-]d.dddde±N" with one digit before the point. */
    size_t outcap = rlen + 32;
    char *out = (char *)malloc(outcap);
    size_t pos = 0;
    if (sign) out[pos++] = '-';
    out[pos++] = signptr[0];
    if (rlen > 1) {
        out[pos++] = '.';
        memcpy(out + pos, signptr + 1, rlen - 1);
        pos += rlen - 1;
    }
    /* Decimal-scientific exponent is (exp - 1) because we shifted the point. */
    long e = (long)exp - 1;
    pos += (size_t)snprintf(out + pos, outcap - pos, "e%ld", e);
    mpfr_free_str(raw);

    uint32_t len = (uint32_t)pos;
    String *str = (String *)gc_alloc_atomic(sizeof(String) + len + 1);
    str->hdr.type=T_STRING; str->hdr.flags=0; str->len=len;
    memcpy(str->data, out, len); str->data[len] = '\0';
    free(out);
    return vptr(str);
}

mpfr_prec_t mpfr_precision(val_t v) {
    if (!vis_mpfr(v)) scm_raise(V_FALSE, "mpfr-precision: not an mpfr");
    return mpfr_get_prec(as_mpfr(v)->x);
}

val_t mpfr_set_precision(val_t v, mpfr_prec_t prec) {
    if (!vis_mpfr(v)) scm_raise(V_FALSE, "mpfr-set-precision: not an mpfr");
    val_t r = mpfr_make(prec);
    mpfr_set(as_mpfr(r)->x, as_mpfr(v)->x, tl_mpfr_rnd);
    return r;
}

/* ---------------------------------------------------------------------- */
/* Interval arithmetic                                                    */
/*                                                                        */
/* Endpoints are stored as MPFR with directed rounding (down on lo, up on */
/* hi).  Coercion from arbitrary reals goes through mpfr_coerce.           */
/* ---------------------------------------------------------------------- */

/* Round `v` to MPFR with the given rounding mode at ctx precision. */
static val_t coerce_directed(val_t v, mpfr_rnd_t rnd) {
    mpfr_prec_t p = vis_mpfr(v) ? mpfr_val_prec(v) : 0;
    if (tl_mpfr_prec > p) p = tl_mpfr_prec;
    if (p == 0) p = MPFR_DEFAULT_PREC;
    val_t r = mpfr_make(p);
    if      (vis_fixnum(v))   mpfr_set_si(as_mpfr(r)->x, (long)vunfix(v), rnd);
    else if (vis_flonum(v))   mpfr_set_d (as_mpfr(r)->x, vfloat(v), rnd);
    else if (vis_bignum(v))   mpfr_set_z (as_mpfr(r)->x, as_big(v)->z, rnd);
    else if (vis_rational(v)) mpfr_set_q (as_mpfr(r)->x, as_rat(v)->q, rnd);
    else if (vis_mpfr(v))     mpfr_set  (as_mpfr(r)->x, as_mpfr(v)->x, rnd);
    else scm_raise(V_FALSE, "interval: endpoint must be a real number");
    return r;
}

val_t interval_make(val_t lo, val_t hi) {
    Interval *iv = CURRY_NEW(Interval);
    iv->hdr.type  = T_INTERVAL;
    iv->hdr.flags = 0;
    iv->lo = coerce_directed(lo, MPFR_RNDD);
    iv->hi = coerce_directed(hi, MPFR_RNDU);
    return vptr(iv);
}

val_t interval_from_number(val_t v) {
    return interval_make(v, v);
}

static void unpack_interval(val_t v, val_t *lo, val_t *hi) {
    if (vis_ival(v)) { *lo = as_ival(v)->lo; *hi = as_ival(v)->hi; return; }
    if (vis_number(v)) { *lo = v; *hi = v; return; }
    scm_raise(V_FALSE, "interval op: argument is not an interval or number");
}

val_t interval_add(val_t a, val_t b) {
    val_t alo, ahi, blo, bhi;
    unpack_interval(a, &alo, &ahi);
    unpack_interval(b, &blo, &bhi);
    /* lower bound uses RNDD; upper bound uses RNDU. */
    mpfr_prec_t p = bin_prec(alo, blo);
    val_t lo = mpfr_make(p), hi = mpfr_make(p);
    val_t alo_m = coerce_directed(alo, MPFR_RNDD);
    val_t blo_m = coerce_directed(blo, MPFR_RNDD);
    val_t ahi_m = coerce_directed(ahi, MPFR_RNDU);
    val_t bhi_m = coerce_directed(bhi, MPFR_RNDU);
    mpfr_add(as_mpfr(lo)->x, as_mpfr(alo_m)->x, as_mpfr(blo_m)->x, MPFR_RNDD);
    mpfr_add(as_mpfr(hi)->x, as_mpfr(ahi_m)->x, as_mpfr(bhi_m)->x, MPFR_RNDU);
    return interval_make(lo, hi);
}

val_t interval_sub(val_t a, val_t b) {
    val_t alo, ahi, blo, bhi;
    unpack_interval(a, &alo, &ahi);
    unpack_interval(b, &blo, &bhi);
    mpfr_prec_t p = bin_prec(alo, blo);
    val_t lo = mpfr_make(p), hi = mpfr_make(p);
    val_t alo_m = coerce_directed(alo, MPFR_RNDD);
    val_t bhi_m = coerce_directed(bhi, MPFR_RNDU);
    val_t ahi_m = coerce_directed(ahi, MPFR_RNDU);
    val_t blo_m = coerce_directed(blo, MPFR_RNDD);
    /* [a_lo - b_hi, a_hi - b_lo] */
    mpfr_sub(as_mpfr(lo)->x, as_mpfr(alo_m)->x, as_mpfr(bhi_m)->x, MPFR_RNDD);
    mpfr_sub(as_mpfr(hi)->x, as_mpfr(ahi_m)->x, as_mpfr(blo_m)->x, MPFR_RNDU);
    return interval_make(lo, hi);
}

/* min/max for 4 mpfr values, with directed rounding. */
static void mpfr_min4(mpfr_t out, mpfr_t a, mpfr_t b, mpfr_t c, mpfr_t d) {
    mpfr_set(out, a, MPFR_RNDD);
    if (mpfr_cmp(b, out) < 0) mpfr_set(out, b, MPFR_RNDD);
    if (mpfr_cmp(c, out) < 0) mpfr_set(out, c, MPFR_RNDD);
    if (mpfr_cmp(d, out) < 0) mpfr_set(out, d, MPFR_RNDD);
}
static void mpfr_max4(mpfr_t out, mpfr_t a, mpfr_t b, mpfr_t c, mpfr_t d) {
    mpfr_set(out, a, MPFR_RNDU);
    if (mpfr_cmp(b, out) > 0) mpfr_set(out, b, MPFR_RNDU);
    if (mpfr_cmp(c, out) > 0) mpfr_set(out, c, MPFR_RNDU);
    if (mpfr_cmp(d, out) > 0) mpfr_set(out, d, MPFR_RNDU);
}

val_t interval_mul(val_t a, val_t b) {
    val_t alo, ahi, blo, bhi;
    unpack_interval(a, &alo, &ahi);
    unpack_interval(b, &blo, &bhi);
    mpfr_prec_t p = bin_prec(alo, blo);
    /* Compute the four corner products with the worst-case rounding direction
     * — RNDD for the candidate min, RNDU for the candidate max — then take
     * min and max.  Doing it in two passes keeps the bounds certified. */
    val_t aloD = coerce_directed(alo, MPFR_RNDD);
    val_t ahiD = coerce_directed(ahi, MPFR_RNDD);
    val_t bloD = coerce_directed(blo, MPFR_RNDD);
    val_t bhiD = coerce_directed(bhi, MPFR_RNDD);
    val_t aloU = coerce_directed(alo, MPFR_RNDU);
    val_t ahiU = coerce_directed(ahi, MPFR_RNDU);
    val_t bloU = coerce_directed(blo, MPFR_RNDU);
    val_t bhiU = coerce_directed(bhi, MPFR_RNDU);

    mpfr_t p1, p2, p3, p4;
    mpfr_init2(p1, p); mpfr_init2(p2, p); mpfr_init2(p3, p); mpfr_init2(p4, p);
    mpfr_mul(p1, as_mpfr(aloD)->x, as_mpfr(bloD)->x, MPFR_RNDD);
    mpfr_mul(p2, as_mpfr(aloD)->x, as_mpfr(bhiD)->x, MPFR_RNDD);
    mpfr_mul(p3, as_mpfr(ahiD)->x, as_mpfr(bloD)->x, MPFR_RNDD);
    mpfr_mul(p4, as_mpfr(ahiD)->x, as_mpfr(bhiD)->x, MPFR_RNDD);
    val_t lo = mpfr_make(p);
    mpfr_min4(as_mpfr(lo)->x, p1, p2, p3, p4);

    mpfr_mul(p1, as_mpfr(aloU)->x, as_mpfr(bloU)->x, MPFR_RNDU);
    mpfr_mul(p2, as_mpfr(aloU)->x, as_mpfr(bhiU)->x, MPFR_RNDU);
    mpfr_mul(p3, as_mpfr(ahiU)->x, as_mpfr(bloU)->x, MPFR_RNDU);
    mpfr_mul(p4, as_mpfr(ahiU)->x, as_mpfr(bhiU)->x, MPFR_RNDU);
    val_t hi = mpfr_make(p);
    mpfr_max4(as_mpfr(hi)->x, p1, p2, p3, p4);

    mpfr_clear(p1); mpfr_clear(p2); mpfr_clear(p3); mpfr_clear(p4);
    return interval_make(lo, hi);
}

val_t interval_div(val_t a, val_t b) {
    val_t alo, ahi, blo, bhi;
    unpack_interval(a, &alo, &ahi);
    unpack_interval(b, &blo, &bhi);
    /* Reject any interval that crosses zero — division would be unbounded. */
    val_t bloM = coerce_directed(blo, MPFR_RNDD);
    val_t bhiM = coerce_directed(bhi, MPFR_RNDU);
    int losgn = mpfr_sgn(as_mpfr(bloM)->x);
    int hisgn = mpfr_sgn(as_mpfr(bhiM)->x);
    if (losgn <= 0 && hisgn >= 0)
        scm_raise(V_FALSE, "interval /: divisor interval contains zero");

    mpfr_prec_t p = bin_prec(alo, blo);
    mpfr_t bloI, bhiI;
    mpfr_init2(bloI, p); mpfr_init2(bhiI, p);
    /* 1/b is decreasing on each sign-strict interval: reciprocate with
     * opposite rounding direction. */
    mpfr_ui_div(bloI, 1, as_mpfr(bhiM)->x, MPFR_RNDD);  /* lower of 1/b */
    mpfr_ui_div(bhiI, 1, as_mpfr(bloM)->x, MPFR_RNDU);  /* upper of 1/b */

    /* Multiply [a] by [1/b]. */
    val_t recip_lo = mpfr_make(p), recip_hi = mpfr_make(p);
    mpfr_set(as_mpfr(recip_lo)->x, bloI, MPFR_RNDD);
    mpfr_set(as_mpfr(recip_hi)->x, bhiI, MPFR_RNDU);
    mpfr_clear(bloI); mpfr_clear(bhiI);
    val_t one_over_b = interval_make(recip_lo, recip_hi);
    return interval_mul(a, one_over_b);
}

/* ---------------------------------------------------------------------- */
/* Lifecycle                                                              */
/* ---------------------------------------------------------------------- */

void mpfr_num_init(void) {
    /* libmpfr is stateless beyond a few thread-local caches; nothing to do. */
}

#endif /* BUILD_MPFR */
