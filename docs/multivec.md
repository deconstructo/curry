# Multivectors — Clifford Algebra Cl(p,q,r)

Curry provides first-class multivectors: elements of the Clifford algebra Cl(p,q,r) over the reals. Multivectors unify scalars, vectors, bivectors, spinors, and rotors in a single type and make coordinate-free geometric computation tractable.

## Background

A Clifford algebra Cl(p,q,r) has `n = p+q+r` basis vectors `e₁ … eₙ` satisfying:

| | squaring rule |
|---|---|
| first *p* vectors | eᵢ² = +1 |
| next *q* vectors | eᵢ² = −1 |
| last *r* vectors | eᵢ² = 0 (null / degenerate) |

A multivector is a linear combination of **blades** — products of distinct basis vectors. For `n` generators there are 2ⁿ blades, one per subset of `{1…n}`. Blades are indexed internally by bitmap: bit *k* set means `eₖ₊₁` is present, so the scalar is blade 0, `e₁` is blade 1, `e₂` is blade 2, `e₁e₂` is blade 3, etc.

Useful algebras:

| Signature | Use |
|-----------|-----|
| Cl(2,0,0) | 2D Euclidean — complex numbers as even subalgebra |
| Cl(3,0,0) | 3D Euclidean — quaternions as even subalgebra; rotors |
| Cl(3,1,0) | Minkowski spacetime — Dirac algebra |
| Cl(3,0,1) | 3D Projective Geometric Algebra (PGA) — rigid-body motors |
| Cl(4,1,0) | 5D Conformal Geometric Algebra (CGA) — spheres, circles, inversions |

Maximum n is 8 (256 blades).

## Construction

```scheme
; (make-mv p q r) → zero multivector in Cl(p,q,r)
(define mv (make-mv 3 0 0))   ; zero element of Cl(3,0,0), 8 components

; (mv-e p q r idx...) → unit blade
; idx arguments are 1-based generator indices
(define e1  (mv-e 3 0 0 1))   ; e₁ in Cl(3,0,0)
(define e12 (mv-e 3 0 0 1 2)) ; e₁₂ = e₁∧e₂

; (mv-from-list p q r component-list) → multivector with given components
; components in blade-bitmap order (scalar first, then e1, e2, e12, ...)
(define mv2 (mv-from-list 3 0 0 '(1.0 0.0 0.0 0.0 0.0 0.0 0.0 0.0)))

; (mv-ref mv blade) → double component
; blade is an integer bitmap or a list of 1-based indices
(mv-ref e12 3)          ; 1.0  (blade 3 = bits 0b011 = e₁∧e₂)
(mv-ref e12 '(1 2))     ; 1.0  (same blade, index form)

; (mv-set! mv blade value) → void, mutates
(mv-set! mv 0 5.0)      ; set scalar part to 5
```

## Arithmetic

All arithmetic operations return a new multivector (same signature as inputs).

```scheme
; Addition / subtraction / negation
(mv+ a b)              ; element-wise sum
(mv- a b)              ; difference
(mv- a)                ; negation (unary)

; Scalar scaling
(mv-scale mv 2.5)      ; scale all components by 2.5

; Geometric product (the fundamental product)
(mv* a b)              ; geometric product a b
(mv* a b c)            ; left-associative: ((a b) c)

; Outer / wedge product  a∧b  (grade-raising)
(mv-wedge a b)

; Left contraction  a⌋b  (grade-lowering / inner product)
(mv-lcontract a b)
```

## Involutions

```scheme
(mv-reverse   mv)   ; ã — reverse blade order; grades 2,3 mod 4 pick up a sign
(mv-involute  mv)   ; â — grade involution; odd-grade blades flip sign
(mv-conjugate mv)   ; ā = involute(reverse(mv))  (Clifford conjugate)
```

These satisfy `(mv* mv (mv-reverse mv))` = scalar norm² for versors.

## Grade projection and norms

```scheme
(mv-grade  mv k)    ; project onto grade-k part
(mv-scalar mv)      ; grade-0 component as a Scheme number

(mv-norm2 mv)       ; ⟨ã·a⟩₀ as flonum (can be negative for mixed signature)
(mv-norm  mv)       ; sqrt(|norm2|)
(mv-normalize mv)   ; mv / ‖mv‖

(mv-dual mv)        ; right complement: mv * reverse(pseudoscalar)
```

## Inspection

```scheme
(mv? x)                   ; #t if x is a multivector
(mv-signature mv)         ; (list p q r)
```

## Conversions

```scheme
; Quaternion ↔ Cl(3,0,0) rotor embedding
; Quaternion (w x y z) maps to the even subalgebra:
;   scalar = w,  e₂₃ = x,  e₁₃ = y,  e₁₂ = z
(quaternion->mv q)          ; q → Cl(3,0,0) multivector
(quaternion->mv w x y z)    ; explicit components
(mv->quaternion mv)         ; Cl(3,0,0) multivector → quaternion
```

## Examples

### Rotation in 3D with a rotor

A rotor encodes a rotation by angle θ around unit axis (ax,ay,az) as `R = cos(θ/2) - sin(θ/2)(ax·e₂₃ + ay·e₁₃ + az·e₁₂)`. Applying it: `v' = R v R̃`.

```scheme
(define (make-rotor-3d ax ay az theta)
  (let* ((h  (/ theta 2.0))
         (s  (- (sin h)))
         (mv (make-mv 3 0 0)))
    (mv-set! mv 0          (cos h))     ; scalar
    (mv-set! mv #b110 (* s ax))         ; e₂₃ = blade 6
    (mv-set! mv #b101 (* s ay))         ; e₁₃ = blade 5
    (mv-set! mv #b011 (* s az))         ; e₁₂ = blade 3
    mv))

(define (rotate-vector-3d rotor vx vy vz)
  (let ((v (mv-from-list 3 0 0 (list 0.0 vx vy vz 0.0 0.0 0.0 0.0))))
    (mv* rotor (mv* v (mv-reverse rotor)))))

; Rotate the y-axis 90° around z
(define r (make-rotor-3d 0 0 1 (/ 3.14159265 2)))
(define v' (rotate-vector-3d r 1.0 0.0 0.0))
(mv-ref v' 2)  ; ≈ 0.0  (e₁ component)
(mv-ref v' 4)  ; ≈ 1.0  (e₂ component, blade 4 = bit 0b100)
```

### Complex numbers in Cl(2,0,0)

The even subalgebra of Cl(2,0,0) is spanned by {1, e₁₂}. With `i = e₁₂` (blade 3):

```scheme
(define (complex->mv2d re im)
  (let ((mv (make-mv 2 0 0)))
    (mv-set! mv 0 re)
    (mv-set! mv 3 im)
    mv))

(define i (mv-e 2 0 0 1 2))
(mv* i i)   ; → -1 scalar (since e₁₂² = -1 in Cl(2,0,0)? No: e₁²e₂² = +1·+1 = +1
            ; but the reversion of two swaps picks up one sign: e₁₂² = -1.)
```

### 3D PGA rigid body motion

In Cl(3,0,1) (three positive, one null), lines are bivectors and motors (screws) are even multivectors encoding both rotation and translation.

```scheme
(define pga (make-mv 3 0 1))   ; Cl(3,0,1), 16 blades
```

## Relationship to quaternions and octonions

Quaternions are the even subalgebra of Cl(3,0,0). The `quaternion->mv` and `mv->quaternion` converters let you move between representations. Octonions are **not** a Clifford algebra (they are non-associative); they remain their own type in Curry's numeric tower.
