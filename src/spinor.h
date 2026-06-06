#ifndef CURRY_SPINOR_H
#define CURRY_SPINOR_H

/*
 * Spinor types for Curry Scheme.
 *
 * Spinors carry an SL(2,C) / Lorentz transform law that is NOT the same as
 * a plain tensor.  This type enforces that invariant: a spinor is never silently
 * promoted to a tensor, and spinor-transform applies the correct representation.
 *
 * Representations:
 *   weyl-l  (left-handed Weyl ψ_α)      2 complex components; ψ → M·ψ
 *   weyl-r  (right-handed Weyl ψ̄_α̇)    2 complex components; ψ → (M†)^{-1}·ψ
 *   dirac   (Dirac bispinor)            4 complex components; block (M, (M†)^{-1})
 *   majorana (self-conjugate Dirac)     4 complex components; ψ_R = C·ψ_L*
 *
 * The SL(2,C) matrix M is passed as a nested list ((a b)(c d)) of complex
 * numbers (or reals promoted to complex).
 *
 * Scheme API:
 *   (make-spinor kind c0 c1 ...)    kind = 'weyl-l | 'weyl-r | 'dirac | 'majorana
 *   (spinor? v)
 *   (spinor-kind v)                 -> symbol
 *   (spinor-ncomp v)                -> 2 or 4
 *   (spinor-ref v i)                -> complex number at index i (0-based)
 *   (spinor-set! v i z)             -> void
 *   (spinor+ a b)
 *   (spinor- a b) / (spinor- a)     -> negate
 *   (spinor-scale s z)              -> scale by complex z
 *   (spinor-transform s M)          -> new spinor after SL(2,C) transform M
 *   (spinor-conjugate s)            -> weyl-l <-> weyl-r; dirac parts swapped
 *   (spinor-adjoint s)              -> Dirac adjoint ψ̄ = ψ†γ⁰
 *   (spinor-inner a b)              -> complex scalar ψ†·χ (Hermitian inner product)
 *   (spinor->list s)                -> list of complex components
 */

#include "value.h"

void spinor_register_builtins(val_t env);

val_t spinor_make(uint8_t kind, uint8_t ncomp, const double *re, const double *im);

void spinor_write(val_t v, val_t port);

#endif /* CURRY_SPINOR_H */
