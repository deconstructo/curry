# Surreal Numbers

Curry has a first-class surreal number type integrated into the numeric tower. Surreal numbers extend the real line with ω (the first infinite ordinal) and ε = 1/ω (the first positive infinitesimal), plus all arithmetic combinations.

## Representation

Surreals are stored in **Hahn-series form**: a finite list of terms `cᵢ · ωᵉⁱ` sorted by exponent descending. Exponents and coefficients are exact rationals.

```
3 + 2ω     →  [(exp=1, coeff=2), (exp=0, coeff=3)]
ε          →  [(exp=-1, coeff=1)]
ω² - 3ε    →  [(exp=2, coeff=1), (exp=-1, coeff=-3)]
```

Every real number is a surreal with a single term at exponent 0. The form is closed under all field operations.

## Constants

```scheme
omega        ; ω — first infinite surreal
epsilon      ; ε = 1/ω — first positive infinitesimal
```

## Arithmetic

All standard operators work on surreals. Numbers are automatically promoted when mixed.

```scheme
(+ omega 3)          ; => ω + 3
(- omega 1)          ; => ω - 1
(* 2 omega)          ; => 2ω
(/ omega 2)          ; => 1/2ω
(/ 1 omega)          ; => ε           (1/ω = ε)
(* omega epsilon)    ; => 1           (ω·ω⁻¹ = 1)
(* epsilon epsilon)  ; => ω^-2        (ε² = ω⁻²)
(+ 1 epsilon)        ; => 1 + ε
(expt omega 2)       ; => ω²
(sqrt omega)         ; => ω^1/2
(expt omega 1/3)     ; => ω^1/3
(make-surreal '((2 . 3) (0 . -1) (-1 . 5)))  ; => 3ω² - 1 + 5ε
```

Mixed surreal + plain number automatically promotes:

```scheme
(+ omega 42)         ; => ω + 42
(* 1/3 epsilon)      ; => 1/3ε
```

## Ordering

Surreals are totally ordered. The ordering respects the Hahn-series structure: ω dominates any finite number, ε is positive but smaller than any positive real.

```scheme
(< epsilon 1)            ; => #t
(< epsilon 1/1000000)    ; => #t     (ε < any positive real)
(> omega 1000000000)     ; => #t     (ω > any integer)
(< 0 epsilon)            ; => #t
(= (+ omega 1) (+ 1 omega)) ; => #t  (commutative)
```

Standard comparison predicates all work: `<`, `>`, `=`, `<=`, `>=`, `min`, `max`.

## Classification

```scheme
(surreal? omega)                    ; => #t
(surreal-infinite? omega)           ; => #t  (leading exponent > 0)
(surreal-finite? (+ 3 epsilon))     ; => #t  (no ω term)
(surreal-infinitesimal? epsilon)    ; => #t  (all exponents < 0)
(surreal-infinitesimal? (* 2 epsilon)) ; => #t

(infinite? omega)    ; => #t    (standard R7RS infinite? predicate)
(finite? epsilon)    ; => #t    (finite in the R7RS sense)
(number? omega)      ; => #t
```

## Accessors

```scheme
(surreal-real-part    (+ omega 3))    ; => 3    (coefficient of ω⁰)
(surreal-omega-part   (+ omega 3))    ; => 1    (coefficient of ω¹)
(surreal-epsilon-part (+ 3 epsilon))  ; => 1    (coefficient of ω⁻¹)
(surreal-nterms       (+ omega 3))    ; => 2
(surreal-terms        (+ omega 3))    ; => ((1 . 1) (0 . 3))
(surreal-birthday     omega)          ; => ω
(surreal-birthday     42)             ; => 42
(surreal->number      (+ omega 0))    ; => ω    (extracts if pure real)
(surreal->number      (make-surreal '((0 . 7))))  ; => 7
```

## Construction

```scheme
; From a list of (exponent . coefficient) pairs
; Terms need not be sorted — they are sorted and normalized automatically
(make-surreal '((2 . 1) (0 . -5) (-1 . 3)))  ; => ω² - 5 + 3ε
```

## Numeric tower position

Surreals slot between rational and flonum for real-line promotion. A plain integer or rational mixed with a surreal promotes to surreal; a surreal mixed with a flonum falls back to double (losing the ω/ε structure).

```
fixnum → bignum → rational → surreal → flonum → complex → quaternion → octonion
```

`(inexact omega)` returns `+inf.0`. `(inexact epsilon)` returns `0.0` (the standard part of an infinitesimal is 0).

## Automatic differentiation

Surreals provide **forward-mode automatic differentiation** for free. Because `f(x + ε) = f(x) + f'(x)·ε + O(ε²)`, evaluating a function at `x + ε` and extracting the ε coefficient gives the exact derivative.

```scheme
; (auto-diff f x) computes f'(x) numerically via f(x + ε)
(auto-diff (lambda (x) (* x x)) 5)           ; => 10   (d/dx x² at 5)
(auto-diff (lambda (x) (expt x 3)) 2)         ; => 12   (d/dx x³ at 2)
(auto-diff (lambda (x) (/ 1 x)) 1)            ; => -1   (d/dx 1/x at 1)
(auto-diff sin 0)                              ; => 1.0  (cos 0)
(auto-diff (lambda (x) (* x (+ x 1))) 3)      ; => 7    (d/dx x(x+1) = 2x+1 at 3)
```

This is exact for polynomial and rational functions. For transcendentals it matches the floating-point result (since `sin(ε) ≈ ε` etc.).

You can also do it manually:

```scheme
(define x+ε (+ 3 epsilon))
(define y   (* x+ε x+ε))         ; => 9 + 6ε + ω^-2
(surreal-epsilon-part y)          ; => 6    (d/dx x² at 3 = 2x = 6) ✓
```

## Connection to symbolic differentiation

For **symbolic** differentiation of arbitrary expressions, use `(∂ expr var)`. For **numeric** differentiation of concrete functions, use `auto-diff`. They cross-check each other:

```scheme
(symbolic x)
(∂ (* x x) x)                        ; => (* 2 x)   (symbolic)
(auto-diff (lambda (x) (* x x)) 3)   ; => 6         (numeric, at x=3)
(substitute (∂ (* x x) x) x 3)       ; => 6         (evaluate symbolic at 3)
```

## Physics

In physics simulation, ε provides a natural handle on differential quantities:

```scheme
; Velocity from position p(t) sampled at t and t+dt
(define (velocity p t dt)
  (/ (- (p (+ t dt)) (p t)) dt))

; With dt = ε, get exact instantaneous velocity
(define (exact-velocity p t)
  (surreal-epsilon-part (p (+ t epsilon))))

(define (p t) (* 3 (expt t 2)))   ; position: 3t²
(exact-velocity p 2)               ; => 12   (= 6t at t=2) ✓
```

## Quick-reference table

| Procedure | Description |
|-----------|-------------|
| `omega` | First infinite surreal |
| `epsilon` | First positive infinitesimal |
| `(make-surreal alist)` | Construct from (exp . coeff) pairs |
| `(surreal? v)` | Predicate |
| `(surreal-infinite? v)` | Highest exponent > 0 |
| `(surreal-finite? v)` | No ω terms |
| `(surreal-infinitesimal? v)` | Nonzero, all exponents < 0 |
| `(surreal-real-part s)` | Coefficient of ω⁰ |
| `(surreal-omega-part s)` | Coefficient of ω¹ |
| `(surreal-epsilon-part s)` | Coefficient of ω⁻¹ |
| `(surreal-terms s)` | List of (exp . coeff) pairs |
| `(surreal-nterms s)` | Number of Hahn-series terms |
| `(surreal-birthday s)` | Conway birthday (as surreal) |
| `(surreal->number s)` | Extract to simpler type if possible |
| `(auto-diff f x)` | Numeric derivative of f at x via ε |
| All arithmetic | `+` `-` `*` `/` `expt` `sqrt` `abs` |
| All comparison | `<` `>` `=` `<=` `>=` `min` `max` |
| `(infinite? s)` | Standard R7RS predicate, works on surreals |
| `(finite? s)` | Standard R7RS predicate |
