# Module: `(curry sicm)`

*v0.8.17 — 2026-05-23*

Structure and Interpretation of Classical Mechanics — symbolic mechanics on top of Curry's CAS. Procedure names follow Sussman & Wisdom, *SICM* 2nd ed. (MIT Press). Includes a scmutils compatibility shim so most Ch 1 examples run with minimal adaptation.

## Import

```scheme
(import (curry sicm))
```

## Differences from scmutils / MIT Scheme

| scmutils | Curry | Notes |
|---|---|---|
| Bare `'m`, `'k` as literals | `(sym-var 'm)` or `(define-symbolic m k)` | Curry requires explicit sym-vars |
| Auto-pretty REPL output | `(pe expr)` | Wrap results with `pe` to see infix |
| `(literal-function 'f (-> (Real) Real))` | `(literal-function 'f)` | Type annotation accepted and ignored |
| `Dq(t)` notation | `q_t(t)` notation | Same maths, different rendering |

## Quickstart

```scheme
(import (curry sicm))

(define-symbolic t m k)
(define q (literal-function 'q))

; Harmonic oscillator equations of motion
(define eom ((Lagrange-equations (L-harmonic m k)) q))
(pe (eom t))
; => -(k * q(t)) - m * q_t_t(t)
```

## scmutils compatibility shim

### Pretty-printing

```scheme
(pe expr)                  ; display expr in infix notation, then newline
(print-expression expr)    ; alias for pe
(show-expression  expr)    ; alias for pe
```

### Declaring symbolic variables

```scheme
(define-symbolic t x y z)
; equivalent to:
; (define t (sym-var 't))
; (define x (sym-var 'x))  ...

(literal-number 'x)        ; alias for (sym-var 'x)
```

### `literal-function`

```scheme
(literal-function 'f)                       ; 1-arg function of t
(literal-function 'f 2)                     ; 2-arg function (integer arity)
(literal-function 'f (-> (Real) Real))      ; scmutils type annotation — accepted, ignored
```

## Utilities

```scheme
(compose f g ...)          ; ((compose f g) x) = (f (g x))
(square x)                 ; (* x x) for scalars; Σ xᵢ² for tuples
```

## Local tuples

A local tuple is `(up t q qdot ...)`. Selectors:

```scheme
(time         local)       ; slot 0 — time
(coordinate   local)       ; slot 1 — generalised coordinate(s)
(velocity     local)       ; slot 2 — generalised velocity/velocities
(acceleration local)       ; slot 3 — acceleration (if present)
(momentum     state)       ; slot 2 of a Hamiltonian state (up t q p)
```

### Tuple constructors, predicates, and accessors

Up-tuples represent contravariant (column) vectors; down-tuples represent covariant (row) vectors.

```scheme
(up   v0 v1 ...)           ; construct an up-tuple (contravariant)
(down v0 v1 ...)           ; construct a down-tuple (covariant)

(up?    v)                 ; #t if v is an up-tuple
(down?  v)                 ; #t if v is a down-tuple
(tuple? v)                 ; #t if v is either kind of tuple

(ref   tup i)              ; 0-based element access
(dimension tup)            ; number of components

(tuple->list tup)          ; convert to a Scheme list
(list->up    lst)          ; build an up-tuple from a list
(list->down  lst)          ; build a down-tuple from a list
```

Example:

```scheme
(define q (up 1 2 3))
(up? q)           ; => #t
(dimension q)     ; => 3
(ref q 1)         ; => 2
(tuple->list q)   ; => (1 2 3)
(list->down '(4 5 6))  ; => (down 4 5 6)
```

## Path functor Γ

```scheme
(Gamma q)          ; t → (up t (q t) ((D q) t))
(Gamma-bar q n)    ; t → (up t (q t) ((D q) t) ... nth derivative)
```

## Euler-Lagrange equations

```scheme
; Returns a function (q → t → residual).  Set residual = 0 for EOM.
(Lagrange-equations L)

; Returns a function (local → residual) — path-free form.
(Euler-Lagrange-operator L)
```

## Standard Lagrangians

```scheme
(L-free-particle m)               ; ½ m qdot²
(L-harmonic m k)                  ; ½ m qdot² − ½ k q²
(L-uniform-acceleration m g)      ; ½ m qdot² − m g q
(make-Lagrangian T V)             ; L = T − V  (T and V are functions of local)

; Multi-DOF
(L-free-particle-nd m)            ; ½ m |qdot|²  (q is an up-tuple)
(L-harmonic-nd m k)               ; ½ m |qdot|² − ½ k |q|²
(L-central-rectangular m V-fn)    ; 2D Cartesian, V-fn is a function of r
(L-Kepler-polar m GM)             ; Kepler in polar coords (up r θ)
```

## Energy extraction

```scheme
(Lagrangian->energy L)   ; local → E = qdot·∂L/∂qdot − L  (= T+V for standard L)
(Lagrangian->T L)        ; local → kinetic energy
(Lagrangian->V L)        ; local → potential energy
```

## Hamiltonian mechanics

A Hamiltonian state is `(up t q p)`.

```scheme
; Legendre transform — works for diagonal kinetic energy T = ½ Σ mᵢ qdotᵢ²
(Lagrangian->Hamiltonian L)        ; H-state → H(t,q,p)

; Direct constructor
(make-Hamiltonian T* V)            ; T* is kinetic energy as function of p

; Hamilton's equations: returns (up dq/dt dp/dt) at an H-state
(Hamilton-equations H)

; Poisson bracket {f, g}
(Poisson-bracket f g)

; Operator commutator [A, B]f = A(Bf) − B(Af)
(commutator A B)
```

## Worked examples

### Free particle (SICM §1.5)

```scheme
(import (curry sicm))
(define-symbolic t m)
(define q (literal-function 'q))

(pe ((( Lagrange-equations (L-free-particle m)) q) t))
; => -(m * q_t_t(t))
```

### Harmonic oscillator (SICM §1.6)

```scheme
(define-symbolic t m k)
(define q (literal-function 'q))

(pe (((Lagrange-equations (L-harmonic m k)) q) t))
; => -(k * q(t)) - m * q_t_t(t)
```

### Energy of a harmonic oscillator

```scheme
(define-symbolic t m k)
(define q (literal-function 'q))
(define local ((Gamma q) t))

(pe ((Lagrangian->energy (L-harmonic m k)) local))
; => ½ k * q(t)² + ½ m * q_t(t)²
```

### Kepler problem in polar coordinates (SICM §1.8)

```scheme
(define-symbolic t m GM)
(define q (literal-function* 'q 2))    ; q = (up r θ)

(pe (((Lagrange-equations (L-Kepler-polar m GM)) q) t))
; => equations of motion in r and θ
```

### Hamiltonian for harmonic oscillator

```scheme
(define-symbolic t m k)
(define q (literal-function 'q))
(define H (Lagrangian->Hamiltonian (L-harmonic m k)))
(define-symbolic p)

(pe (H (up t q p)))
; => p²/(2m) + ½ k * q²
```

### Poisson brackets

```scheme
(define-symbolic q p)
(define (q-fn state) (coordinate state))
(define (p-fn state) (momentum   state))

(pe ((Poisson-bracket q-fn p-fn) (up 0 q p)))
; => 1
```

## Coverage by chapter

| Chapter | Status |
|---|---|
| Ch 1 — Lagrangian mechanics | ✓ Complete |
| Ch 2 — Rigid body mechanics | Planned (Phase 12) |
| Ch 3–4 — Numerical integration | Planned (Phase 13) |
| Ch 5 — Canonical transformations | Planned (Phase 14) |
| Ch 6–7 — Perturbation theory | Planned (Phase 15) |
