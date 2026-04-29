#ifndef CURRY_MULTIVEC_H
#define CURRY_MULTIVEC_H

/*
 * Multivectors: elements of the Clifford algebra Cl(p,q,r).
 *
 * Metric signature:  p basis vectors square to +1
 *                    q basis vectors square to -1
 *                    r basis vectors square to 0  (null/degenerate)
 *
 * A multivector has 2^n components (n = p+q+r), one per basis blade.
 * Blades are indexed by bitmaps: bit k set means basis vector e_{k+1} is present.
 *
 * Useful algebras:
 *   Cl(2,0,0) — 2D Euclidean (complex numbers as even subalgebra)
 *   Cl(3,0,0) — 3D Euclidean (quaternions as even subalgebra)
 *   Cl(3,1,0) — Minkowski spacetime (Dirac algebra)
 *   Cl(3,0,1) — 3D Projective Geometric Algebra (PGA), rigid-body mechanics
 *   Cl(4,1,0) — 5D Conformal Geometric Algebra (CGA)
 */

#include "value.h"
#include <stdint.h>

/* Geometric product of basis blades A and B in Cl(p,q,r).
   Returns 0 (null metric cancellation), +1, or -1.
   *out_blade is set to the result blade bitmap. */
int mv_blade_gp(int A, int B, int p, int q, int r, int *out_blade);

/* Allocate a zero multivector in Cl(p,q,r). n=p+q+r must be ≤ 8. */
val_t mv_make(int p, int q, int r);

/* Arithmetic — all return a new multivector */
val_t mv_add(val_t a, val_t b);
val_t mv_sub(val_t a, val_t b);
val_t mv_neg(val_t a);
val_t mv_scale(val_t mv, double s);
val_t mv_geom(val_t a, val_t b);        /* geometric product a*b */
val_t mv_wedge(val_t a, val_t b);       /* outer/wedge product a∧b */
val_t mv_lcontract(val_t a, val_t b);   /* left contraction a⌋b */

/* Involutions */
val_t mv_reverse(val_t a);    /* ã: reverses blade order; grades 2,3 mod 4 flip sign */
val_t mv_involute(val_t a);   /* â: grade involution; odd grades flip sign */
val_t mv_conjugate(val_t a);  /* ā: Clifford conjugate = involute ∘ reverse */

/* Other operations */
val_t mv_dual(val_t a);            /* right complement: a * rev(I) */
val_t mv_grade(val_t a, int k);    /* grade-k projection */
double mv_norm2_d(val_t a);        /* ⟨ã·a⟩₀ as double */
val_t mv_norm(val_t a);            /* sqrt(|norm2|) */
val_t mv_normalize(val_t a);       /* a / ‖a‖ */

/* Convert blade specifier to bitmap: integer bitmap OR list of 1-based indices.
   Returns -1 on error. */
int mv_blade_index(val_t spec, int n);

/* Write a multivector to a port */
void mv_write(val_t v, val_t port);

/* Register all Scheme builtins into env */
void mv_register_builtins(val_t env);

#endif /* CURRY_MULTIVEC_H */
