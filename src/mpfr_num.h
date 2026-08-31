#ifndef CURRY_MPFR_NUM_H
#define CURRY_MPFR_NUM_H

#ifdef BUILD_MPFR
#include <mpfr.h>
#include "value.h"
#include "object.h"
#include <stdbool.h>

/* Thread-local precision context.  0 = not active (use double). */
extern CURRY_THREAD_LOCAL mpfr_prec_t tl_mpfr_prec;
extern CURRY_THREAD_LOCAL mpfr_rnd_t  tl_mpfr_rnd;

#define MPFR_DEFAULT_PREC 128

static inline mpfr_prec_t mpfr_ctx_prec(void) {
    return tl_mpfr_prec > 0 ? tl_mpfr_prec : MPFR_DEFAULT_PREC;
}
static inline mpfr_rnd_t mpfr_ctx_rnd(void) { return tl_mpfr_rnd; }

/* Return the precision of an MPFR val_t */
static inline mpfr_prec_t mpfr_val_prec(val_t v) {
    return mpfr_get_prec(as_mpfr(v)->x);
}

/* Constructors */
val_t mpfr_make(mpfr_prec_t prec);
val_t mpfr_make_from_d(double d, mpfr_prec_t prec);
val_t mpfr_make_from_z(mpz_t z, mpfr_prec_t prec);
val_t mpfr_make_from_q(mpq_t q, mpfr_prec_t prec);
val_t mpfr_make_from_si(long n, mpfr_prec_t prec);
val_t mpfr_make_from_str(const char *s, int base, mpfr_prec_t prec);
val_t mpfr_make_copy(val_t src, mpfr_prec_t prec);

/* Coerce any numeric val_t to MPFR at given precision */
val_t mpfr_coerce(val_t v, mpfr_prec_t prec);

/* Arithmetic (result precision = max(prec(a),prec(b)) or ctx_prec if > that) */
val_t mpfr_num_add(val_t a, val_t b);
val_t mpfr_num_sub(val_t a, val_t b);
val_t mpfr_num_mul(val_t a, val_t b);
val_t mpfr_num_div(val_t a, val_t b);
val_t mpfr_num_neg(val_t a);
val_t mpfr_num_abs(val_t a);
val_t mpfr_num_sqrt(val_t a);
val_t mpfr_num_expt(val_t base, val_t exp);
val_t mpfr_num_exp(val_t a);
val_t mpfr_num_log(val_t a);
val_t mpfr_num_log2(val_t a);
val_t mpfr_num_log10(val_t a);
val_t mpfr_num_sin(val_t a);
val_t mpfr_num_cos(val_t a);
val_t mpfr_num_tan(val_t a);
val_t mpfr_num_asin(val_t a);
val_t mpfr_num_acos(val_t a);
val_t mpfr_num_atan(val_t a);
val_t mpfr_num_atan2(val_t y, val_t x);
val_t mpfr_num_sinh(val_t a);
val_t mpfr_num_cosh(val_t a);
val_t mpfr_num_tanh(val_t a);
val_t mpfr_num_asinh(val_t a);
val_t mpfr_num_acosh(val_t a);
val_t mpfr_num_atanh(val_t a);
val_t mpfr_num_floor(val_t a);
val_t mpfr_num_ceiling(val_t a);
val_t mpfr_num_truncate(val_t a);
val_t mpfr_num_round(val_t a);
val_t mpfr_num_gamma(val_t a);
val_t mpfr_num_lgamma(val_t a);
val_t mpfr_num_zeta(val_t a);
val_t mpfr_num_erf(val_t a);
val_t mpfr_num_erfc(val_t a);
val_t mpfr_num_j0(val_t a);
val_t mpfr_num_j1(val_t a);
val_t mpfr_num_hypot(val_t a, val_t b);
val_t mpfr_num_fma(val_t a, val_t b, val_t c);  /* a*b + c */

/* Constants at given precision (or ctx_prec if prec==0).
 * Prefixed mpfr_c_* to avoid colliding with libmpfr's mpfr_const_pi etc. */
val_t mpfr_c_pi(mpfr_prec_t prec);
val_t mpfr_c_e(mpfr_prec_t prec);
val_t mpfr_c_phi(mpfr_prec_t prec);   /* golden ratio */
val_t mpfr_c_log2(mpfr_prec_t prec);
val_t mpfr_c_euler(mpfr_prec_t prec); /* Euler-Mascheroni γ */
val_t mpfr_c_catalan(mpfr_prec_t prec);
val_t mpfr_c_apery(mpfr_prec_t prec); /* ζ(3) */

/* Comparison */
int  mpfr_num_cmp(val_t a, val_t b);
bool mpfr_num_eq(val_t a, val_t b);

/* Predicates */
bool mpfr_is_nan(val_t v);
bool mpfr_is_inf(val_t v);
bool mpfr_is_zero(val_t v);
bool mpfr_is_positive(val_t v);
bool mpfr_is_negative(val_t v);
bool mpfr_is_integer(val_t v);

/* Conversion */
double mpfr_to_double(val_t v);
val_t  mpfr_to_exact(val_t v);   /* → exact rational */
val_t  mpfr_to_string(val_t v, int digits, int base); /* digits=0 → enough for round-trip */

/* Precision/rounding query */
mpfr_prec_t mpfr_precision(val_t v);
val_t mpfr_set_precision(val_t v, mpfr_prec_t prec); /* re-round to new prec */

/* Interval arithmetic */
val_t interval_make(val_t lo, val_t hi);
val_t interval_add(val_t a, val_t b);
val_t interval_sub(val_t a, val_t b);
val_t interval_mul(val_t a, val_t b);
val_t interval_div(val_t a, val_t b);
val_t interval_from_number(val_t v);  /* point interval [v,v] */

void mpfr_num_init(void);

#endif /* BUILD_MPFR */
#endif /* CURRY_MPFR_NUM_H */
