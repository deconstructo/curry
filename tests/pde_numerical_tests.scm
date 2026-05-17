;;; Phase 9 — Numerical PDE module tests
;;;
;;; Validates (curry pde) solvers against problems with known closed-form
;;; solutions.  Error tolerances account for the method's spatial order
;;; O(dx²) and temporal order O(dt⁴) for RK4, O(dt²) for leapfrog.

(import (curry pde))
(import (scheme base))
(import (scheme inexact))

(define pass 0)
(define fail 0)
(define pi (acos -1.0))

(define (check label got expected tol)
  (let ((err (abs (- (inexact got) (inexact expected)))))
    (if (<= err tol)
        (begin (display "PASS: ") (display label) (newline)
               (set! pass (+ pass 1)))
        (begin (display "FAIL: ") (display label)
               (display " — got ") (display (inexact got))
               (display "  expected ") (display (inexact expected))
               (display "  err=") (display err)
               (display "  tol=") (display tol) (newline)
               (set! fail (+ fail 1))))))

(define (check-true label val)
  (if val
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label) (newline)
             (set! fail (+ fail 1)))))

;;; ════════════════════════════════════════════════════════════
;;; § 1  pde-linspace
;;; ════════════════════════════════════════════════════════════

(let ((v (pde-linspace 0 1 5)))
  (check-true "linspace: vector?"    (vector? v))
  (check      "linspace: length"     (vector-length v) 5 0)
  (check      "linspace: left end"   (vector-ref v 0)  0.0 1e-15)
  (check      "linspace: right end"  (vector-ref v 4)  1.0 1e-15)
  (check      "linspace: midpoint"   (vector-ref v 2)  0.5 1e-15))

(let ((v (pde-linspace -1 1 3)))
  (check "linspace: negative a"  (vector-ref v 0) -1.0 1e-15)
  (check "linspace: midpoint 0"  (vector-ref v 1)  0.0 1e-15)
  (check "linspace: positive b"  (vector-ref v 2)  1.0 1e-15))

;;; ════════════════════════════════════════════════════════════
;;; § 2  Poisson 1D  —  u_xx = f,  Thomas tridiagonal
;;; ════════════════════════════════════════════════════════════

;;; Test A: Laplace equation  u_xx = 0,  u(0)=0, u(1)=1
;;;   Exact: u(x) = x  (linear)
;;;   nx=41 so dx=1/40 and x_10=0.25, x_20=0.5 exactly.
(let* ((nx  41)
       (xv  (pde-linspace 0 1 nx))
       (bc  (bc-dirichlet 0.0 1.0))
       (u   (pde-poisson-1d (lambda (x) 0.0) xv bc)))
  (check "poisson: u_xx=0 left BC"   (vector-ref u 0)   0.0  1e-14)
  (check "poisson: u_xx=0 right BC"  (vector-ref u (- nx 1)) 1.0 1e-14)
  (check "poisson: u_xx=0 mid"       (vector-ref u 20)  0.5  1e-10)
  (check "poisson: u_xx=0 x=0.25"    (vector-ref u 10)  0.25 1e-10))

;;; Test B: u_xx = -π²·sin(πx),  u(0)=u(1)=0
;;;   Exact: u(x) = sin(πx)  (O(dx²) error, ~1e-3 with nx=41)
(let* ((nx    41)
       (xv    (pde-linspace 0 1 nx))
       (bc    (bc-dirichlet 0.0 0.0))
       (u     (pde-poisson-1d
                (lambda (x) (- (* pi pi (sin (* pi x)))))
                xv bc)))
  (check "poisson: u_xx=-π²sin(πx) mid"
         (vector-ref u 20) 1.0 2e-3)
  (check "poisson: u_xx=-π²sin(πx) x=0.25"
         (vector-ref u 10) (sin (* pi 0.25)) 2e-3))

;;; Test C: u_xx = 2 (constant),  u(0)=0, u(1)=0
;;;   Exact: u(x) = x(x-1)  i.e. u(x) = x²-x
(let* ((nx   51)
       (xv   (pde-linspace 0 1 nx))
       (bc   (bc-dirichlet 0.0 0.0))
       (u    (pde-poisson-1d (lambda (x) 2.0) xv bc))
       (mid  (quotient nx 2))
       (x-mid (vector-ref xv mid)))
  (check "poisson: u_xx=2 mid"
         (vector-ref u mid) (* x-mid (- x-mid 1.0)) 1e-10))

;;; ════════════════════════════════════════════════════════════
;;; § 3  Heat equation  u_t = α·u_xx  (MOL + RK4)
;;; ════════════════════════════════════════════════════════════

;;; Test D: Dirichlet,  u(x,0)=sin(πx),  u(0,t)=u(1,t)=0,  α=1
;;;   Exact: u(x,t) = e^{-π²t}·sin(πx)
;;;   RK4 stability: dt·4/dx² < 2.785  → dt=2e-4, nx=51 gives dt·4·50²=2.0 ✓
(let* ((nx    51)
       (u0    (let ((v (make-vector nx 0.0)))
                (do ((i 0 (+ i 1))) ((= i nx))
                  (vector-set! v i (sin (* pi (/ (inexact i) (- nx 1))))))
                v))
       (bc    (bc-dirichlet 0.0 0.0))
       (t1    0.1)
       (u     (pde-heat 1.0 u0 0 1 nx 0 t1 2e-4 bc))
       (mid   (quotient nx 2))
       (exact (* (exp (- (* pi pi t1))) (sin (* pi 0.5)))))
  (check "heat: Dirichlet sin(πx) mid"
         (vector-ref u mid) exact 5e-3)
  (check "heat: Dirichlet left BC"
         (vector-ref u 0) 0.0 1e-12)
  (check "heat: Dirichlet right BC"
         (vector-ref u (- nx 1)) 0.0 1e-12))

;;; Test E: Neumann,  u(x,0)=cos(πx),  u_x(0,t)=u_x(1,t)=0,  α=1
;;;   Exact: u(x,t) = e^{-π²t}·cos(πx)
(let* ((nx    51)
       (u0    (let ((v (make-vector nx 0.0)))
                (do ((i 0 (+ i 1))) ((= i nx))
                  (vector-set! v i (cos (* pi (/ (inexact i) (- nx 1))))))
                v))
       (bc    (bc-neumann 0.0 0.0))
       (t1    0.1)
       (u     (pde-heat 1.0 u0 0 1 nx 0 t1 2e-4 bc))
       (exact (exp (- (* pi pi t1)))))
  (check "heat: Neumann cos(πx) at x=0"
         (vector-ref u 0) exact 5e-3)
  (check "heat: Neumann cos(πx) at x=1"
         (vector-ref u (- nx 1)) (- exact) 5e-3))

;;; Test F: pde-heat/steps returns list of (t . vector) snapshots
(let* ((nx    11)
       (u0    (make-vector nx 1.0))
       (bc    (bc-dirichlet 0.0 0.0))
       (steps (pde-heat/steps 1.0 u0 0 1 nx 0 0.02 0.01 bc)))
  (check-true "heat/steps: list?"        (list? steps))
  (check-true "heat/steps: non-empty"    (> (length steps) 0))
  (check      "heat/steps: first t=0"    (caar steps) 0.0 1e-12)
  (check-true "heat/steps: vectors"      (vector? (cdar steps))))

;;; ════════════════════════════════════════════════════════════
;;; § 4  Wave equation  u_tt = c²·u_xx  (leapfrog)
;;; ════════════════════════════════════════════════════════════

;;; Test G: Dirichlet,  u(x,0)=sin(πx), u_t(x,0)=0,  c=1
;;;   Exact: u(x,t) = sin(πx)·cos(πt)    (d'Alembert)
;;;   CFL: c·dt/dx = 1·0.01/0.02 = 0.5 ≤ 1  ✓
(let* ((nx    51)
       (u0    (let ((v (make-vector nx 0.0)))
                (do ((i 0 (+ i 1))) ((= i nx))
                  (vector-set! v i (sin (* pi (/ (inexact i) (- nx 1))))))
                v))
       (v0    (make-vector nx 0.0))
       (bc    (bc-dirichlet 0.0 0.0))
       (t1    0.25)
       (u     (pde-wave 1.0 u0 v0 0 1 nx 0 t1 0.01 bc))
       (mid   (quotient nx 2))
       (exact (* (sin (* pi 0.5)) (cos (* pi t1)))))
  (check "wave: Dirichlet at t=0.25, mid"
         (vector-ref u mid) exact 5e-3)
  (check "wave: Dirichlet left BC"
         (vector-ref u 0) 0.0 1e-12)
  (check "wave: Dirichlet right BC"
         (vector-ref u (- nx 1)) 0.0 1e-12))

;;; Test H: Initial velocity only,  u(x,0)=0, u_t(x,0)=π·sin(πx)
;;;   Exact: u(x,t) = sin(πx)·sin(πt)
(let* ((nx    51)
       (u0    (make-vector nx 0.0))
       (v0    (let ((v (make-vector nx 0.0)))
                (do ((i 0 (+ i 1))) ((= i nx))
                  (vector-set! v i (* pi (sin (* pi (/ (inexact i) (- nx 1)))))))
                v))
       (bc    (bc-dirichlet 0.0 0.0))
       (t1    0.25)
       (u     (pde-wave 1.0 u0 v0 0 1 nx 0 t1 0.01 bc))
       (mid   (quotient nx 2))
       (exact (* (sin (* pi 0.5)) (sin (* pi t1)))))
  (check "wave: initial velocity mid"
         (vector-ref u mid) exact 5e-3))

;;; Test I: pde-wave/steps returns sorted list of snapshots
(let* ((nx    21)
       (u0    (make-vector nx 0.0))
       (v0    (make-vector nx 0.0))
       (bc    (bc-dirichlet 0.0 0.0))
       (steps (pde-wave/steps 1.0 u0 v0 0 1 nx 0 0.5 0.1 bc)))
  (check-true "wave/steps: list?"     (list? steps))
  (check-true "wave/steps: non-empty" (> (length steps) 0))
  (check      "wave/steps: first t=0" (caar steps) 0.0 1e-12)
  (check-true "wave/steps: vectors"   (vector? (cdar steps))))

;;; ════════════════════════════════════════════════════════════
;;; § 5  Method of lines  (pde-mol)
;;; ════════════════════════════════════════════════════════════

;;; Test J: pde-mol with explicit Laplacian reproduces pde-heat
(let* ((nx   31)
       (u0   (let ((v (make-vector nx 0.0)))
               (do ((i 0 (+ i 1))) ((= i nx))
                 (vector-set! v i (sin (* pi (/ (inexact i) (- nx 1))))))
               v))
       (bc   (bc-dirichlet 0.0 0.0))
       (dx   (/ 1.0 (- nx 1)))
       (t1   0.05)
       (dt   5e-4)
       (u-heat (pde-heat 1.0 u0 0 1 nx 0 t1 dt bc))
       (u-mol  (pde-mol (lambda (t u) (fd-laplacian-1d u dx bc))
                        u0 0 t1 dt bc))
       (mid  (quotient nx 2)))
  (check "mol = heat: mid"
         (vector-ref u-heat mid) (vector-ref u-mol mid) 1e-12)
  (check "mol = heat: idx 5"
         (vector-ref u-heat 5) (vector-ref u-mol 5) 1e-12))

;;; Test K: pde-mol/steps returns list of snapshots
(let* ((nx    11)
       (u0    (make-vector nx 0.5))
       (bc    (bc-dirichlet 0.0 0.0))
       (dx    (/ 1.0 (- nx 1)))
       (steps (pde-mol/steps (lambda (t u) (fd-laplacian-1d u dx bc))
                             u0 0 0.1 0.05 bc)))
  (check-true "mol/steps: list?"     (list? steps))
  (check-true "mol/steps: 3 points"  (= (length steps) 3))
  (check      "mol/steps: t=0"       (caar steps) 0.0 1e-12)
  (check      "mol/steps: t=0.1"     (car (car (reverse steps))) 0.1 1e-12))

;;; ════════════════════════════════════════════════════════════
;;; § 6  Periodic boundary conditions
;;; ════════════════════════════════════════════════════════════

;;; Test L: Heat equation, periodic,  u(x,0)=sin(2πx)
;;;   Exact: u(x,t) = e^{-4π²t}·sin(2πx)
;;;   nx=41 so x_i=i/40, x_{10}=0.25 exactly → sin(2π·0.25)=sin(π/2)=1
(let* ((nx    41)
       (u0    (let ((v (make-vector nx 0.0)))
                (do ((i 0 (+ i 1))) ((= i nx))
                  (vector-set! v i (sin (* 2.0 pi (/ (inexact i) (- nx 1))))))
                v))
       (bc    (bc-periodic))
       (t1    0.01)
       (u     (pde-heat 1.0 u0 0 1 nx 0 t1 1e-4 bc))
       (exact (* (exp (* -4.0 pi pi t1)) (sin (* 2.0 pi 0.25)))))
  (check "heat: periodic sin(2πx) at x=0.25"
         (vector-ref u 10) exact 0.05))

;;; ════════════════════════════════════════════════════════════
;;; § 7  fd-laplacian-1d and fd-gradient-1d
;;; ════════════════════════════════════════════════════════════

;;; Test M: fd-laplacian-1d of a quadratic x² gives 2 everywhere
;;;   (u_xx = 2 for u=x², exact with central differences)
(let* ((nx   11)
       (dx   (/ 1.0 (- nx 1)))
       (u    (let ((v (make-vector nx 0.0)))
               (do ((i 0 (+ i 1))) ((= i nx))
                 (let ((x (* i dx)))
                   (vector-set! v i (* x x))))
               v))
       (bc   (bc-neumann 0.0 (* 2.0 (/ (- nx 1) 1))))  ; u'(1)=2·1=2
       (lap  (fd-laplacian-1d u dx bc)))
  ;; Interior points must be exactly 2
  (check "laplacian of x²: interior mid"
         (vector-ref lap (quotient nx 2)) 2.0 1e-10))

;;; Test N: fd-gradient-1d of x gives 1 everywhere (interior)
(let* ((nx   11)
       (dx   (/ 1.0 (- nx 1)))
       (u    (pde-linspace 0 1 nx))
       (bc   (bc-neumann 1.0 1.0))
       (grad (fd-gradient-1d u dx bc)))
  (check "gradient of x: mid"
         (vector-ref grad (quotient nx 2)) 1.0 1e-10)
  (check "gradient of x: Neumann left"
         (vector-ref grad 0) 1.0 1e-10))

;;; ════════════════════════════════════════════════════════════
;;; Summary
;;; ════════════════════════════════════════════════════════════

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
