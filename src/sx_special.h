#pragma once
#include "value.h"

/* ---- Orthogonal polynomial families ----
   For integer n and symbolic x, returns the exact polynomial expression.
   For numeric x, evaluates numerically. */

val_t sx_legendre(val_t n, val_t x);           /* P_n(x)        */
val_t sx_assoc_legendre(val_t l, val_t m, val_t x); /* P_l^m(x) */
val_t sx_hermite(val_t n, val_t x);            /* H_n(x) — physicists' */
val_t sx_hermite_prob(val_t n, val_t x);       /* He_n(x) — probabilists' */
val_t sx_chebyshev_t(val_t n, val_t x);        /* T_n(x) — first kind */
val_t sx_chebyshev_u(val_t n, val_t x);        /* U_n(x) — second kind */
val_t sx_laguerre(val_t n, val_t x);           /* L_n(x) */
val_t sx_assoc_laguerre(val_t n, val_t k, val_t x); /* L_n^k(x) */

/* ---- Spherical harmonics ---- */
/* Y_l^m(theta, phi) — returns symbolic expression */
val_t sx_spherical_harmonic(val_t l, val_t m, val_t theta, val_t phi);

/* ---- Gamma and related ---- */
val_t sx_gamma(val_t z);           /* Γ(z) — exact for integers/half-integers */
val_t sx_log_gamma(val_t z);       /* log Γ(z) */
val_t sx_digamma(val_t z);         /* ψ(z) = Γ'(z)/Γ(z) */
val_t sx_beta(val_t a, val_t b);   /* B(a,b) = Γ(a)Γ(b)/Γ(a+b) */

/* ---- Error function ---- */
val_t sx_erf(val_t x);             /* erf(x) — symbolic-aware */
val_t sx_erfc(val_t x);            /* erfc(x) = 1 - erf(x) */

/* ---- Bessel functions ---- */
val_t sx_bessel_j(val_t n, val_t x); /* J_n(x) — first kind */
val_t sx_bessel_y(val_t n, val_t x); /* Y_n(x) — second kind */
val_t sx_bessel_i(val_t n, val_t x); /* I_n(x) — modified first kind */
val_t sx_bessel_k(val_t n, val_t x); /* K_n(x) — modified second kind */

/* ---- Elliptic integrals ---- */
val_t sx_elliptic_k(val_t k);         /* K(k) — complete first kind */
val_t sx_elliptic_e(val_t k);         /* E(k) — complete second kind */
val_t sx_elliptic_f(val_t phi, val_t k); /* F(φ,k) — incomplete first kind */
val_t sx_elliptic_pi(val_t n, val_t k);  /* Π(n,k) — complete third kind */

/* ---- Laurent and Puiseux series ---- */

/* Laurent series of expr around point, terms from x^low_order to x^n.
   low_order is computed automatically (most negative power). */
val_t sx_laurent(val_t expr, val_t var, val_t point, int n);

/* Puiseux series (fractional powers) of expr around point to order n.
   denom is the denominator of the smallest rational exponent expected. */
val_t sx_puiseux(val_t expr, val_t var, val_t point, int n, int denom);
