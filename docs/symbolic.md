# Symbolic Expressions — Computer Algebra in Curry

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
```

Nested expressions are built automatically:

```scheme
(symbolic x)
(define ke (* 1/2 m (expt v 2)))   ; kinetic energy
ke   ; => (* 1/2 m (expt v 2))
```

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

`simplify` is called automatically by `∂` and `substitute` on their results.

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
| `∂(tan u)/∂x` | `(1 + tan²(u)) · ∂u/∂x` |
| `∂(exp u)/∂x` | `exp(u) · ∂u/∂x` |
| `∂(log u)/∂x` | `∂u/∂x / u` |
| `∂(sqrt u)/∂x` | `∂u/∂x / (2 sqrt(u))` |
| `∂(abs u)/∂x` | `sgn(u) · ∂u/∂x` |

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
(∂ (* 1/2 m (expt v 2)) v)   ; => (* m v)   (= momentum!)

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

## Substitution

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

## Printing symbolic values

Symbolic expressions display in standard Scheme prefix notation:

```scheme
(symbolic x)
(display (∂ (* x x) x))     ; prints: (* 2 x)
(display (∂ (sin x) x))     ; prints: (cos x)
(display (∂ (/ 1 x) x))     ; prints: (/ -1 (expt x 2))
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
| `(∂ expr var)` | Differentiate expr w.r.t. var |
| `(sym-diff expr var)` | ASCII alias for ∂ |
| `(simplify expr)` | Algebraic simplification |
| `(substitute expr var val)` | Replace var with val |

All standard numeric operators (`+` `-` `*` `/` `expt` `sqrt` `sin` `cos` `tan` `exp` `log` `abs`) lift automatically over symbolic values.

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
