;;; ODE solver tests — (curry ode)
;;;
;;; Each solver is validated against problems with known closed-form solutions.
;;; Tests check that numerical error stays within expected bounds for the
;;; method's order.

(import (curry ode))
(import (scheme inexact))
(import (scheme base))

;;; ---- Harness ----

(define pass 0)
(define fail 0)

(define (check label got expected tol)
  (let ((err (abs (- (inexact got) (inexact expected)))))
    (if (<= err tol)
        (begin (display "PASS: ") (display label) (newline)
               (set! pass (+ pass 1)))
        (begin (display "FAIL: ") (display label)
               (display " — got ") (display (inexact got))
               (display " expected ") (display (inexact expected))
               (display " err=") (display err)
               (display " tol=") (display tol) (newline)
               (set! fail (+ fail 1))))))

(define (check-list label got expected tol)
  (let ((err (apply max (map (lambda (g e) (abs (- (inexact g) (inexact e))))
                             got expected))))
    (if (<= err tol)
        (begin (display "PASS: ") (display label) (newline)
               (set! pass (+ pass 1)))
        (begin (display "FAIL: ") (display label)
               (display " — err=") (display err)
               (display " tol=") (display tol) (newline)
               (set! fail (+ fail 1))))))

;;; ════════════════════════════════════════════════════════════
;;; Test problems with known solutions
;;; ════════════════════════════════════════════════════════════

;;; Problem 1: dy/dt = y,  y(0)=1  →  y(t) = e^t
(define (f-exp t y) y)
(define y-exp (lambda (t) (exp t)))

;;; Problem 2: dy/dt = -y,  y(0)=1  →  y(t) = e^{-t}
(define (f-decay t y) (- y))
(define y-decay (lambda (t) (exp (- t))))

;;; Problem 3: dy/dt = cos(t),  y(0)=0  →  y(t) = sin(t)
(define (f-cos t y) (cos t))
(define y-cos (lambda (t) (sin t)))

;;; Problem 4: dy/dt = 2t,  y(0)=0  →  y(t) = t²  (polynomial, exact in RK4)
(define (f-poly t y) (* 2.0 t))
(define y-poly (lambda (t) (* t t)))

;;; Problem 5: system — harmonic oscillator  [q, p] = [p, -q]
;;;   q(t) = cos(t),  p(t) = -sin(t)  starting from [1, 0]
(define (f-harmonic t qp)
  (list (cadr qp) (- (car qp))))
(define q-harmonic (lambda (t) (cos t)))
(define p-harmonic (lambda (t) (- (sin t))))

;;; Problem 6: Verlet — harmonic oscillator  accel = -q
(define (accel-harmonic t q) (- q))

;;; ════════════════════════════════════════════════════════════
;;; § 1  EULER tests (first-order, large tolerance)
;;; ════════════════════════════════════════════════════════════

; Euler global error is O(h); with h=0.01, t=[0,1]: err ≈ h/2 * e ≈ 0.014
(let ((y (ode-euler f-exp 1.0 0.0 1.0 0.01)))
  (check "euler: e^1 (h=0.01)" y (y-exp 1.0) 0.015))

(let ((y (ode-euler f-decay 1.0 0.0 2.0 0.01)))
  (check "euler: e^{-2} (h=0.01)" y (y-decay 2.0) 0.006))

(let ((y (ode-euler f-cos 0.0 0.0 1.0 0.001)))
  (check "euler: sin(1) (h=0.001)" y (y-cos 1.0) 0.001))

;;; /steps variant: first and last point
(let* ((steps (ode-euler/steps f-exp 1.0 0.0 1.0 0.1))
       (t0-y  (car steps))
       (t1-y  (car (reverse steps))))
  (check "euler/steps: first t=0" (car t0-y) 0.0 1e-15)
  (check "euler/steps: first y=1" (cdr t0-y) 1.0 1e-15)
  (check "euler/steps: last  t=1" (car t1-y) 1.0 1e-14))

;;; ════════════════════════════════════════════════════════════
;;; § 2  RK4 tests (fourth-order, tight tolerance)
;;; ════════════════════════════════════════════════════════════

; RK4 global error O(h^4); with h=0.1, t=[0,1]: err ≈ h^4/30 * e ≈ 9e-6
(let ((y (ode-rk4 f-exp 1.0 0.0 1.0 0.1)))
  (check "rk4: e^1 (h=0.1)" y (y-exp 1.0) 1e-5))

(let ((y (ode-rk4 f-decay 1.0 0.0 3.0 0.1)))
  (check "rk4: e^{-3} (h=0.1)" y (y-decay 3.0) 1e-5))

(let ((y (ode-rk4 f-cos 0.0 0.0 2.0 0.1)))
  (check "rk4: sin(2) (h=0.1)" y (y-cos 2.0) 1e-7))

;;; Polynomial t² should be exact (RK4 is exact for degree ≤ 4 polys)
(let ((y (ode-rk4 f-poly 0.0 0.0 3.0 0.5)))
  (check "rk4: t² exact" y (y-poly 3.0) 1e-12))

;;; System: harmonic oscillator [q(t),p(t)] with h=0.1 over t=[0,2π]
(let* ((t1  (* 2.0 (acos -1.0)))
       (y   (ode-rk4 f-harmonic '(1.0 0.0) 0.0 t1 0.1))
       (q   (car y))
       (p   (cadr y)))
  (check "rk4: harmonic q(2π)" q 1.0 1e-5)
  (check "rk4: harmonic p(2π)" p 0.0 1e-5))

;;; /steps: check monotone t and length (initial point + 10 steps = 11;
;;; floating-point accumulation can produce one extra, so allow 11–12)
(let* ((steps (ode-rk4/steps f-exp 1.0 0.0 1.0 0.1))
       (ts    (map car steps)))
  (check "rk4/steps: ~11 points" (length steps) 11 1)
  (check "rk4/steps: monotone"
         (apply min (map - (cdr ts) ts))
         0.1 0.15))

;;; ════════════════════════════════════════════════════════════
;;; § 3  RK45 tests (adaptive, very tight tolerance)
;;; ════════════════════════════════════════════════════════════

(let ((y (ode-rk45 f-exp 1.0 0.0 1.0 1e-8)))
  (check "rk45: e^1 (tol=1e-8)" y (y-exp 1.0) 1e-6))

(let ((y (ode-rk45 f-decay 1.0 0.0 5.0 1e-8)))
  (check "rk45: e^{-5} (tol=1e-8)" y (y-decay 5.0) 1e-6))

(let ((y (ode-rk45 f-cos 0.0 0.0 3.0 1e-8)))
  (check "rk45: sin(3) (tol=1e-8)" y (y-cos 3.0) 1e-6))

;;; Default tolerance
(let ((y (ode-rk45 f-exp 1.0 0.0 2.0)))
  (check "rk45: e^2 default tol" y (y-exp 2.0) 1e-4))

;;; System: harmonic oscillator over t=[0, 4π] — two full orbits
(let* ((t1  (* 4.0 (acos -1.0)))
       (y   (ode-rk45 f-harmonic '(1.0 0.0) 0.0 t1 1e-8))
       (q   (car y))
       (p   (cadr y)))
  (check "rk45: harmonic q(4π)" q 1.0 1e-5)
  (check "rk45: harmonic p(4π)" p 0.0 1e-5))

;;; /steps: verify returned steps cover the interval
(let* ((steps (ode-rk45/steps f-exp 1.0 0.0 1.0 1e-6))
       (t-end (car (car (reverse steps))))
       (y-end (cdr (car (reverse steps)))))
  (check "rk45/steps: ends at t=1" t-end 1.0 1e-12)
  (check "rk45/steps: y at t=1" y-end (y-exp 1.0) 1e-4))

;;; Stiff-ish problem: rapid decay, check RK45 handles it
(let ((y (ode-rk45 (lambda (t y) (* -20.0 y)) 1.0 0.0 1.0 1e-6)))
  (check "rk45: fast decay e^{-20}" y (exp -20.0) 1e-4))

;;; ════════════════════════════════════════════════════════════
;;; § 4  VERLET tests (symplectic, Hamiltonian systems)
;;; ════════════════════════════════════════════════════════════

;;; Scalar harmonic oscillator: d²q/dt² = -q
;;; q(t) = cos(t),  q'(t) = p(t) = -sin(t)
;;; Starting from q=1, p=0.

(let* ((pi  (acos -1.0))
       (qp  (ode-verlet accel-harmonic 1.0 0.0 0.0 (* 2.0 pi) 0.01))
       (q   (car qp))
       (p   (cdr qp)))
  (check "verlet: harmonic q(2π)" q 1.0 1e-4)
  (check "verlet: harmonic p(2π)" p 0.0 1e-4))

;;; Verlet over 10 full orbits — energy conservation
;;; Total energy E = (p² + q²)/2 = 1/2 throughout.
(let* ((pi    (acos -1.0))
       (steps (ode-verlet/steps accel-harmonic 1.0 0.0 0.0 (* 20.0 pi) 0.05))
       (energies (map (lambda (tqp)
                        (let ((q (cadr tqp)) (p (cddr tqp)))
                          (* 0.5 (+ (* q q) (* p p)))))
                      steps))
       (e-max (apply max energies))
       (e-min (apply min energies)))
  ; Verlet conserves a modified Hamiltonian; energy oscillates by O(h²)
  (check "verlet: energy max ≈ 0.5" e-max 0.5 5e-4)
  (check "verlet: energy min ≈ 0.5" e-min 0.5 5e-4))

;;; Vector harmonic oscillator: [q1, q2] each oscillating independently
(let* ((pi  (acos -1.0))
       (accel-2d (lambda (t q)
                   (list (- (car q)) (- (cadr q)))))
       (qp  (ode-verlet accel-2d '(1.0 0.5)
                                  '(0.0 0.0)
                                  0.0 (* 2.0 pi) 0.01))
       (q   (car qp)))
  (check "verlet: 2D q1(2π)" (car  q) 1.0 1e-4)
  (check "verlet: 2D q2(2π)" (cadr q) 0.5 1e-4))

;;; /steps: check snapshot count is reasonable
(let* ((pi    (acos -1.0))
       (steps (ode-verlet/steps accel-harmonic 1.0 0.0 0.0 pi 0.1))
       (n     (length steps)))
  (check "verlet/steps: ~32 steps for t=[0,π]" n 32 2))

;;; ════════════════════════════════════════════════════════════
;;; Summary
;;; ════════════════════════════════════════════════════════════

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
