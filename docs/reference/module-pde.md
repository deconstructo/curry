# Module: `(curry pde)`

*v0.8.17 — 2026-05-24*

Numerical PDE solvers for 1D time-dependent and elliptic problems. Pure Scheme — no C extension, no extra dependencies.

For **symbolic** PDE analysis (separation of variables, method of characteristics) see [`(curry pde-symbolic)`](module-pde-symbolic.md).

## Import

```scheme
(import (curry pde))
```

## Methods at a glance

| Procedure | PDE | Method |
|-----------|-----|--------|
| `pde-heat` | u_t = α·u_xx | Method of lines + RK4 |
| `pde-wave` | u_tt = c²·u_xx | Leapfrog (2nd-order, CFL-stable) |
| `pde-poisson-1d` | u_xx = f(x) | Thomas tridiagonal |
| `pde-mol` | du/dt = rhs(t,u) | General method of lines + RK4 |

## Boundary conditions

All solvers take a boundary-condition descriptor constructed by one of these:

```scheme
(bc-dirichlet left right)           ; u(0,t)=left, u(L,t)=right (fixed values)
(bc-neumann   left-deriv right-deriv) ; u_x(0,t)=left-deriv, u_x(L,t)=right-deriv
(bc-periodic)                        ; u(0,t) = u(L,t)  (wrap-around)
```

## Grid utility

```scheme
(pde-linspace a b n)   ; → vector of n evenly-spaced flonum x values in [a,b]
```

## Heat equation

**u_t = α·u_xx**, with initial condition `u0` (vector of length `nx`).

```scheme
; Final state at t1
(pde-heat alpha u0 a b nx t0 t1 dt bc)   ; → vector

; All snapshots: list of (t . u-vector) pairs
(pde-heat/steps alpha u0 a b nx t0 t1 dt bc)   ; → ((t . u) ...)
```

**Parameters**

| Parameter | Meaning |
|-----------|---------|
| `alpha` | thermal diffusivity (> 0) |
| `u0` | initial condition vector, length `nx` |
| `a`, `b` | spatial domain [a, b] |
| `nx` | number of grid points |
| `t0`, `t1` | time interval |
| `dt` | time step (must satisfy CFL: `alpha·dt/dx² ≤ 0.5` for stability) |
| `bc` | boundary condition |

**Example — Gaussian pulse diffusing on [0,1]:**

```scheme
(import (curry pde))

(define nx 50)
(define xs (pde-linspace 0.0 1.0 nx))
(define u0 (vector-map (lambda (x) (exp (* -50.0 (expt (- x 0.5) 2)))) xs))

(define result
  (pde-heat 0.01 u0 0.0 1.0 nx 0.0 0.5 0.001 (bc-dirichlet 0.0 0.0)))
; result: vector of u values at t=0.5
```

## Wave equation

**u_tt = c²·u_xx**, with initial displacement `u0` and velocity `v0` (both vectors of length `nx`).

```scheme
; Final state (u at t1)
(pde-wave c u0 v0 a b nx t0 t1 dt bc)   ; → vector

; All snapshots
(pde-wave/steps c u0 v0 a b nx t0 t1 dt bc)   ; → ((t . u) ...)
```

Uses a **leapfrog** (Störmer-Verlet) scheme, which is second-order in time and space, and is stable provided the CFL number `r = c·dt/dx ≤ 1`. The ghost layer at `t = -dt` is initialised to second-order accuracy using the initial velocity.

**Example — plucked string:**

```scheme
(import (curry pde))

(define nx 100)
(define xs (pde-linspace 0.0 1.0 nx))
; Triangle initial shape, zero velocity
(define u0 (vector-map (lambda (x) (if (< x 0.5) (* 2 x) (* 2 (- 1 x)))) xs))
(define v0 (make-vector nx 0.0))

(define steps
  (pde-wave/steps 1.0 u0 v0 0.0 1.0 nx 0.0 2.0 0.005 (bc-dirichlet 0.0 0.0)))
; steps: list of (t . u-vector) pairs at each time step
```

## Poisson 1D

**u_xx = f(x)** on a grid, with Dirichlet BCs only.

```scheme
(pde-poisson-1d f x-vec bc)   ; → solution vector
```

`f` is either a procedure `(lambda (x) ...)` or a vector of length `nx` giving the right-hand side at each grid point. Solved via the **Thomas tridiagonal algorithm** in O(n) time.

**Example — electrostatic potential:**

```scheme
(import (curry pde))

(define nx 100)
(define xs (pde-linspace 0.0 1.0 nx))
; u'' = -1 (uniform charge density), u(0)=0, u(1)=0
; Exact: u(x) = x(1-x)/2
(define u (pde-poisson-1d (lambda (x) -1.0) xs (bc-dirichlet 0.0 0.0)))
(display (vector-ref u 50))   ; ≈ 0.25  (exact: 0.5·0.5·0.5 = 0.125 — midpoint of x(1-x)/2)
```

## General method of lines

```scheme
; Final state
(pde-mol rhs u0 t0 t1 dt bc)   ; → vector

; All snapshots
(pde-mol/steps rhs u0 t0 t1 dt bc)   ; → ((t . u) ...)
```

`rhs` is `(lambda (t u-vector) ...)` returning `du/dt` as a vector of the same length as `u`. Boundary conditions are enforced after each RK4 step. All spatial discretisation is the caller's responsibility — this is the lowest-level building block.

**Example — advection (upwind, manual stencil):**

```scheme
(import (curry pde))

(define nx 80)
(define dx (/ 1.0 (- nx 1)))
(define c 1.0)   ; advection speed

(define (advect-rhs t u)
  ; upwind finite difference: du/dt ≈ -c * (u_i - u_{i-1}) / dx
  (let ((r (make-vector nx 0.0)))
    (do ((i 1 (+ i 1))) ((= i (- nx 1)))
      (vector-set! r i (/ (* (- c) (- (vector-ref u i) (vector-ref u (- i 1)))) dx)))
    r))

(define xs (pde-linspace 0.0 1.0 nx))
(define u0 (vector-map (lambda (x) (exp (* -50.0 (expt (- x 0.3) 2)))) xs))

(pde-mol advect-rhs u0 0.0 0.5 0.005 (bc-dirichlet 0.0 0.0))
```

## Finite-difference stencils

Lower-level helpers available if you build your own `rhs`:

```scheme
; ∂²u/∂x² via central differences — returns vector of length n
(fd-laplacian-1d u dx bc)

; ∂u/∂x via central differences (one-sided at boundaries for Dirichlet)
(fd-gradient-1d u dx bc)
```

## Stability notes

| Scheme | CFL condition | Constraint |
|--------|--------------|------------|
| Heat (MOL + RK4) | `alpha·dt/dx² ≤ ~0.5` | explicit, mildly stiff |
| Wave (leapfrog) | `c·dt/dx ≤ 1` | exactly satisfied when `r ≤ 1` |
| Poisson (Thomas) | unconditionally stable | direct solve, not time-stepping |

Exceeding the CFL number causes visible exponential growth in the solution. If unsure, halve `dt` until the solution is stable.

## See also

- [`(curry ode)`](module-ode.md) — ODE solvers (the 1D spatial grid is effectively an ODE system after method-of-lines reduction)
- [`(curry pde-symbolic)`](module-pde-symbolic.md) — exact symbolic PDE analysis: separation of variables, method of characteristics
