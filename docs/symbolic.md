# Symbolic Expressions — Computer Algebra in Curry

*v0.8.5 — 2026-05-16*

Curry's numeric tower includes a built-in computer algebra system (CAS). Any operation that would normally fail with "wrong type" when applied to an unbound symbol instead builds a symbolic expression tree. The evaluator becomes a CAS by default.

## Declaring symbolic variables

```scheme
(symbolic x)           ; x is now a symbolic variable
(symbolic x y z)       ; declare several at once
```

After this, `x` is not an error — it evaluates to a symbolic variable object rather than raising "unbound variable".

```scheme
(symbolic x)
x                      ; => x           (a symbolic variable)
(+ x 1)               ; => (+ x 1)     (a symbolic expression)
(* x x)               ; => (* x x)
(expt x 3)            ; => (expt x 3)
```

Predicates:

```scheme
(sym-var?   x)         ; #t — is it a bare variable?
(sym-expr?  e)         ; #t — is it a composite expression?
(symbolic?  v)         ; #t — either a variable or an expression
(sym-var-name x)       ; "x"  — the variable name as a string
```

### Assumption flags

`sym-var` accepts an optional second argument declaring a domain assumption. Assumptions unlock additional algebraic simplification rules.

```scheme
(define xp (sym-var 'x 'positive))   ; x > 0  (implies real, nonzero)
(define xn (sym-var 'x 'negative))   ; x < 0  (implies real)
(define xr (sym-var 'x 'real))       ; x ∈ ℝ
(define xi (sym-var 'x 'integer))    ; x ∈ ℤ  (implies real)
(define xz (sym-var 'x 'nonzero))    ; x ≠ 0
(define q  (sym-var 'q 'quaternion)) ; q ∈ ℍ  (non-commutative)

(sym-assumption? xp 'positive)       ; => #t
(sym-assumption? xp 'real)           ; => #t  (implied)
(sym-assumption? xp 'nonzero)        ; => #t  (implied)
(sym-assumption? q  'quaternion)     ; => #t
```

**Simplification rules enabled by assumptions:**

| Expression | With assumption | Result |
|-----------|----------------|--------|
| `(abs xp)` | `positive` | `xp` |
| `(abs xn)` | `negative` | `(- xn)` |
| `(sqrt (expt xp 2))` | `positive` | `xp` |
| `(log (expt xp n))` | `positive` | `(* n (log xp))` |
| `(sign xp)` | `positive` | `1` |
| `(sign xn)` | `negative` | `-1` |
| `(* q p)` | `quaternion` | `(nc* q p)` (non-commutative — order preserved) |

```scheme
(define xp (sym-var 'x 'positive))

(abs xp)                          ; => xp            (not |x|)
(simplify (sqrt (expt xp 2)))     ; => xp            (not (sqrt (expt x 2)))
(simplify (log (expt xp 3)))      ; => (* 3 (log x))
(sign xp)                         ; => 1
(sign (sym-var 'y 'negative))     ; => -1
```

## Symbolic arithmetic

All numeric operations propagate through symbolic values automatically. You do not need to call anything special — just use `+`, `*`, `expt`, `sin`, etc. normally.

```scheme
(symbolic m v t)

(+ m v)                ; => (+ m v)
(* 2 x)               ; => (* 2 x)
(* m (expt v 2))      ; => (* m (expt v 2))
(/ 1 (+ x 1))         ; => (/ 1 (+ x 1))
(sin (* 2 x))         ; => (sin (* 2 x))
(exp x)               ; => (exp x)
(log x)               ; => (log x)
(sqrt x)              ; => (sqrt x)
(abs x)               ; => (abs x)

; Hyperbolic functions
(sinh x)              ; => (sinh x)
(cosh x)              ; => (cosh x)
(tanh x)              ; => (tanh x)

; Inverse trig — evaluates numerically for real and complex; symbolic for higher tower
(asin x)              ; => (asin x)
(acos x)              ; => (acos x)
(atan x)              ; => (atan x)
(asinh x)             ; => (asinh x)
(acosh x)             ; => (acosh x)
(atanh x)             ; => (atanh x)

; Reciprocal trig
(cot x)               ; => (cot x)
(sec x)               ; => (sec x)
(csc x)               ; => (csc x)

; Sign function — simplifies to 1 / -1 with assumption flags
(sign x)              ; => (sign x)
(sign 5)              ; => 1
(sign -3)             ; => -1
```

Nested expressions are built automatically:

```scheme
(symbolic m v)
(define ke (* 1/2 m (expt v 2)))   ; kinetic energy
ke   ; => (* 1/2 m (expt v 2))
```

### Numeric evaluation domains

| Function group | Real | Complex | Quaternion+ |
|----------------|------|---------|-------------|
| `sin`, `cos`, `exp`, `log`, `sqrt`, `sinh`, `cosh`, `tanh` | ✓ | ✓ | symbolic |
| `tan`, `cot`, `sec`, `csc` | ✓ | ✓ (via sin/cos) | symbolic |
| `asin`, `acos`, `atan`, `asinh`, `acosh`, `atanh` | ✓ | ✓ | symbolic |

The inverse trig and inverse hyperbolic functions evaluate over complex numbers using the standard logarithmic identities (principal values, matching ISO C99 branch cuts):

```
asin(z)  = -i · ln(iz + √(1−z²))
acos(z)  = -i · ln(z + i·√(1−z²))
atan(z)  = (i/2) · ln((1−iz)/(1+iz))
asinh(z) = ln(z + √(z²+1))
acosh(z) = ln(z + √(z²−1))
atanh(z) = (1/2) · ln((1+z)/(1−z))
```

```scheme
(asin (make-rectangular 0 1))    ; ≈ 0+0.8814i
(acos (make-rectangular 0 1))    ; ≈ 1.5708-0.8814i
(atan (make-rectangular 0 0.5))  ; ≈ 0+0.5493i
(asinh (make-rectangular 0 1))   ; ≈ 0+1.5708i   [= iπ/2]
(acosh (make-rectangular 0 1))   ; ≈ 0.8814+1.5708i
(atanh (make-rectangular 0 2))   ; ≈ 0+1.1071i
```

Quaternion trig is handled numerically using the complex-plane projection formula — see the next section.

## Quaternion symbolic variables and non-commutative products

The `'quaternion` assumption on a sym-var changes how `*` works: the product becomes an ordered, non-commutative node (`nc*`) rather than the flat commutative bag used for real-valued expressions.

```scheme
(define q (sym-var 'q 'quaternion))
(define p (sym-var 'p 'quaternion))

(* q p)                 ; => (nc* q p)
(* p q)                 ; => (nc* p q)
(equal? (* q p) (* p q)) ; => #f  — order is preserved

; Real scalars always commute out to a leading coefficient
(equal? (* 2 q) (* q 2)) ; => #t  — both give (nc* 2 q)
(* 3 q p)               ; => (nc* 3 q p)
(* q 3 p)               ; => (nc* 3 q p)  — same thing
```

**Differentiation** applies the ordered product rule — the derivative of each factor is inserted in place, with all other factors maintaining their left-to-right order:

```scheme
(∂ (* q p) q)    ; => p          — (∂q/∂q)·p = 1·p = p
(∂ (* p q) q)    ; => p          — p·(∂q/∂q) = p·1 = p
(∂ (* q p) p)    ; => q          — q·(∂p/∂p) = q·1 = q
(∂ (* q q) q)    ; => (+ q q)   — two terms; each position contributes once
(∂ (* q p q) q)  ; => (+ (nc* p q) (nc* q p))   — positions 0 and 2 contribute
(∂ (* q p q) p)  ; => (nc* q q)                  — only position 1 contributes
```

**`expand`** distributes over sums preserving order, so `(q+p)²` yields four terms rather than three:

```scheme
(expand (* (+ q p) (+ q p)))
; => (+ (nc* q q) (nc* q p) (nc* p q) (nc* p p))
; q*p and p*q are distinct — they do NOT cancel or combine
```

**Concrete quaternion values** fold correctly within NC products: if `qi = 0+1i+0j+0k` and `qj = 0+0i+1j+0k`, then `(* qi qj)` evaluates to `qk = 0+0i+0j+1k` (the Hamilton product), while `(* qj qi)` gives `-qk`.

**`substitute`** maintains factor order:

```scheme
(define qi (make-quaternion 0 1 0 0))
(substitute (* q p) q qi)   ; => (nc* 0+1i+0j+0k p)  — qi in left position
(substitute (* p q) q qi)   ; => (nc* p 0+1i+0j+0k)  — qi in right position
```

**Numeric quaternion transcendentals** work directly on concrete quaternion values — these are not symbolic operations but numeric computations. Every quaternion `q = a + v̂·‖v‖` is embedded in the complex plane spanned by `{1, v̂}`:

```scheme
(define q (make-quaternion 1.0 2.0 3.0 4.0))
(sin  q)    ; => quaternion: sin(a)·cosh(‖v‖) + v̂·cos(a)·sinh(‖v‖)
(cos  q)    ; => quaternion: cos(a)·cosh(‖v‖) − v̂·sin(a)·sinh(‖v‖)
(exp  q)    ; => quaternion: eᵃ·(cos‖v‖ + v̂·sin‖v‖)
(log  q)    ; => quaternion: ln‖q‖ + v̂·arccos(a/‖q‖)
(sqrt q)    ; => quaternion: √((‖q‖+a)/2) + v̂·√((‖q‖−a)/2)

; Euler identity holds on every pure-imaginary axis:
(define pi 3.141592653589793)
(exp (make-quaternion 0.0 pi 0.0 0.0))   ; ≈ −1  (i-axis)
(exp (make-quaternion 0.0 0.0 pi 0.0))   ; ≈ −1  (j-axis)
(exp (make-quaternion 0.0 0.0 0.0 pi))   ; ≈ −1  (k-axis)

; Pythagorean identity:
(abs (- (+ (* (sin q) (sin q)) (* (cos q) (cos q)))
        (make-quaternion 1.0 0.0 0.0 0.0)))  ; < 1e-10
```

`abs` of a quaternion returns its Euclidean norm `√(a²+b²+c²+d²)`.

## Simplification

`(simplify expr)` applies algebraic identities to produce a simpler equivalent expression. It performs:

- **Constant folding** — evaluates any all-numeric subexpression
- **Flattening** — `(+ (+ a b) c)` → `(+ a b c)`, same for `*`
- **Numeric coefficient collection** — `(* 2 3 x)` → `(* 6 x)`
- **Additive identity** — removes `0` from sums
- **Multiplicative identity** — removes `1` from products
- **Multiplicative zero** — `(* 0 anything)` → `0`
- **Negation** — `(- (- x))` → `x`

```scheme
(simplify '(+ 0 x))           ; => x
(simplify '(* 1 x))           ; => x
(simplify '(* 0 x))           ; => 0
(simplify '(+ 1 2 x))         ; => (+ 3 x)
(simplify '(* 2 3 x))         ; => (* 6 x)
(simplify '(+ (+ a b) c))     ; => (+ a b c)
(simplify '(* (* a b) c))     ; => (* a b c)
```

`simplify` is called automatically by `∂`, `∫`, and `substitute` on their results.

## Symbolic differentiation

`(∂ expr var)` differentiates `expr` with respect to `var`. The result is automatically simplified.

The `∂` character is U+2202 PARTIAL DIFFERENTIAL (entered directly or via an editor shortcut). The ASCII alias is `sym-diff`:

```scheme
(∂ expr var)
(sym-diff expr var)     ; same thing
```

### Rules implemented

| Pattern | Result |
|---------|--------|
| `∂(c)/∂x` where c is constant | `0` |
| `∂(x)/∂x` | `1` |
| `∂(y)/∂x` where y ≠ x | `0` |
| `∂(u + v + ...)/∂x` | `∂u/∂x + ∂v/∂x + ...` |
| `∂(u - v)/∂x` | `∂u/∂x - ∂v/∂x` |
| `∂(-u)/∂x` | `-(∂u/∂x)` |
| `∂(u·v·...)/∂x` | Product rule (generalized to n factors) |
| `∂(u/v)/∂x` | `(v·∂u - u·∂v) / v²` (quotient rule) |
| `∂(uⁿ)/∂x` where n is constant | `n · u^(n-1) · ∂u/∂x` |
| `∂(uᵛ)/∂x` where v depends on x | `uᵛ · (v' ln u + v u'/u)` |
| `∂(sin u)/∂x` | `cos(u) · ∂u/∂x` |
| `∂(cos u)/∂x` | `-(sin(u)) · ∂u/∂x` |
| `∂(tan u)/∂x` | `∂u/∂x / cos²(u)` |
| `∂(exp u)/∂x` | `exp(u) · ∂u/∂x` |
| `∂(log u)/∂x` | `∂u/∂x / u` |
| `∂(sqrt u)/∂x` | `∂u/∂x / (2 sqrt(u))` |
| `∂(abs u)/∂x` | `sgn(u) · ∂u/∂x` |
| `∂(sinh u)/∂x` | `cosh(u) · ∂u/∂x` |
| `∂(cosh u)/∂x` | `sinh(u) · ∂u/∂x` |
| `∂(tanh u)/∂x` | `∂u/∂x / cosh²(u)` |
| `∂(asin u)/∂x` | `∂u/∂x / √(1−u²)` |
| `∂(acos u)/∂x` | `−∂u/∂x / √(1−u²)` |
| `∂(atan u)/∂x` | `∂u/∂x / (1+u²)` |
| `∂(asinh u)/∂x` | `∂u/∂x / √(u²+1)` |
| `∂(acosh u)/∂x` | `∂u/∂x / √(u²−1)` |
| `∂(atanh u)/∂x` | `∂u/∂x / (1−u²)` |
| `∂(cot u)/∂x` | `−∂u/∂x / sin²(u)` |
| `∂(sec u)/∂x` | `sec(u)·tan(u)·∂u/∂x` |
| `∂(csc u)/∂x` | `−csc(u)·cot(u)·∂u/∂x` |
| `∂(conj f)/∂x` | `conj(∂f/∂x)` (x real) |
| `∂(Re f)/∂x` | `Re(∂f/∂x)` (x real) |
| `∂(Im f)/∂x` | `Im(∂f/∂x)` (x real) |

For operators not in the table, `∂` returns the unevaluated form `(∂ expr var)`.

### Examples

```scheme
(symbolic x)

; Polynomial
(∂ (expt x 3) x)          ; => (* 3 (expt x 2))

; Product rule
(∂ (* x x) x)             ; => (* 2 x)

; Kinetic energy — derivative with respect to velocity
(symbolic m v)
(∂ (* 1/2 m (expt v 2)) v)   ; => (* m v)   (= momentum)

; Chain rule
(∂ (sin (* 2 x)) x)       ; => (* 2 (cos (* 2 x)))

; Exponential
(∂ (exp x) x)             ; => (exp x)

; Natural log
(∂ (log x) x)             ; => (/ 1 x)

; Composed
(∂ (expt (sin x) 2) x)    ; => (* 2 (sin x) (cos x))

; Quotient rule
(∂ (/ 1 x) x)             ; => (/ -1 (expt x 2))

; No-dependency
(symbolic y)
(∂ (expt y 2) x)           ; => 0
```

### Electromagnetism example

Coulomb potential energy U = k·q₁·q₂/r, force F = −∂U/∂r:

```scheme
(symbolic k q1 q2 r)
(define U (/ (* k q1 q2) r))
(define F (- (∂ U r)))
F   ; => (/ (* k q1 q2) (expt r 2))   (Coulomb's law)
```

## Symbolic integration

`(∫ expr var)` returns the antiderivative of `expr` with respect to `var`, without a constant of integration. The `∫` character is U+222B INTEGRAL. The ASCII alias is `integrate`:

```scheme
(∫ expr var)
(integrate expr var)    ; same thing
```

Definite integrals take two extra arguments for the bounds:

```scheme
(∫ expr var a b)        ; evaluates F(b) − F(a) where F = antiderivative
```

The bounds `a` and `b` may be any numeric type: fixnum, bignum, rational, flonum, or complex.

### Rules implemented

| Pattern | Antiderivative |
|---------|---------------|
| constant `c` | `c·x` |
| `x` | `x²/2` |
| `(expt x n)` n ≠ −1 | `x^(n+1)/(n+1)` |
| `(expt x -1)` or `(/ 1 x)` | `ln\|x\|` |
| `(expt (+ (* a x) b) n)` n ≠ −1 | `(ax+b)^(n+1) / (a(n+1))` |
| `(expt (+ (* a x) b) -1)` | `ln\|ax+b\| / a` |
| `(sin x)` | `-(cos x)` |
| `(cos x)` | `sin x` |
| `(tan x)` | `-(log(cos x))` |
| `(exp x)` | `exp x` |
| `(log x)` | `x·log(x) − x` |
| `(sqrt x)` | `(2/3)·x^(3/2)` |
| `(sinh (ax+b))` | `cosh(ax+b)/a` |
| `(cosh (ax+b))` | `sinh(ax+b)/a` |
| `(tanh (ax+b))` | `log(cosh(ax+b))/a` |
| `(cot (ax+b))` | `log\|sin(ax+b)\|/a` |
| `(sec (ax+b))` | `log\|sec(ax+b)+tan(ax+b)\|/a` |
| `(csc (ax+b))` | `−log\|csc(ax+b)+cot(ax+b)\|/a` |
| `(expt (sec (ax+b)) 2)` | `tan(ax+b)/a` |
| `(expt (csc (ax+b)) 2)` | `−cot(ax+b)/a` |
| `(asin (ax+b))` | `((ax+b)·asin(ax+b) + √(1−(ax+b)²))/a` (IBP) |
| `(acos (ax+b))` | `((ax+b)·acos(ax+b) − √(1−(ax+b)²))/a` (IBP) |
| `(atan (ax+b))` | `((ax+b)·atan(ax+b) − log(1+(ax+b)²)/2)/a` (IBP) |
| `(asinh (ax+b))` | `((ax+b)·asinh(ax+b) − √((ax+b)²+1))/a` |
| `(acosh (ax+b))` | `((ax+b)·acosh(ax+b) − √((ax+b)²−1))/a` |
| `(atanh (ax+b))` | `((ax+b)·atanh(ax+b) + log(1−(ax+b)²)/2)/a` |
| `(expt (sin (ax+b)) 2)` | `x/2 − sin(2(ax+b))/(4a)` (half-angle) |
| `(expt (cos (ax+b)) 2)` | `x/2 + sin(2(ax+b))/(4a)` (half-angle) |
| `(* (expt x n) (sin (ax+b)))` | IBP: `x^n·(−cos/a) − ∫n·x^(n−1)·(−cos/a) dx` |
| `(* (expt x n) (cos (ax+b)))` | IBP: `x^n·(sin/a) − ∫n·x^(n−1)·(sin/a) dx` |
| `(* (expt x n) (exp (ax+b)))` | IBP: `x^n·(exp/a) − ∫n·x^(n−1)·(exp/a) dx` |
| `(* (expt x n) (log (ax+b)))` | `x^(n+1)·ln(ax+b)/(n+1) − ∫x^(n+1)·a/((n+1)(ax+b)) dx` |
| `(/ c (+ (* a (expt x 2)) (* b x) d))` | `2c/√Δ · atan((2ax+b)/√Δ)` where Δ=4ad−b² (Δ>0) |
| `k·f(x)` | `k·∫f(x)dx` |
| `f(x) + g(x) + ...` | `∫f + ∫g + ...` |
| `conj(f(x))` | `conj(∫f(x)dx)` |
| `Re(f(x))` | `Re(∫f(x)dx)` |
| `Im(f(x))` | `Im(∫f(x)dx)` |

Unknown forms leave an unevaluated `(∫ expr var)` node.

### Examples

```scheme
(symbolic x)

(∫ (expt x 2) x)          ; => (/ (expt x 3) 3)
(∫ (sin x) x)             ; => (- (cos x))
(∫ (exp x) x)             ; => (exp x)
(∫ (/ 1 x) x)             ; => (log x)
(∫ (cos (* 3 x)) x)       ; => (/ (sin (* 3 x)) 3)

; Definite integral — numeric result
(∫ (expt x 2) x 0 1)      ; => 1/3
(∫ (sin x) x 0 3.14159)   ; => ~2.0

; Works with complex bounds
(∫ (* 2 x) x 0 1+i)       ; => (+ -1 (* 2 i))  [= (1+i)² - 0²]

; Roundtrip: ∂(∫f)/∂x = f
(symbolic x)
(define f (* 3 (expt x 2)))
(define F (∫ f x))         ; F = x³
(∂ F x)                    ; => (* 3 (expt x 2))   ✓

; Integration by parts: polynomial × trig/exp
(∫ (* x (sin x)) x)        ; => x·(−cos(x)) − (−sin(x))   i.e. −x·cos(x) + sin(x)
(∫ (* x (cos x)) x)        ; => x·sin(x) − (−cos(x))       i.e. x·sin(x) + cos(x)
(∫ (* x (exp x)) x)        ; => x·exp(x) − exp(x)

; IBP: polynomial × logarithm (LIATE rule — differentiate log)
(∫ (* x (log x)) x)        ; => x²·log(x)/2 − x²/4
(∫ (* (expt x 2) (log x)) x) ; => x³·log(x)/3 − x³/9

; Half-angle trig power reductions
(∫ (expt (sin x) 2) x)     ; => x/2 − sin(2x)/4
(∫ (expt (cos x) 2) x)     ; => x/2 + sin(2x)/4

; Quadratic denominator (positive discriminant → atan form)
(∫ (/ 1 (+ (expt x 2) 1)) x)   ; => atan(x)
(∫ (/ 1 (+ (expt x 2) 4)) x)   ; => (1/2)·atan(x/2)
(∫ (/ 1 (+ (expt x 2) (* 2 x) 2)) x)  ; => atan(x+1)  [completes the square]
```

## Limits

`(limit f x a)` computes the two-sided limit of `f` as `x → a`.
`(limit f x a 'left)` and `(limit f x a 'right)` compute one-sided limits.

```scheme
(symbolic x)

; Direct substitution
(limit (+ x 3) x 2)           ; => 5
(limit (sin x) x 0)            ; => 0.0

; L'Hôpital: 0/0 form
(limit (/ (sin x) x) x 0)     ; => 1
(limit (/ (- (exp x) 1) x) x 0)  ; => 1
(limit (/ (- 1 (cos x)) (expt x 2)) x 0)  ; => 0.5  (1/2)

; L'Hôpital applied three times
(limit (/ (- x (sin x)) (expt x 3)) x 0)  ; => 0.166667  (1/6)

; Factoring at removable discontinuity
(limit (/ (- (expt x 2) 1) (- x 1)) x 1)  ; => 2

; Infinity
(limit (/ 1 x) x +inf.0)      ; => 0

; One-sided limits
(limit (/ 1 x) x 0.0 'right)  ; => +inf.0
```

; Exotic indeterminate forms
(symbolic x)

; 0·∞ — rewritten as a ratio, then L'Hôpital
(limit (* x (log x)) x 0.0 'right)          ; => 0

; 0^0, ∞^0, 1^∞ — rewritten as exp(g·log(f))
(limit (expt x x) x 0.0 'right)             ; => 1   (0^0)
(limit (expt x (/ 1 x)) x +inf.0)           ; => 1   (∞^0)
(limit (expt (+ 1 (/ 1 x)) x) x +inf.0)    ; => e   (1^∞)
```

### Algorithm

1. **Direct substitution** — substitutes `a` into `f` and simplifies. Returns if numeric and not NaN.
2. **L'Hôpital** — when `f = p/q` and both `p(a) = 0` and `q(a) = 0` (or both ±∞), differentiates numerator and denominator, simplifies the ratio, and retries. Iterates up to 5 times.
3. **0·∞** — for a product `f·g` where one factor → 0 and the other → ∞, rewrites as `f/(1/g)` or `g/(1/f)` and applies L'Hôpital.
4. **Indeterminate powers** — `1^∞`, `0^0`, `∞^0` forms rewrite `f^g` as `exp(g·log(f))`, take the limit of the exponent, then return `exp(lim_exp)`.
5. **Infinity** — handles `finite/∞ = 0` directly.
6. **Fallback** — returns an unevaluated `(limit f x a)` node.

## Taylor series

`(series f x a n)` expands `f` around the point `a` to order `n`, returning the truncated Taylor series as a symbolic sum:

```
Σ_{k=0}^{n} f^(k)(a)/k! · (x − a)^k
```

Zero-coefficient terms are dropped.  When `a` is an exact integer (e.g. `0`) and `f^(k)(a)` evaluates to an integer, the coefficients are **exact rationals** (`1/2`, `1/6`, `1/24`, …).

```scheme
(symbolic x)

(series (exp x) x 0 4)
; => (+ 1 x (* 1/2 (expt x 2)) (* 1/6 (expt x 3)) (* 1/24 (expt x 4)))

(series (sin x) x 0 5)
; => (+ x (* -1/6 (expt x 3)) (* 1/120 (expt x 5)))

(series (cos x) x 0 4)
; => (+ 1 (* -1/2 (expt x 2)) (* 1/24 (expt x 4)))

(series (log (+ 1 x)) x 0 4)
; => (+ x (* -1/2 (expt x 2)) (* 1/3 (expt x 3)) (* -1/4 (expt x 4)))

; Expansion around a non-zero point
(series (exp x) x 1 3)
; => e + e·(x−1) + e/2·(x−1)² + e/6·(x−1)³   (flonum coefficients)
```

The result is a plain symbolic expression — you can feed it directly to `simplify`, `substitute`, `∂`, `sym->infix`, or `sym->latex`:

```scheme
(sym->latex (series (sin x) x 0 5))
; => x - \frac{1}{6} x^{3} + \frac{1}{120} x^{5}

; Numerically evaluate at a point
(substitute (series (exp x) x 0 6) x 1)
; => 163/60   (= 1+1+1/2+1/6+1/24+1/120+1/720, exact)

; Differentiate the series — recovers the series of the derivative
(simplify (∂ (series (sin x) x 0 5) x))
; => (+ 1 (* -1/2 (expt x 2)) (* 1/24 (expt x 4)))  — matches (series (cos x) x 0 4)
```

### Signature

| Argument | Type | Description |
|---|---|---|
| `f` | any symbolic or numeric expression | Function to expand |
| `x` | sym-var | Expansion variable |
| `a` | number or symbolic | Expansion point |
| `n` | exact non-negative integer | Maximum order |

## Vector calculus (Cartesian)

The vector calculus operators work on **Scheme lists of symbolic expressions** representing components of a scalar or vector field. Variables are passed as a list of symbolic variables in order `(x y z ...)`.

```scheme
(symbolic x y z)
(define vars (list x y z))
```

### `grad` / `gradient`

`(grad f vars)` — gradient of a scalar field `f`. Returns a list `(∂f/∂x₁ ∂f/∂x₂ ...)`.

```scheme
(grad (+ (expt x 2) (expt y 2) (expt z 2)) vars)
; => ((* 2 x)  (* 2 y)  (* 2 z))
```

### `divergence`

`(divergence F vars)` — divergence `∑ ∂Fᵢ/∂xᵢ` of a vector field `F` (list of components). Returns a scalar expression.

```scheme
(divergence (list (expt x 2) (expt y 2) (expt z 2)) vars)
; => (+ (* 2 x) (* 2 y) (* 2 z))
```

### `curl`

`(curl F vars)` — curl of a **3-D** vector field. Returns a list of 3 components:
`(∂Fz/∂y − ∂Fy/∂z,  ∂Fx/∂z − ∂Fz/∂x,  ∂Fy/∂x − ∂Fx/∂y)`.

```scheme
; Conservative field — curl is zero
(curl (list (* y z) (* x z) (* x y)) vars)  ; => (0 0 0)

; Uniform rotation about z-axis
(curl (list (- y) x 0) vars)   ; => (0 0 2)
```

### `laplacian`

`(laplacian f vars)` — scalar Laplacian `∑ ∂²f/∂xᵢ²`. N-dimensional (works for any number of variables).

```scheme
(laplacian (+ (expt x 2) (expt y 2) (expt z 2)) vars)  ; => 6
```

### `vec-laplacian`

`(vec-laplacian F vars)` — vector Laplacian: apply `laplacian` component-wise to a vector field.

### `dot-product` / `cross-product`

```scheme
(dot-product (list x y z) (list x y z))   ; => x²+y²+z²

(cross-product '(1 0 0) '(0 1 0))         ; => (0 0 1)
```

### Vector calculus identities

The following identities hold symbolically:

```scheme
; div(curl(F)) = 0 for any F
(divergence (curl F vars) vars)   ; => 0

; curl(grad(f)) = (0 0 0) for any f
(curl (grad f vars) vars)         ; => (0 0 0)
```

### Verifying Maxwell's equations

A plane wave `E = (0, sin(kx−ωt), 0)`, `B = (0, 0, sin(kx−ωt))` satisfies all four Maxwell equations in vacuum (c=1 units):

```scheme
(symbolic x y z t)
(define xyz (list x y z))
(define arg (- x t))           ; kx − ωt with k=ω=1
(define E (list 0 (sin arg) 0))
(define B (list 0 0 (sin arg)))

; Gauss: div(E) = 0
(divergence E xyz)             ; => 0

; No monopoles: div(B) = 0
(divergence B xyz)             ; => 0

; Faraday: curl(E) + ∂B/∂t = 0
(map + (curl E xyz) (map (lambda (b) (∂ b t)) B))   ; => (0 0 0)

; Ampere-Maxwell: curl(B) − ∂E/∂t = 0  (c=1, μ₀ε₀=1)
(map - (curl B xyz) (map (lambda (e) (∂ e t)) E))   ; => (0 0 0)
```

## Substitution and numeric evaluation

`(substitute expr var val)` replaces all occurrences of `var` in `expr` with `val`, then simplifies. `val` may be a number or another symbolic expression.

```scheme
(symbolic x)

(substitute (expt x 2) x 3)         ; => 9
(substitute (sin x) x 0)             ; => 0.0
(substitute (+ x 1) x 10)            ; => 11

; Substitute a symbolic value for another
(symbolic y)
(substitute (* x x) x (+ y 1))      ; => (* (+ y 1) (+ y 1))
```

To evaluate a symbolic expression at a numeric point, convert it to a procedure:

```scheme
(symbolic x)
(define expr (+ (expt x 3) (* -2 x) 1))
(define (eval-at v) (substitute expr x v))

(eval-at 0)      ; => 1
(eval-at 2)      ; => 5
(eval-at -1)     ; => 4
```

## Polynomial / structural operations

Four procedures treat symbolic expressions as polynomials in a single variable.

### `expand`

Distributes `*` over `+` and expands integer powers (2..16):

```scheme
(symbolic x y)

(expand (* (+ x 1) (+ x 2)))     ; => (+ (* x x) (* 3 x) 2)   — x²+3x+2
(expand (expt (+ x 1) 2))        ; => (+ (* x x) (* 2 x) 1)   — x²+2x+1
(expand (expt (+ x 1) 3))        ; => (+ (* x x x) (* 3 (* x x)) (* 3 x) 1)
(expand (* (+ x y) (- x y)))     ; => (+ (* x x) (neg (* y y))) — x²-y²
(expand (- (+ x y)))             ; => (+ (neg x) (neg y))
```

Exponents outside the range 2..16, or symbolic exponents, are left as-is; nested multiplications are always distributed.

### `degree`

Returns the polynomial degree of an expression in a given variable (as an exact fixnum):

```scheme
(symbolic x y)

(degree 5 x)                          ; => 0
(degree x x)                          ; => 1
(degree y x)                          ; => 0
(degree (expt x 3) x)                 ; => 3
(degree (+ (expt x 2) (* 3 x) 1) x)  ; => 2
(degree (* (+ x 1) (+ x 2)) x)       ; => 2   — degree of the product
```

For a product, degree is the sum of degrees of factors; for a sum, it is the maximum. Transcendental functions of `x` contribute degree 0.

### `leading-coeff`

Returns the coefficient of the highest-degree term (internally expands first):

```scheme
(symbolic x)

(leading-coeff x x)                          ; => 1
(leading-coeff (* 3 x) x)                    ; => 3
(leading-coeff (+ (expt x 2) (* 5 x) 7) x)  ; => 1
(leading-coeff (+ (* 2 (expt x 3)) x) x)    ; => 2
(leading-coeff (* (+ x 1) (+ x 2)) x)       ; => 1
```

### `collect`

Groups like-degree terms together after expanding, combining their coefficients. Produces a canonical sum sorted by descending degree:

```scheme
(symbolic x)

(collect (+ (* 2 x) (* 3 x)) x)
; => (* 5 x)

(collect (+ (expt x 2) (* 2 x) (expt x 2) x) x)
; => (+ (* 2 (expt x 2)) (* 3 x))   — 2x²+3x

(collect (expand (expt (+ x 1) 2)) x)
; => (+ (expt x 2) (* 2 x) 1)       — x²+2x+1, with explicit expt
```

Terms that cannot be identified as monomials in the given variable are left at the end of the sum unchanged.

### Typical polynomial workflow

```scheme
(symbolic x)

; Start with a factored form
(define p (* (+ x 1) (+ x 2) (- x 3)))

; Expand to sum-of-monomials
(define expanded (expand p))           ; x³-7x+6  (with repeated-factor notation)

; Inspect polynomial properties
(degree expanded x)                    ; => 3
(leading-coeff expanded x)             ; => 1

; Collect into canonical form
(define canonical (collect expanded x)) ; (+ (expt x 3) (* -7 x) 6)

; Evaluate
(substitute canonical x 2)             ; => -4   (2³-14+6)

; Differentiate the collected polynomial
(simplify (∂ canonical x))             ; 3x²-7
```

## Complex operators

The symbolic system understands complex conjugates and real/imaginary part extraction as first-class operators. When the argument is a concrete number the result is numeric; when symbolic, a symbolic expression tree is returned.

```scheme
(conj expr)           ; complex conjugate — also (conjugate expr)
(real-part expr)      ; Re(expr)
(imag-part expr)      ; Im(expr)
```

When `expr` is symbolic, these build expression nodes:

```scheme
(symbolic z)
(conj z)              ; => (conj z)
(real-part z)         ; => (real-part z)
(imag-part z)         ; => (imag-part z)
(conj (+ z 1))        ; => (conj (+ z 1))
(real-part (* 2 z))   ; => (real-part (* 2 z))
```

### Simplification identities

The simplifier automatically applies these:

| Input | Simplified |
|-------|-----------|
| `(conj (conj f))` | `f` |
| `(conj (real-part f))` | `(real-part f)` |
| `(conj (imag-part f))` | `(imag-part f)` |
| `(real-part (conj f))` | `(real-part f)` |
| `(real-part (real-part f))` | `(real-part f)` |
| `(real-part (imag-part f))` | `(imag-part f)` |
| `(imag-part (conj f))` | `(- (imag-part f))` |
| `(imag-part (real-part f))` | `0` |
| `(imag-part (imag-part f))` | `0` |

### Differentiation of complex operators

When the variable of differentiation is a real variable `x`:

```scheme
(symbolic x)
(define f (sin x))

(∂ (conj f) x)        ; => (conj (cos x))        — conj passes through ∂
(∂ (real-part f) x)   ; => (real-part (cos x))
(∂ (imag-part f) x)   ; => (imag-part (cos x))
```

Integration obeys the same rule — `conj`, `real-part`, and `imag-part` pass through `∫`.

## Wirtinger calculus

Standard calculus treats only real variables. For complex analysis you need the **Wirtinger derivatives** ∂/∂z and ∂/∂z̄, which treat `z` and `conj(z)` as independent:

```scheme
(wirtinger-d    expr z)   ; ∂/∂z
(wirtinger-dbar expr z)   ; ∂/∂z̄
```

### Key rules

| Rule | Result |
|------|--------|
| `∂z/∂z` | `1` |
| `∂conj(z)/∂z` | `0` |
| `∂z/∂z̄` | `0` |
| `∂conj(z)/∂z̄` | `1` |
| `∂conj(f)/∂z` | `conj(∂f/∂z̄)` — the cross rule |
| `∂Re(f)/∂z` | `½(∂f/∂z + conj(∂f/∂z̄))` |
| `∂Im(f)/∂z` | `(∂f/∂z − conj(∂f/∂z̄))/(2i)` |

All standard arithmetic and holomorphic transcendentals follow the chain rule. A function is holomorphic iff `(wirtinger-dbar f z)` simplifies to `0`.

### Holomorphicity test

```scheme
(symbolic z)

; z² is holomorphic
(wirtinger-dbar (expt z 2) z)   ; => 0   ✓

; conj(z) is anti-holomorphic
(wirtinger-dbar (conj z) z)     ; => 0   — ∂conj(z)/∂z̄ = 1 ≠ 0 means ∂/∂z̄ ≠ 0
(wirtinger-d    (conj z) z)     ; => 0   — holomorphic part is 0

; |z|² = z·conj(z) is not holomorphic
(wirtinger-dbar (* z (conj z)) z)   ; => z   (non-zero ⟹ not holomorphic)

; exp(z) is holomorphic
(wirtinger-dbar (exp z) z)      ; => 0   ✓

; Holomorphic derivative equals ordinary derivative for holomorphic functions
(wirtinger-d (expt z 3) z)      ; => (* 3 (expt z 2))
```

### Cauchy-Riemann equations

For a function f(z) = u(x,y) + i·v(x,y) written in terms of real part `u` and imaginary part `v`, the Wirtinger approach gives:

```scheme
(symbolic z)
(define f (expt z 2))   ; f(z) = z² = (x²-y²) + 2xyi

; ∂f/∂z̄ = 0 ⟺ Cauchy-Riemann conditions hold
(wirtinger-dbar f z)    ; => 0   — z² is holomorphic ✓
```

## Auto-differentiation

`(auto-diff f x)` evaluates `f(x + ε)` using dual-number surreals and extracts the ε coefficient, which equals f′(x) exactly (no finite-difference approximation error). It works for any algebraic lambda:

```scheme
(auto-diff (lambda (x) (* x x)) 3)            ; => 6    [2x at x=3]
(auto-diff (lambda (x) (expt x 3)) 4)         ; => 48   [3x² at x=4]
(auto-diff (lambda (t) (+ (* t t) t)) 2)      ; => 5    [2t+1 at t=2]
(auto-diff (lambda (x) (/ 1 x)) 2)            ; => -1/4 [−1/x² at x=2]
```

Note: C-level primitives such as `sin`, `cos`, and `exp` do not propagate surreals. For transcendental derivatives use symbolic differentiation instead.

### Combining auto-diff and symbolic CAS

You can use symbolic differentiation to produce a derivative expression, then compile it to a fast numeric lambda:

```scheme
(symbolic x)
(define f-expr (+ (expt x 4) (* -3 (expt x 2)) 1))
(define df-expr (∂ f-expr x))   ; => (+ (* 4 (expt x 3)) (* -6 x))

; Compile to a lambda for fast numeric evaluation
(define (f  xv) (substitute f-expr  x xv))
(define (df xv) (substitute df-expr x xv))

(f  2)    ; => 5
(df 2)    ; => 20   [4x³ − 6x at x=2]
```

## Fractional calculus

Curry extends the symbolic CAS with **fractional derivatives and integrals**, generalising the standard differential operators to non-integer order α. This is useful in viscoelasticity, anomalous diffusion, signal processing, and control theory.

### Symbolic fractional operators

`(frac-diff expr α var)` returns a symbolic expression for the **Caputo fractional derivative** D^α of `expr` with respect to `var`. `(frac-int expr α var)` returns a symbolic expression for the **Riemann-Liouville fractional integral** I^α.

The results are expression trees: they can be further differentiated, integrated, simplified, or have variables substituted:

```scheme
(symbolic x)

(frac-diff (expt x 2) 1/2 x)    ; => (frac-diff (expt x 2) 1/2 x)
(frac-int  (expt x 2) 1/2 x)    ; => (frac-int  (expt x 2) 1/2 x)

; Compose symbolic operations
(∂ (frac-int (expt x 3) 1/2 x) x)   ; differentiates the I^(1/2) node
(simplify (frac-diff x 1 x))         ; D^1 of x = 1
```

### Numerical fractional operators

For concrete functions, numerical approximations are available:

`(quad-frac-diff f α x)` computes the **Grünwald-Letnikov** fractional derivative of the callable `f` at point `x` for order `α` (may be non-integer), using a finite-difference sum with adaptive step size.

`(quad-frac-int f α x)` computes the **Riemann-Liouville** fractional integral of `f` at `x` numerically.

```scheme
; D^(1/2) of x² at x=1 ≈ 2/√π ≈ 1.128
(quad-frac-diff (lambda (x) (* x x)) 1/2 1.0)    ; ≈ 1.128

; I^(1/2) of 1 at x=1 ≈ 2/√π ≈ 1.128
(quad-frac-int  (lambda (x) 1.0)     1/2 1.0)    ; ≈ 1.128

; Non-integer order
(quad-frac-diff (lambda (x) (exp x)) 0.7 1.0)
```

### General numerical quadrature

`(quad f a b)` computes the definite integral of `f` from `a` to `b` using **Gauss-Kronrod G7K15** adaptive quadrature. It is more accurate than Simpson's rule for smooth functions and handles mild endpoint singularities:

```scheme
(quad (lambda (x) (* x x)) 0 1)               ; => 1/3  (exact to double precision)
(quad (lambda (x) (sin x)) 0 3.14159)         ; ≈ 2.0
(quad (lambda (x) (exp (- (* x x)))) 0 10)   ; ≈ 0.8862...  (half of √π)
```

`quad` works for any callable `f`; it does not require symbolic expressions. Use it when a closed-form antiderivative does not exist or when the integrand is only available as data.

## Output formatting

By default, symbolic expressions display in **Scheme prefix notation** — the same form used internally. Two additional renderers convert to human-readable forms.

### Infix notation — `sym->string` / `sym->infix`

Both names are aliases for the same function. Returns a string with standard algebraic infix notation, with operator precedence handled automatically:

```scheme
(symbolic x)

(sym->string (+ x 1))                     ; => "x + 1"
(sym->string (* 3 (expt x 2)))            ; => "3 * x^2"
(sym->string (+ (expt x 2) (* -2 x) 1))  ; => "x^2 - 2 * x + 1"
(sym->string (/ 1 (+ x 1)))              ; => "1 / (x + 1)"
(sym->string (sin (* 2 x)))              ; => "sin(2 * x)"
(sym->string (∂ (expt x 2) x))           ; => "2 * x"
```

Subexpressions are parenthesised only when needed. Subtraction of negative terms is shown with `−` rather than `+ (−...)`.

### LaTeX notation — `sym->latex`

Returns a LaTeX string suitable for embedding in `$...$` or `$$...$$` math mode:

```scheme
(symbolic x)

(sym->latex (expt x 2))                   ; => "x^{2}"
(sym->latex (+ (expt x 2) (* -2 x) 1))   ; => "x^{2} - 2 x + 1"
(sym->latex (/ 1 (+ x 1)))               ; => "\\frac{1}{x + 1}"
(sym->latex (sqrt x))                     ; => "\\sqrt{x}"
(sym->latex (sin x))                      ; => "\\sin\\!\\left(x\\right)"
(sym->latex (exp (* 2 x)))               ; => "e^{2 x}"
(sym->latex (log x))                      ; => "\\ln\\!\\left(x\\right)"
```

Rational numbers render as `\frac{p}{q}`. Variable names that match Greek letters render as LaTeX commands:

```scheme
(symbolic alpha beta gamma omega)

(sym->latex (+ alpha beta))               ; => "\\alpha + \\beta"
(sym->latex (* gamma (expt omega 2)))     ; => "\\gamma \\omega^{2}"
```

Complex operators use standard mathematical notation:

```scheme
(symbolic z)

(sym->latex (conj z))                     ; => "\\overline{z}"
(sym->latex (real-part z))               ; => "\\operatorname{Re}\\!\\left(z\\right)"
(sym->latex (imag-part z))               ; => "\\operatorname{Im}\\!\\left(z\\right)"
```

### Workflow: symbolic derivation to LaTeX

```scheme
(symbolic x)
(define f   (+ (expt x 4) (* -3 (expt x 2)) 1))
(define df  (∂ f x))
(define ddf (∂ df x))

(display (sym->latex f))    ; x^{4} - 3 x^{2} + 1
(display (sym->latex df))   ; 4 x^{3} - 6 x
(display (sym->latex ddf))  ; 12 x^{2} - 6
```

## Printing symbolic values

Symbolic expressions display in standard Scheme prefix notation:

```scheme
(symbolic x)
(display (∂ (* x x) x))     ; prints: (* 2 x)
(display (∂ (sin x) x))     ; prints: (cos x)
(display (∂ (/ 1 x) x))     ; prints: (/ -1 (expt x 2))
(display (∫ (expt x 2) x))  ; prints: (/ (expt x 3) 3)
```

`write` and `display` both use prefix notation. Negation is shown as `(- x)` rather than `(neg x)`.

## Quick-reference table

| Procedure | Description |
|-----------|-------------|
| `(symbolic v ...)` | Declare symbolic variables |
| `(sym-var? v)` | Is v a symbolic variable? |
| `(sym-expr? v)` | Is v a symbolic expression? |
| `(symbolic? v)` | Is v symbolic (var or expr)? |
| `(sym-var-name v)` | Variable name as string |
| `(sym-assumption? v flag)` | Does sym-var `v` carry assumption `flag`? Flags: `real`, `positive`, `negative`, `integer`, `nonzero`, `quaternion` |
| `(∂ expr var)` | Differentiate expr w.r.t. var |
| `(sym-diff expr var)` | ASCII alias for ∂ |
| `(∫ expr var)` | Indefinite integral of expr w.r.t. var |
| `(∫ expr var a b)` | Definite integral from a to b |
| `(integrate expr var)` | ASCII alias for ∫ |
| `(simplify expr)` | Algebraic simplification |
| `(substitute expr var val)` | Replace var with val |
| `(conj expr)` / `(conjugate expr)` | Complex conjugate |
| `(real-part expr)` | Real part (symbolic-aware) |
| `(imag-part expr)` | Imaginary part (symbolic-aware) |
| `(wirtinger-d expr z)` | Wirtinger ∂/∂z |
| `(wirtinger-dbar expr z)` | Wirtinger ∂/∂z̄ |
| `(auto-diff f x)` | Automatic differentiation at point x |
| `(frac-diff expr α var)` | Caputo symbolic fractional derivative D^α |
| `(frac-int expr α var)` | Riemann-Liouville symbolic fractional integral I^α |
| `(quad-frac-diff f α x)` | Grünwald-Letnikov numerical D^α (for concrete f) |
| `(quad-frac-int f α x)` | Numerical Riemann-Liouville fractional integral |
| `(quad f a b)` | Gauss-Kronrod G7K15 adaptive numerical quadrature |
| `(expand expr)` | Distribute `*` over `+`; expand integer powers |
| `(degree expr var)` | Polynomial degree in var (exact fixnum) |
| `(leading-coeff expr var)` | Coefficient of highest-degree term |
| `(collect expr var)` | Group like-degree terms; canonical descending form |
| `(limit f x a)` | Two-sided limit of `f` as `x → a` |
| `(limit f x a 'left)` | One-sided limit x→a⁻ |
| `(limit f x a 'right)` | One-sided limit x→a⁺ |
| `(series f x a n)` | Truncated Taylor series of `f` around `a` to order `n` |
| `(grad f vars)` / `(gradient f vars)` | Gradient of scalar field — list of ∂f/∂xᵢ |
| `(divergence F vars)` | Divergence of vector field — scalar |
| `(curl F vars)` | Curl of 3-D vector field — 3-component list |
| `(laplacian f vars)` | Scalar Laplacian ∑ ∂²f/∂xᵢ² |
| `(vec-laplacian F vars)` | Vector Laplacian (component-wise) |
| `(dot-product A B)` | Symbolic dot product |
| `(cross-product A B)` | Symbolic 3-D cross product |
| `(sym->string expr)` / `(sym->infix expr)` | Infix string: `x^2 - 2*x + 1` |
| `(sym->latex expr)` | LaTeX string: `x^{2} - 2 x + 1` |

All standard numeric operators lift automatically over symbolic values: `+` `-` `*` `/` `expt` `sqrt` `abs` `exp` `log` `sin` `cos` `tan` `sinh` `cosh` `tanh` `asin` `acos` `atan` `asinh` `acosh` `atanh` `cot` `sec` `csc`.

## Physics in non-integer dimensions

The symbolic system combines naturally with the fractional-calculus and dimensional machinery. The Laplacian of a scalar field in D spatial dimensions involves `(∂/∂r)(rᴰ⁻¹ ∂f/∂r)`, computable symbolically:

```scheme
(symbolic f r D)
(define laplacian-1d
  (* (expt r (- 1 D))
     (∂ (* (expt r (- D 1)) (∂ f r)) r)))
(simplify laplacian-1d)
; => symbolic expression for the radial Laplacian in D dimensions
```

## Graphing with Qt6

The symbolic CAS pairs naturally with the Qt6 module for interactive visualization. The pattern is:

1. Declare a symbolic variable and build expressions for f, f′, and ∫f.
2. Compile each expression to a numeric lambda using `substitute`.
3. In the `canvas-on-draw!` callback, sample the lambda across the visible x range and connect the samples with `gfx-draw-line!`.
4. Use sidebar sliders to adjust the view range and let `canvas-redraw!` trigger a repaint.

See `examples/symbolic-grapher.scm` for a complete runnable example.

```bash
./build/curry examples/symbolic-grapher.scm
```

The grapher displays:
- The selected function f(x) in white
- Its symbolic derivative f′(x) in cyan
- Its symbolic antiderivative F(x) in orange
- The symbolic expressions for f′ and F in sidebar labels
- Sliders for the x range and y scale
- A dropdown to switch between preset functions
