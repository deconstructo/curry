;;; (curry ode) — Ordinary Differential Equation solvers
;;;
;;; šiddu ša ikkibu — the tablet of forbidden rates
;;;
;;; Solves initial-value problems of the form  dy/dt = f(t, y)
;;; where y may be a number (scalar ODE) or a list of numbers (system).
;;; All methods work transparently with Curry's numeric tower — exact
;;; rationals, complex numbers, and symbolic expressions are all valid y.
;;;
;;; Methods:
;;;   Euler      — 1st-order, fixed step; educational only
;;;   RK4        — classical 4th-order Runge-Kutta, fixed step
;;;   RK45       — Dormand-Prince adaptive step (5th-order, error O(h⁵))
;;;   Verlet     — velocity-Verlet, symplectic; for Hamiltonian systems
;;;
;;; API (return final y at t1):
;;;   (ode-euler  f y0 t0 t1 h)            → y
;;;   (ode-rk4    f y0 t0 t1 h)            → y
;;;   (ode-rk45   f y0 t0 t1)              → y  (tol=1e-6)
;;;   (ode-rk45   f y0 t0 t1 tol)          → y
;;;   (ode-verlet accel q0 p0 t0 t1 h)     → (q . p)
;;;
;;; /steps variants return ((t . y) ...) snapshots at every accepted step:
;;;   (ode-euler/steps   f y0 t0 t1 h)     → list
;;;   (ode-rk4/steps     f y0 t0 t1 h)     → list
;;;   (ode-rk45/steps    f y0 t0 t1)       → list
;;;   (ode-rk45/steps    f y0 t0 t1 tol)   → list
;;;   (ode-verlet/steps  accel q0 p0 t0 t1 h) → ((t q . p) ...)

(import (scheme base))
(import (scheme inexact))

;;; ════════════════════════════════════════════════════════════
;;; § 1  GENERALISED VECTOR ARITHMETIC
;;;       Works for scalar y (a number) or vector y (a list).
;;; ════════════════════════════════════════════════════════════

(define (y+ a b)
  (if (number? a) (+ a b) (map + a b)))

(define (y- a b)
  (if (number? a) (- a b) (map - a b)))

(define (y* s y)
  (if (number? y) (* s y) (map (lambda (x) (* s x)) y)))

;;; L2 norm (scalar: absolute value; list: Euclidean norm)
(define (y-norm y)
  (if (number? y)
      (abs (inexact y))
      (sqrt (apply + (map (lambda (x) (let ((v (inexact x))) (* v v))) y)))))

;;; ════════════════════════════════════════════════════════════
;;; § 2  EULER — first-order, fixed step
;;;       y_{n+1} = y_n + h·f(t_n, y_n)
;;; ════════════════════════════════════════════════════════════

(define (ode-euler f y0 t0 t1 h)
  (let ((t0f (inexact t0)) (t1f (inexact t1)) (hf (inexact h)))
    (let loop ((t t0f) (y y0))
      (if (>= t t1f)
          y
          (let ((step (min hf (- t1f t))))
            (loop (+ t step)
                  (y+ y (y* step (f t y)))))))))

(define (ode-euler/steps f y0 t0 t1 h)
  (let ((t0f (inexact t0)) (t1f (inexact t1)) (hf (inexact h)))
    (let loop ((t t0f) (y y0) (acc (list (cons t0f y0))))
      (if (>= t t1f)
          (reverse acc)
          (let* ((step (min hf (- t1f t)))
                 (y1   (y+ y (y* step (f t y))))
                 (t1*  (+ t step)))
            (loop t1* y1 (cons (cons t1* y1) acc)))))))

;;; ════════════════════════════════════════════════════════════
;;; § 3  CLASSICAL RK4 — fourth-order, fixed step
;;;
;;;   k1 = f(t,       y)
;;;   k2 = f(t + h/2, y + h/2·k1)
;;;   k3 = f(t + h/2, y + h/2·k2)
;;;   k4 = f(t + h,   y + h·k3)
;;;   y_{n+1} = y + (h/6)·(k1 + 2k2 + 2k3 + k4)
;;; ════════════════════════════════════════════════════════════

(define (rk4-step f t y h)
  (let* ((h/2 (* 0.5 h))
         (k1  (f t y))
         (k2  (f (+ t h/2) (y+ y (y* h/2 k1))))
         (k3  (f (+ t h/2) (y+ y (y* h/2 k2))))
         (k4  (f (+ t h)   (y+ y (y* h   k3)))))
    (y+ y (y* (/ h 6.0)
              (y+ k1 (y+ (y* 2.0 k2) (y+ (y* 2.0 k3) k4)))))))

(define (ode-rk4 f y0 t0 t1 h)
  (let ((t0f (inexact t0)) (t1f (inexact t1)) (hf (inexact h)))
    (let loop ((t t0f) (y y0))
      (if (>= t t1f)
          y
          (let ((step (min hf (- t1f t))))
            (loop (+ t step) (rk4-step f t y step)))))))

(define (ode-rk4/steps f y0 t0 t1 h)
  (let ((t0f (inexact t0)) (t1f (inexact t1)) (hf (inexact h)))
    (let loop ((t t0f) (y y0) (acc (list (cons t0f y0))))
      (if (>= t t1f)
          (reverse acc)
          (let* ((step (min hf (- t1f t)))
                 (y1   (rk4-step f t y step))
                 (t1*  (+ t step)))
            (loop t1* y1 (cons (cons t1* y1) acc)))))))

;;; ════════════════════════════════════════════════════════════
;;; § 4  DORMAND-PRINCE RK45 — adaptive step size
;;;
;;; The 5th-order Dormand-Prince method (DOPRI5) with an embedded
;;; 4th-order solution for error estimation and automatic step control.
;;; This is the algorithm behind MATLAB's ode45 and SciPy's RK45.
;;;
;;; Butcher tableau (Dormand & Prince 1980):
;;;   c:  0   1/5   3/10   4/5   8/9   1   1
;;;   5th-order weights:  35/384  0  500/1113  125/192  −2187/6784  11/84  0
;;;   4th-order weights:  5179/57600  0  7571/16695  393/640  −92097/339200  187/2100  1/40
;;;
;;; Step-size control:  h_new = h · 0.9 · (tol/err)^(1/5)
;;;   clamped to [h/10, 10h] per step.
;;; ════════════════════════════════════════════════════════════

(define (dp-step f t y h)
  ;; Returns (values y5 err-norm) — 5th-order advance + error estimate
  (let* ((k1 (f t y))
         (k2 (f (+ t (* 1/5 h))
                (y+ y (y* (* 1/5 h) k1))))
         (k3 (f (+ t (* 3/10 h))
                (y+ y (y+ (y* (* 3/40  h) k1)
                           (y* (* 9/40  h) k2)))))
         (k4 (f (+ t (* 4/5 h))
                (y+ y (y+ (y* (*  44/45   h) k1)
                           (y+ (y* (* -56/15   h) k2)
                               (y* (*  32/9    h) k3))))))
         (k5 (f (+ t (* 8/9 h))
                (y+ y (y+ (y* (*  19372/6561   h) k1)
                           (y+ (y* (* -25360/2187  h) k2)
                               (y+ (y* (*  64448/6561  h) k3)
                                   (y* (*   -212/729   h) k4)))))))
         (k6 (f (+ t h)
                (y+ y (y+ (y* (*  9017/3168    h) k1)
                           (y+ (y* (*  -355/33    h) k2)
                               (y+ (y* (* 46732/5247   h) k3)
                                   (y+ (y* (*    49/176   h) k4)
                                       (y* (* -5103/18656 h) k5)))))))))
    ;; 5th-order solution
    (let* ((y5 (y+ y (y+ (y* (*    35/384   h) k1)
                          (y+ (y* (*   500/1113  h) k3)
                              (y+ (y* (*   125/192  h) k4)
                                  (y+ (y* (* -2187/6784  h) k5)
                                      (y* (*    11/84   h) k6)))))))
           ;; k7 = f(t+h, y5) — FSAL; also needed for 4th-order error estimate
           (k7 (f (+ t h) y5))
           ;; 4th-order solution for error estimate
           (y4 (y+ y (y+ (y* (*  5179/57600   h) k1)
                          (y+ (y* (*  7571/16695   h) k3)
                              (y+ (y* (*   393/640    h) k4)
                                  (y+ (y* (* -92097/339200 h) k5)
                                      (y+ (y* (*   187/2100   h) k6)
                                          (y* (*     1/40    h) k7)))))))))
      (values y5 (y-norm (y- y5 y4))))))

(define (ode-rk45 f y0 t0 t1 . opts)
  (let* ((tol (if (pair? opts) (inexact (car opts)) 1e-6))
         (t0f (inexact t0))
         (t1f (inexact t1))
         (h0  (* 0.1 (- t1f t0f))))  ; initial step: 1/10 of interval
    (let loop ((t t0f) (y y0) (h h0))
      (if (>= t t1f)
          y
          (let ((h-try (min h (- t1f t))))
            (call-with-values (lambda () (dp-step f t y h-try))
              (lambda (y5 err)
                (if (<= err tol)
                    ;; Accept — grow step cautiously
                    (let ((h-new (* h-try 0.9
                                    (expt (/ tol (max err 1e-20)) 0.2))))
                      (loop (+ t h-try) y5
                            (min (* 10.0 h-try) (max h-new h-try))))
                    ;; Reject — shrink step
                    (let ((h-new (* h-try 0.9
                                    (expt (/ tol err) 0.25))))
                      (loop t y (max (* 0.1 h-try) h-new)))))))))))

(define (ode-rk45/steps f y0 t0 t1 . opts)
  (let* ((tol (if (pair? opts) (inexact (car opts)) 1e-6))
         (t0f (inexact t0))
         (t1f (inexact t1))
         (h0  (* 0.1 (- t1f t0f))))
    (let loop ((t t0f) (y y0) (h h0) (acc (list (cons t0f y0))))
      (if (>= t t1f)
          (reverse acc)
          (let ((h-try (min h (- t1f t))))
            (call-with-values (lambda () (dp-step f t y h-try))
              (lambda (y5 err)
                (if (<= err tol)
                    (let* ((t1*   (+ t h-try))
                           (h-new (* h-try 0.9
                                     (expt (/ tol (max err 1e-20)) 0.2))))
                      (loop t1* y5
                            (min (* 10.0 h-try) (max h-new h-try))
                            (cons (cons t1* y5) acc)))
                    (let ((h-new (* h-try 0.9
                                    (expt (/ tol err) 0.25))))
                      (loop t y (max (* 0.1 h-try) h-new) acc))))))))))

;;; ════════════════════════════════════════════════════════════
;;; § 5  VELOCITY-VERLET — symplectic integrator
;;;
;;; For Hamiltonian systems with position q and velocity/momentum p:
;;;   a_n   = accel(t_n, q_n)
;;;   q_{n+1} = q_n + h·p_n + (h²/2)·a_n
;;;   a_{n+1} = accel(t_{n+1}, q_{n+1})
;;;   p_{n+1} = p_n + (h/2)·(a_n + a_{n+1})
;;;
;;; Symplectic means it exactly conserves a modified Hamiltonian,
;;; so total energy drifts as O(h²) over long integrations rather
;;; than growing unboundedly as with RK methods.
;;;
;;; (ode-verlet accel q0 p0 t0 t1 h) → (q . p)
;;; (ode-verlet/steps accel q0 p0 t0 t1 h) → ((t q . p) ...)
;;;   where q and p may each be numbers or lists.
;;; ════════════════════════════════════════════════════════════

(define (verlet-step accel t q p h)
  (let* ((a0 (accel t q))
         (h2 (* 0.5 h h))
         (q1 (y+ q (y+ (y* h p) (y* h2 a0))))
         (a1 (accel (+ t h) q1))
         (p1 (y+ p (y* (* 0.5 h) (y+ a0 a1)))))
    (cons q1 p1)))

(define (ode-verlet accel q0 p0 t0 t1 h)
  (let ((t0f (inexact t0)) (t1f (inexact t1)) (hf (inexact h)))
    (let loop ((t t0f) (q q0) (p p0))
      (if (>= t t1f)
          (cons q p)
          (let* ((step (min hf (- t1f t)))
                 (qp   (verlet-step accel t q p step)))
            (loop (+ t step) (car qp) (cdr qp)))))))

(define (ode-verlet/steps accel q0 p0 t0 t1 h)
  (let ((t0f (inexact t0)) (t1f (inexact t1)) (hf (inexact h)))
    (let loop ((t t0f) (q q0) (p p0)
               (acc (list (cons t0f (cons q0 p0)))))
      (if (>= t t1f)
          (reverse acc)
          (let* ((step (min hf (- t1f t)))
                 (qp   (verlet-step accel t q p step))
                 (t1*  (+ t step)))
            (loop t1* (car qp) (cdr qp)
                  (cons (cons t1* qp) acc)))))))
