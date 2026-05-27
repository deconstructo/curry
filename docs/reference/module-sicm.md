# Module: `(curry sicm)`

*v0.9.0 — 2026-05-27*

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

## Rigid body mechanics (Ch 2)

Matrices are represented as lists of rows, each a list of three elements.

### Rotation matrix

```scheme
; ZXZ Euler angles (φ, θ, ψ) → 3×3 rotation matrix R = Rz(φ)·Rx(θ)·Rz(ψ)
(rotation-matrix-from-Euler phi theta psi)
```

### Angular velocity

```scheme
; Angular velocity in body frame from Euler-angle local tuple
; local = (up t (up φ θ ψ) (up φ̇ θ̇ ψ̇))
; Returns (up ω₁ ω₂ ω₃) in principal-axis coordinates.
(Euler->omega-body local)

; Angular velocity in space frame
(Euler->omega-space local)
```

### Kinetic energy and Lagrangian

```scheme
; T = ½(A·ω₁² + B·ω₂² + C·ω₃²)  — principal moments A, B, C
(T-rigid-body A B C)   ; local → kinetic energy

; Torque-free Lagrangian L = T  (same function, different name)
(L-rigid-body A B C)
```

### Matrix helpers

```scheme
(mat-ref      M i j)   ; element access (0-based)
(mat-transpose M)      ; 3×3 transpose
(mat-vec-mul   M v)    ; 3×3 × 3-list → 3-list
(mat-mat-mul  M1 M2)   ; 3×3 × 3×3 → 3×3
```

### Angle normalisation

```scheme
; Reduce angle x to (−xmax, xmax].
((principal-value xmax) x)

; Example: normalise to (−π, π]
(define norm (principal-value (acos -1.0)))
(norm (* 3.0 (acos -1.0)))   ; => π
```

### Example — torque-free symmetric top

```scheme
(import (curry sicm))
(import (curry ode))

(define (euler-rhs A B C)
  (lambda (t omega)
    (list (/ (* (- B C) (list-ref omega 1) (list-ref omega 2)) A)
          (/ (* (- C A) (list-ref omega 2) (list-ref omega 0)) B)
          (/ (* (- A B) (list-ref omega 0) (list-ref omega 1)) C))))

; Symmetric top A=B=1, C=2: ω₃ is conserved.
(define omega-fin
  (ode-rk4 (euler-rhs 1.0 1.0 2.0) '(0.1 0.0 1.0) 0.0 10.0 0.01))
(list-ref omega-fin 2)   ; => ≈ 1.0  (conserved)
```

---

## Numerical trajectory evolution (Ch 3–4)

Uses a **symplectic Störmer-Verlet** (leapfrog) integrator. Unlike plain RK4, the
symplectic integrator preserves the phase-space volume form, so energy error
is bounded O(h²) rather than drifting secularly. Essential for Poincaré
sections and any long-run integration.

State representation: `(up t q p)` where `q` and `p` are either flonums
(1-DOF) or up-tuples of flonums (multi-DOF). Gradients ∂H/∂q and ∂H/∂p
are computed by central finite differences, so `H` can be any Scheme
function returning a real number — no symbolic form required.

### Integration

```scheme
; Integrate H for n-steps starting from state0.
; Returns a list of (n-steps + 1) states including state0.
(evolve-Hamiltonian H state0 dt n-steps)

; Convert Lagrangian initial tuple (up t q qdot) to (up t q p),
; build H via Lagrangian->Hamiltonian, then call evolve-Hamiltonian.
; Works for standard T−V Lagrangians with diagonal kinetic energy.
(evolve-Lagrangian L local0 dt n-steps)

; Single step — useful for custom integration loops.
(stoermer-verlet-step H dt state)    ; → (up t q p)
```

### State conversion

```scheme
; Numerically compute p = ∂L/∂qdot and return (up t q p).
(lagrangian->hamiltonian-state L local)
```

### Poincaré surface of section

```scheme
; Collect phase-space points at upward crossings of q₀ = 0.
; Returns a list of (cons q₁ p₁) pairs.
; For 1-DOF: records (cons q p) at each upward zero crossing of q.
(poincare-section H state0 dt n-steps)
```

### Example — harmonic oscillator

```scheme
(import (curry sicm))

(define (H-ho m k)
  (lambda (state)
    (+ (/ (* (momentum   state) (momentum   state)) (* 2.0 m))
       (* 0.5 k (coordinate state) (coordinate state)))))

(define H  (H-ho 1.0 1.0))
(define s0 (up 0.0 1.0 0.0))        ; t=0, q=1, p=0

; Integrate for 2π seconds (one period)
(define pi (acos -1.0))
(define states (evolve-Hamiltonian H s0 0.001 (exact (round (/ (* 2 pi) 0.001)))))

; Energy is conserved to O(h²) ≈ 1e-6
(H s0)                               ; => 0.5
(H (car (reverse states)))           ; => ≈ 0.5
```

### Example — Hénon-Heiles Poincaré section

```scheme
(define (H-hh)
  (lambda (state)
    (let* ((q (coordinate state)) (p (momentum state))
           (x (ref q 0)) (y (ref q 1))
           (px (ref p 0)) (py (ref p 1)))
      (+ (* 0.5 (+ (* px px) (* py py)))
         (* 0.5 (+ (* x x) (* y y)))
         (* x x y)
         (* (/ -1.0 3.0) y y y)))))

; IC on the energy surface E=1/12, starts off the y-axis so x oscillates.
(define E (/ 1.0 12.0))
(define s0 (up 0.0 (up 0.2 0.0) (up (sqrt (* 2.0 (- E 0.02))) 0.0)))

; Collect ~50 crossings of x=0 over 15000 steps.
(define pts (poincare-section (H-hh) s0 0.005 15000))

; pts is a list of (y . py) pairs lying on invariant curves.
(length pts)   ; => O(50) for regular orbit
```

### Notes on accuracy

- Energy error is O(h²) per step and bounded (does not grow with time).
  For h=0.01 expect |ΔE/E| ≲ 10⁻⁵ over thousands of steps.
- Gradients use central differences with step ε = 10⁻⁷. For ill-conditioned
  Hamiltonians with very small or very large scales, adjust `sv-eps` in the
  module source.
- `evolve-Lagrangian` requires `Lagrangian->Hamiltonian` to succeed symbolically.
  This works for any Lagrangian of the form T(qdot) − V(q) with diagonal T.

---

## Coverage by chapter

| Chapter | Status |
|---|---|
| Ch 1 — Lagrangian mechanics | ✓ Complete |
| Ch 2 — Rigid body mechanics | ✓ Complete |
| Ch 3–4 — Numerical trajectory evolution | ✓ Complete |
| Ch 5 — Canonical transformations | Planned (Phase 14) |
| Ch 6–7 — Perturbation theory / Lie transforms | Planned (Phase 15) |
