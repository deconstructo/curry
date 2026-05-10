# Module: (curry ode)

Ordinary differential equation solvers for initial-value problems of the form **dy/dt = f(t, y)**. Pure Scheme — no C extension, no extra dependencies.

`y` may be a **number** (scalar ODE) or a **list of numbers** (system of ODEs). All methods work transparently with Curry's numeric tower: exact rationals, complex numbers, and symbolic expressions are all valid.

## Import

```scheme
(import (curry ode))
```

## Methods

| Method | Order | Step | Best for |
|--------|-------|------|----------|
| `ode-euler` | 1st | fixed | illustration / quick prototyping |
| `ode-rk4` | 4th | fixed | smooth problems, known step size |
| `ode-rk45` | 5th (adaptive) | automatic | general use — same algorithm as MATLAB `ode45` |
| `ode-verlet` | 2nd (symplectic) | fixed | Hamiltonian systems: energy-preserving |

## API

Each method has two forms: a **final-value** form that returns `y` at `t1`, and a **/steps** form that returns a list of `(t . y)` snapshots at every accepted step.

### Euler

```scheme
(ode-euler  f y0 t0 t1 h)         ; → y at t1
(ode-euler/steps f y0 t0 t1 h)    ; → ((t . y) ...)
```

First-order, global error O(h). Use a small `h` — or use RK4/RK45 instead.

### RK4

```scheme
(ode-rk4  f y0 t0 t1 h)           ; → y at t1
(ode-rk4/steps f y0 t0 t1 h)      ; → ((t . y) ...)
```

Classical fourth-order Runge-Kutta. Global error O(h⁴). Exact for polynomials of degree ≤ 4. A step of `h = 0.01–0.1` is usually sufficient for smooth problems.

### RK45 (Dormand-Prince)

```scheme
(ode-rk45  f y0 t0 t1)            ; → y at t1  (tolerance 1e-6)
(ode-rk45  f y0 t0 t1 tol)        ; → y at t1
(ode-rk45/steps f y0 t0 t1)       ; → ((t . y) ...)
(ode-rk45/steps f y0 t0 t1 tol)   ; → ((t . y) ...)
```

Fifth-order Dormand-Prince with embedded fourth-order error estimate. Step size is adjusted automatically so the local error stays ≤ `tol`. This is the default choice for most problems — it requires no tuning and handles varying timescales gracefully.

`tol` defaults to `1e-6`. Tighter tolerances (e.g. `1e-10`) give higher accuracy at the cost of more function evaluations.

### Verlet (symplectic)

```scheme
(ode-verlet  accel q0 p0 t0 t1 h)          ; → (q . p)
(ode-verlet/steps accel q0 p0 t0 t1 h)     ; → ((t q . p) ...)
```

Velocity-Verlet integrator for second-order systems **d²q/dt² = accel(t, q)** with position `q` and velocity `p`. `q` and `p` may each be a number or a list.

`accel` is a function `(lambda (t q) ...)` returning the acceleration (force/mass).

Verlet is **symplectic** — it exactly conserves a slightly modified Hamiltonian, so total energy oscillates by O(h²) rather than drifting unboundedly. RK4 and RK45 accumulate energy error over long integrations; Verlet does not. Use it for orbital mechanics, molecular dynamics, and any conservative physical system you intend to run for many periods.

## Examples

### Exponential growth / decay

```scheme
(import (curry ode))

; dy/dt = -0.5y,  y(0) = 10  →  y(t) = 10·e^{-t/2}
(ode-rk45 (lambda (t y) (* -0.5 y)) 10.0 0.0 4.0)
; ≈ 1.353...  (exact: 10·e^{-2})
```

### Harmonic oscillator (system)

```scheme
(import (curry ode))

; [q, p]' = [p, -q]  →  q(t) = cos(t),  p(t) = -sin(t)
(define (f t qp)
  (list (cadr qp) (- (car qp))))

(let* ((steps (ode-rk4/steps f '(1.0 0.0) 0.0 (* 2 (acos -1.0)) 0.1))
       (last  (cdar (reverse steps))))
  (display (car last))   ; ≈ 1.0  (q returns to start)
  (display (cadr last))) ; ≈ 0.0
```

### Lorenz attractor

```scheme
(import (curry ode))

(define (lorenz t y)
  (let ((x (car y)) (v (cadr y)) (z (caddr y)))
    (list (* 10.0 (- v x))
          (- (* x (- 28.0 z)) v)
          (- (* x v) (* 8/3 z)))))

; 30 seconds of chaos — returns a list of (t x y z) snapshots
(define trajectory
  (ode-rk45/steps lorenz '(1.0 0.0 0.0) 0.0 30.0 1e-8))

(display (length trajectory))       ; adaptive: ~4000 steps
(display (cdar (reverse trajectory))) ; final state
```

### Pendulum

```scheme
(import (curry ode))

; d²θ/dt² = -sin(θ),  θ(0) = π/3,  θ'(0) = 0
; Nonlinear — no closed-form solution for large angles
(define (pendulum t y)
  (list (cadr y) (- (sin (car y)))))

(define (pi) (acos -1.0))

(ode-rk45/steps pendulum (list (/ (pi) 3) 0.0) 0.0 (* 4 (pi)) 1e-8)
```

### N-body gravity with Verlet

```scheme
(import (curry ode))

; Two bodies, each 2D: q = (x1 y1 x2 y2), p = (vx1 vy1 vx2 vy2)
; Gravity: F = -G·m1·m2/r²
(define G 1.0)
(define m1 1.0)
(define m2 1.0)

(define (gravity t q)
  (let* ((x1 (car q)) (y1 (cadr q))
         (x2 (caddr q)) (y2 (cadddr q))
         (dx (- x2 x1)) (dy (- y2 y1))
         (r3 (expt (+ (* dx dx) (* dy dy)) 1.5))
         (ax1 (*  G m2 (/ dx r3)))
         (ay1 (*  G m2 (/ dy r3)))
         (ax2 (* -G m1 (/ dx r3)))
         (ay2 (* -G m1 (/ dy r3))))
    (list ax1 ay1 ax2 ay2)))

; Circular orbit initial conditions
(define steps
  (ode-verlet/steps gravity
    '(0.5 0.0 -0.5 0.0)    ; positions
    '(0.0 0.5  0.0 -0.5)   ; velocities
    0.0 (* 2 (acos -1.0)) 0.01))

; Extract x1 trajectory
(map (lambda (tqp) (list (car tqp) (cadr tqp))) steps)
```

### Exact arithmetic with symbolic expressions

```scheme
(import (curry ode))

; RK4 with exact rational step — result is exact rational
; dy/dt = 2t,  y(0) = 0  →  y(t) = t²  (polynomial, exact in RK4)
(ode-rk4 (lambda (t y) (* 2 t)) 0 0 3 1/2)
; → 9  (exact, not 9.0)
```

## Choosing a method

```
Need energy conservation over many periods?  →  ode-verlet
General smooth problem, hands-off?           →  ode-rk45
Fixed step, full control?                    →  ode-rk4
Teaching / quick check?                      →  ode-euler
```

## Notes

- **Systems:** pass `y0` as a list; `f` receives and returns a list of the same length.
- **Verlet /steps format:** each snapshot is `(t q . p)` where `q` and `p` may be numbers or lists — extract with `(cadr snapshot)` for q and `(cddr snapshot)` for p.
- **Stiff problems:** RK45 handles mildly stiff systems by reducing step size automatically, but very stiff problems (e.g. electrical circuits with widely separated timescales) will be slow. Consider transforming the problem or using an implicit method.
- **Tolerance vs accuracy:** `tol` controls the *local* truncation error per step. The *global* error over a long interval is larger — roughly `tol * (t1 - t0)` in the worst case. Tighten `tol` proportionally if global accuracy matters.
- **Complex y:** arithmetic is inherited from Curry's numeric tower, so `y` can be complex-valued. Useful for solving the Schrödinger equation or other complex-valued ODEs directly.
