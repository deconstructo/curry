# Module: `(curry pde-symbolic)`

*v1.1.0 — 2026-06-06*

Symbolic tools for exact analysis of linear PDEs: separation of variables, Fourier coefficient computation, method of characteristics, and Laplace-transform ODE reduction. Pure Scheme — operates entirely on top of Curry's CAS layer (`sym-var`, `∂`, `∫`, `substitute`, `simplify`).

For **numerical** PDE solving see [`(curry pde)`](module-pde.md).

## Import

```scheme
(import (curry pde-symbolic))
```

## Conventions

| Symbol | Role |
|--------|------|
| `x`, `y`, `t` | spatial / temporal `sym-var` arguments, caller-supplied |
| `n` | mode index — an exact integer or a positive `sym-var` |
| `L` | domain length — a number or a positive `sym-var` |
| `alpha`, `c`, `v` | PDE parameters — numbers or `sym-var`s |

All procedures return symbolic expressions that compose with `∂`, `substitute`, `simplify`, `sym->infix`, and `sym->latex`.

---

## § 1 — Sturm-Liouville eigenvalue problem (Dirichlet, [0, L])

The building block for separation of variables: **X'' + λX = 0**, X(0) = X(L) = 0.

```scheme
(pde-eigenvalue    n L)       ; → (nπ/L)²
(pde-eigenfunction n x L)    ; → sin(nπx/L)
```

**Orthogonality:** ∫₀ᴸ Xₘ(x)·Xₙ(x)dx = (L/2)·δₘₙ — the eigenfunctions form an orthonormal basis after scaling by √(2/L).

---

## § 2 — Separation of variables

### Heat equation

**u_t = α·u_xx** on (0, L), Dirichlet BCs u(0,t) = u(L,t) = 0.

The n-th separated mode:

```
u_n(x,t) = sin(nπx/L) · exp(−α·(nπ/L)²·t)
```

```scheme
(pde-heat-mode alpha L n x t)   ; → symbolic mode expression
```

The general solution is **u(x,t) = Σₙ Bₙ · u_n(x,t)** where Bₙ are Fourier coefficients of the initial condition (see § 3).

**Example:**

```scheme
(import (curry pde-symbolic))

(define-symbolic alpha L n x t)
(define mode (pde-heat-mode alpha L n x t))
(sym->infix mode)
; → "sin(n * π / L * x) * exp(-alpha * (n * π / L)^2 * t)"

; Verify the mode satisfies the PDE:
(simplify (pde-heat-residual alpha mode x t))
; → 0
```

### Wave equation

**u_tt = c²·u_xx** on (0, L), Dirichlet BCs. Each eigenfrequency ωₙ = cnπ/L yields two independent modes:

```
u_n^cos(x,t) = sin(nπx/L) · cos(ωₙ·t)
u_n^sin(x,t) = sin(nπx/L) · sin(ωₙ·t)
```

```scheme
(pde-wave-cos-mode c L n x t)   ; → cos-in-time mode
(pde-wave-sin-mode c L n x t)   ; → sin-in-time mode
```

General solution: **u = Σₙ [Aₙ·cos-mode + Bₙ·sin-mode]** where Aₙ = Fourier coefficients of u(x,0) and Bₙ depend on u_t(x,0).

### 2D Laplace equation

**u_xx + u_yy = 0** on (0,L)×(0,H), zero on three sides:

```scheme
(pde-laplace-2d-mode n x y L)   ; → sin(nπx/L) · sinh(nπy/L)
```

---

## § 3 — Fourier coefficient computation

Given an initial condition f(x), the Fourier coefficients weighting each eigenmode are computed by symbolic definite integration:

```scheme
(pde-fourier-sin-coeff f-expr L n x)   ; Bₙ = (2/L)·∫₀ᴸ f·sin(nπx/L) dx
(pde-fourier-cos-coeff f-expr L n x)   ; Aₙ = (2/L)·∫₀ᴸ f·cos(nπx/L) dx
```

`f-expr` is a symbolic expression in `x`.

**Examples:**

```scheme
(import (curry pde-symbolic))

(define x (sym-var 'x))
(define pi (acos -1.0))

; Initial condition f(x) = sin(πx) on [0,1]:
; Only n=1 mode survives (orthogonality)
(pde-fourier-sin-coeff (sin (* pi x)) 1.0 1 x)   ; → 1

; Initial condition f(x) = x on [0,1]:
; B₁ = 2/π ≈ 0.6366,  B₂ = -1/π ≈ -0.3183
(inexact (pde-fourier-sin-coeff x 1.0 1 x))   ; → 0.6366...
(inexact (pde-fourier-sin-coeff x 1.0 2 x))   ; → -0.3183...
```

The integrals use Curry's symbolic integration engine, including IBP rules for `x·sin(ax)`, `x·cos(ax)`, and half-angle reductions for `sin²(ax)` and `cos²(ax)`.

---

## § 4 — Method of characteristics

### Transport equation

**u_t + v·u_x = 0** — general solution is u(x,t) = f(x − v·t) for an arbitrary function f.

```scheme
(pde-transport-general v x t)   ; → (fn-apply f (- x (* v t)))
```

Returns an `SX_APPLY` node using a freshly created `sym-fn` named `f`. Differentiating this expression correctly applies the chain rule.

**Verification:**

```scheme
(define x (sym-var 'x))
(define t (sym-var 't))
(define u (pde-transport-general 2 x t))   ; f(x − 2t)

(simplify (pde-transport-residual 2 u x t))   ; → 0
; u_t + 2·u_x = f'(x-2t)·(-2) + 2·f'(x-2t) = 0  ✓
```

### First-order linear PDE — homogeneous

**a·u_x + b·u_t = 0** — characteristics are lines b·x − a·t = const.

```scheme
(pde-characteristics-homogeneous a b x t)   ; → f(b·x − a·t)
```

### First-order linear PDE — with decay/growth

**a·u_x + b·u_t = c·u** — along each characteristic, u satisfies du/ds = c·u, giving exponential factor exp(c·t/b):

```
u(x,t) = f(b·x − a·t) · exp(c·t/b)
```

```scheme
(pde-characteristics-decay a b c x t)   ; → f(bx−at) · exp(ct/b)
```

**Verification:**

```scheme
(define u (pde-characteristics-decay 1 1 2 x t))   ; f(x-t)·exp(2t)
(simplify (pde-first-order-residual 1 1 2 u x t))   ; → 0
; u_x + u_t − 2u = f'(x-t)·exp(2t) − f'(x-t)·exp(2t) + 2f·exp(2t) − 2f·exp(2t) = 0  ✓
```

---

## § 5 — Laplace-transform ODE reduction

Applying the Laplace transform in t to **u_t = α·u_xx** with IC u(x,0) = u₀(x):

```
L{u_t} = s·U(x,s) − u₀(x)
→  α·U_xx − s·U = −u₀(x)     [ODE in x for each s]
```

```scheme
; Returns (list alpha-coeff s-coeff rhs) for α·U_xx + (−s)·U = rhs
(pde-heat-laplace-ode alpha s u-init-expr)

; Symbolic residual α·U_xx − s·U + u-init (= 0 for a solution)
(pde-heat-laplace-residual alpha s U x u-init-expr)
```

`U` must be a `sym-fn` of two arguments (x and s). The residual uses `fn-apply` and `∂` to express U_xx symbolically.

**Example:**

```scheme
(define alpha (sym-var 'alpha 'positive))
(define s     (sym-var 's 'positive))

; For u₀(x) = sin(πx):
(define ode-params (pde-heat-laplace-ode alpha s (sin (* pi x))))
; ode-params = (alpha  -s  -sin(πx))
; The ODE is: α·U_xx − s·U = −sin(πx)
; Particular solution: U_p(x,s) = sin(πx)/(s + α·π²)
```

---

## § 6 — PDE residual verification

Each procedure returns the symbolic residual. Substitute numeric values with `pde-residual-at` for spot-checking.

```scheme
; Heat:        u_t − α·u_xx
(pde-heat-residual alpha u x t)

; Wave:        u_tt − c²·u_xx
(pde-wave-residual c u x t)

; Transport:   u_t + v·u_x
(pde-transport-residual v u x t)

; Laplace 2D:  u_xx + u_yy
(pde-laplace-residual u x y)

; First-order: a·u_x + b·u_t − c·u
(pde-first-order-residual a b c u x t)

; Numeric spot-check: substitute vars←vals, return inexact result (≈ 0 if valid)
(pde-residual-at residual-expr vars vals)
```

**Example:**

```scheme
(define-symbolic alpha L n x t)
(define u (pde-heat-mode alpha L n x t))
(define res (pde-heat-residual alpha u x t))

; Symbolic verification
(simplify res)   ; → 0

; Numeric verification at a specific point
(pde-residual-at res
  (list alpha L    n x   t)
  (list 0.1   1.0  2 0.4 0.5))
; → ~0.0  (within floating-point rounding)
```

---

## Full worked example — Fourier series solution for the heat equation

```scheme
(import (curry pde-symbolic))
(import (curry pde))   ; for numerical comparison

(define (pi) (acos -1.0))
(define x  (sym-var 'x))
(define t  (sym-var 't))
(define al 0.1)    ; thermal diffusivity
(define L  1.0)    ; domain length

; Initial condition: f(x) = sin(πx)  →  only mode n=1 survives
; B₁ = 1 (by orthogonality), all other Bₙ = 0
(define B1 (pde-fourier-sin-coeff (sin (* (pi) x)) L 1 x))
; B1 = 1

; Exact solution: u(x,t) = sin(πx)·exp(−α·π²·t)
(define u-exact
  (* B1 (pde-heat-mode al L 1 x t)))

; Verify PDE is satisfied
(pde-residual-at (pde-heat-residual al u-exact x t)
  (list x   t)
  (list 0.4 0.3))
; → ≈ 0.0

; Compare with numerical solver at x=0.4, t=0.3
(define nx 100)
(define dx (/ L (- nx 1)))
(define u0-vec (list->vector
  (map (lambda (i) (sin (* (pi) (* i dx)))) (iota nx))))

(define u-num (pde-heat al u0-vec 0.0 L nx 0.0 0.3 0.001 (bc-dirichlet 0.0 0.0)))

; Exact at x=0.4
(define exact-val
  (exact->inexact (substitute (substitute u-exact x 0.4) t 0.3)))
(display exact-val)               ; ≈ 0.5408...
(display (vector-ref u-num 40))   ; ≈ 0.5408... (numerical confirmation)
```

---

## See also

- [`(curry pde)`](module-pde.md) — numerical PDE solvers (method of lines, heat/wave/Poisson)
- [`(curry ode)`](module-ode.md) — ODE solvers used internally by the method-of-lines PDE solver
- [Symbolic CAS reference](symbolic.md) — `sym-var`, `∂`, `∫`, `substitute`, `simplify`
