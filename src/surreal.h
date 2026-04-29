#ifndef CURRY_SURREAL_H
#define CURRY_SURREAL_H

/*
 * Surreal numbers — Hahn-series representation.
 *
 * A surreal is stored as a finite list of (exponent, coefficient) pairs
 * sorted in DESCENDING order of exponent.  Exponents and coefficients are
 * val_t values from the numeric tower (typically fixnum or rational).
 * All coefficients are nonzero.
 *
 * The basis element ω (omega) has exponent 1.
 * The infinitesimal ε (epsilon) = 1/ω has exponent -1.
 *
 *   3 + 2ω         → [(1, 2), (0, 3)]
 *   ε              → [(-1, 1)]
 *   ω² - 3ε        → [(2, 1), (-1, -3)]
 *
 * Every real number embeds as a single term at exponent 0.
 *
 * Arithmetic is exact when coefficients are exact (rational).  When
 * coefficients are flonums the result is approximate.
 *
 * The forward-mode automatic differentiation identity follows for free:
 *   f(x + ε) = f(x) + f'(x)·ε + O(ε²)
 * Extract the ε coefficient with surreal-epsilon-part.
 */

#include "value.h"
#include <stdbool.h>

void surreal_init(void);

/* Pre-initialised constants (valid after surreal_init) */
extern val_t SUR_ZERO, SUR_ONE, SUR_NEG_ONE, SUR_OMEGA, SUR_EPSILON;

/* ---- Constructors ---- */
val_t sur_from_val(val_t v);               /* embed a real number → surreal */
val_t sur_make_term(val_t exp, val_t coeff); /* single term c·ωᵉ */
val_t sur_make(int n, val_t *exps, val_t *coeffs); /* sorted arrays → surreal */

/* ---- Arithmetic ---- */
val_t sur_add(val_t a, val_t b);
val_t sur_sub(val_t a, val_t b);
val_t sur_mul(val_t a, val_t b);
val_t sur_div(val_t a, val_t b);
val_t sur_neg(val_t a);
val_t sur_abs(val_t a);
val_t sur_expt(val_t base, val_t exp);     /* exp: any val_t number */

/* ---- Comparison (total order) ---- */
int  sur_compare(val_t a, val_t b);        /* -1 / 0 / +1 */
bool sur_is_zero(val_t a);
bool sur_is_positive(val_t a);
bool sur_is_negative(val_t a);

/* ---- Classification ---- */
bool sur_finite_p(val_t a);               /* highest exponent ≤ 0 */
bool sur_infinite_p(val_t a);             /* highest exponent > 0  */
bool sur_infinitesimal_p(val_t a);        /* nonzero, all exponents < 0 */

/* ---- Accessors ---- */
int   sur_nterms(val_t a);
val_t sur_term_exp(val_t a, int i);        /* exponent of i-th term */
val_t sur_term_coeff(val_t a, int i);      /* coefficient of i-th term */
val_t sur_real_part(val_t a);             /* coefficient of ω⁰ */
val_t sur_omega_part(val_t a);            /* coefficient of ω¹ */
val_t sur_epsilon_part(val_t a);          /* coefficient of ω⁻¹ */

/* ---- Conversion ---- */
double sur_to_double(val_t a);             /* standard part as double */
val_t  sur_to_val(val_t a);               /* extract to simpler type if possible */

/* ---- Birthday ---- */
val_t sur_birthday(val_t a);              /* Conway birthday (ordinal, as surreal) */

/* ---- Display ---- */
void sur_write(val_t a, val_t port);

#endif /* CURRY_SURREAL_H */
