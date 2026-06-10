#pragma once
#include "value.h"

void sx_poly_init(void);

/* ---- Univariate polynomial operations ----
   All functions take/return symbolic expressions (SymExpr trees).
   'var' must be a T_SYMVAR. */

/* GCD of polynomials p and q in var (result is primitive, monic over ℚ) */
val_t sx_poly_gcd(val_t p, val_t q, val_t var);

/* Resultant of p and q w.r.t. var */
val_t sx_poly_resultant(val_t p, val_t q, val_t var);

/* Squarefree factorisation: returns Scheme list of (factor . multiplicity) */
val_t sx_poly_squarefree(val_t p, val_t var);

/* Full factorisation over ℚ: returns list of (factor . multiplicity).
   Constant pre-factor (if any) is prepended as (k . 1). */
val_t sx_poly_factor(val_t p, val_t var);

/* Partial fraction decomposition of num/den w.r.t. var.
   Returns a symbolic expression: sum of partial fraction terms. */
val_t sx_partial_fractions(val_t num, val_t den, val_t var);

/* ---- Equation solving ---- */

/* Solve expr = 0 for var.  Returns Scheme list of solutions (exact where
   possible).  Returns #f if unable to solve. */
val_t sx_solve(val_t expr, val_t var);

/* Solve a linear system of equations.
   eqs  — Scheme list of symbolic expressions (each = 0 implicitly)
   vars — Scheme list of sym-vars to solve for
   Returns an alist ((var . solution) ...) or #f if no unique solution. */
val_t sx_solve_system(val_t eqs, val_t vars);

/* ---- Groebner basis ----
   polys — Scheme list of polynomials (symbolic expressions = 0 implicitly)
   vars  — Scheme list of sym-vars (lex ordering: first var is highest)
   Returns a reduced Groebner basis as a Scheme list of polynomials. */
val_t sx_groebner(val_t polys, val_t vars);
