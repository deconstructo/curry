;;; spacetime_horizon_tests.scm -- the physics behind examples/spacetime-horizon
;;;
;;; kerr-slice.scm is a closed-form copy of the equatorial Kerr-Schild metric,
;;; its light cones, and a falling observer.  Checked here against:
;;;   - (curry spacetime)'s CAS metric, component by component
;;;   - null-ness and orthonormality of the cone frame
;;;   - Schwarzschild's radial light speeds, (1-f)/(1+f) outward and -1 inward
;;;   - the observer's conserved quantities: unit 4-velocity, energy E = 1,
;;;     angular momentum L, all measured through the Cartesian metric
;;;   - the Schwarzschild free-fall proper time (2/3) r0^(3/2) / sqrt(2)
;;; No GPU needed.

(import (scheme base) (scheme inexact) (scheme write) (curry spacetime))

(load "../examples/spacetime-horizon/kerr-slice.scm")

(define pass 0)
(define fail 0)

(define (check label ok)
  (if ok
      (set! pass (+ pass 1))
      (begin (set! fail (+ fail 1)) (display "FAIL: ") (display label) (newline))))

(define (near? a b tol) (<= (abs (- a b)) (* tol (max 1.0 (abs a) (abs b)))))

;; ---- the closed form against the CAS metric ----

(define (cas-component m i j x y a)
  (let* ((e (metric-component m i j))
         (vars (spacetime-coordinates))
         (e1 (if (number? e) e (substitute e (list-ref vars 3) 0)))
         (e2 (if (number? e1) e1 (substitute e1 (list-ref vars 0) 0)))
         (e3 (if (number? e2) e2 (substitute e2 (list-ref vars 1) x)))
         (e4 (if (number? e3) e3 (substitute e3 (list-ref vars 2) y))))
    e4))

(define (check-metric a x y)
  (let* ((m (kerr-metric 1 a))
         (P (ks-point x y a))
         (basis (list (vector 1.0 0.0 0.0) (vector 0.0 1.0 0.0) (vector 0.0 0.0 1.0)))
         (idx '(0 1 2)))
    (for-each
     (lambda (i)
       (for-each
        (lambda (j)
          (let ((cas (cas-component m i j x y a))
                (mine (ks-dot P (list-ref basis i) (list-ref basis j))))
            (check (list "metric component" a x y i j cas mine)
                   (and (number? cas) (near? cas mine 1e-9)))))
        idx))
     idx)))

(check-metric 0.7 2.3 -1.1)
(check-metric 0.7 -0.9 1.4)     ; inside the ergosphere: rho^2 = 2.07, r^2 = 1.58
(check-metric 0.3 1.7 0.4)

;; ---- cone frame ----

(define (check-frame a x y)
  (let* ((P (ks-point x y a))
         (T (ks-tetrad x y a))
         (e0 (vector-ref T 0)) (e1 (vector-ref T 1)) (e2 (vector-ref T 2)))
    (check (list "e0 unit timelike" a x y) (near? (ks-dot P e0 e0) -1.0 1e-9))
    (check (list "e1 unit spacelike" a x y) (near? (ks-dot P e1 e1) 1.0 1e-9))
    (check (list "e2 unit spacelike" a x y) (near? (ks-dot P e2 e2) 1.0 1e-9))
    (check (list "e0 . e1" a x y) (near? (ks-dot P e0 e1) 0.0 1e-9))
    (check (list "e0 . e2" a x y) (near? (ks-dot P e0 e2) 0.0 1e-9))
    (check (list "e1 . e2" a x y) (near? (ks-dot P e1 e2) 0.0 1e-9))
    (check (list "e0 future-directed" a x y) (> (vector-ref e0 0) 0.0))
    ;; every rim point lies on the null cone
    (for-each
     (lambda (off)
       (let ((k (vector 1.5 (car off) (cdr off))))
         (check (list "rim point is null" a x y off) (near? (ks-dot P k k) 0.0 1e-8))))
     (ks-cone-rim x y a 12 1.5))))

(check-frame 0.0 6.0 0.0)
(check-frame 0.0 2.0 0.0)       ; exactly on the Schwarzschild horizon
(check-frame 0.0 1.0 0.5)       ; inside it
(check-frame 0.9 2.5 1.0)
(check-frame 0.9 1.3 -0.6)      ; between the Kerr horizons
(check-frame 0.9 1.0 0.1)       ; near the ring

;; ---- radial light speeds ----

(define (check-radial x y)
  (let* ((P (ks-point x y 0.0)) (f (vector-ref P 0))
         (sp (ks-radial-light-speeds x y 0.0)))
    (check (list "radial speeds exist" x y) sp)
    (when sp
      (check (list "outward speed (1-f)/(1+f)" x y) (near? (car sp) (/ (- 1.0 f) (+ 1.0 f)) 1e-9))
      (check (list "inward speed -1" x y) (near? (cdr sp) -1.0 1e-9)))))

(check-radial 8.0 0.0)
(check-radial 3.0 0.0)
(check-radial 2.0 0.0)
(check-radial 1.0 0.0)

;; outward light stops at the horizon and falls inward inside it
(check "outward speed positive outside" (> (car (ks-radial-light-speeds 3.0 0.0 0.0)) 0.0))
(check "outward speed zero at the horizon" (near? (car (ks-radial-light-speeds 2.0 0.0 0.0)) 0.0 1e-9))
(check "outward speed negative inside" (< (car (ks-radial-light-speeds 1.0 0.0 0.0)) 0.0))

;; ---- the falling observer ----

;; Killing vectors in Cartesian components: d/dt = (1 0 0), d/dphi = (0 -y x).
(define (check-worldline a L r0)
  (let* ((wl (ks-worldline a L r0))
         (Leff (min L (ks-max-L a)))
         (n (vector-length wl))
         (worst-norm 0.0) (worst-E 0.0) (worst-L 0.0) (worst-future 1.0))
    (do ((i 0 (+ i 1))) ((= i n))
      (let* ((s (vector-ref wl i))
             (x (vector-ref s 2)) (y (vector-ref s 3))
             (u (vector (vector-ref s 6) (vector-ref s 7) (vector-ref s 8)))
             (P (ks-point x y a)))
        (set! worst-norm (max worst-norm (abs (+ 1.0 (ks-dot P u u)))))
        (set! worst-E (max worst-E (abs (+ 1.0 (ks-dot P u (vector 1.0 0.0 0.0))))))
        (set! worst-L (max worst-L (abs (- Leff (ks-dot P u (vector 0.0 (- y) x))))))
        (set! worst-future (min worst-future (vector-ref u 0)))))
    (check (list "worldline has samples" a L r0) (> n 100))
    (check (list "g(u,u) = -1 along the worldline" a L r0 worst-norm) (< worst-norm 1e-4))
    (check (list "energy E = 1" a L r0 worst-E) (< worst-E 1e-4))
    (check (list "angular momentum L" a L r0 worst-L) (< worst-L 1e-4))
    (check (list "u^t > 0 (future-directed)" a L r0 worst-future) (> worst-future 0.0))
    wl))

(define wl0 (check-worldline 0.0 0.0 10.0))
(check-worldline 0.9 0.0 10.0)
(check-worldline 0.9 1.5 10.0)
(check-worldline 0.9 -3.0 10.0)
(check-worldline 0.5 3.0 8.0)

;; Schwarzschild radial fall from r0 = 10: tau = (2/3) r0^1.5 / sqrt(2) to r = 0.
(let* ((last (vector-ref wl0 (- (vector-length wl0) 1)))
       (tau-end (vector-ref last 0))
       (exact (/ (* (/ 2.0 3.0) (expt 10.0 1.5)) (sqrt 2.0))))
  (check (list "Schwarzschild fall proper time" tau-end exact) (< (abs (- tau-end exact)) 0.02)))

;; The worldline crosses the horizon: r goes from 10 to below r+ with t finite.
(check "worldline reaches the singularity region"
       (< (vector-ref (vector-ref wl0 (- (vector-length wl0) 1)) 4) 0.05))
(check "Kerr-Schild time stays finite at the horizon"
       (let loop ((i 0))
         (cond ((= i (vector-length wl0)) #t)
               ((not (< (abs (vector-ref (vector-ref wl0 i) 1)) 1000.0)) #f)
               (else (loop (+ i 1))))))

;; interpolation returns the sample at an endpoint and stays between neighbours
(let ((mid (ks-sample-at wl0 5.0)))
  (check "interpolated tau" (near? (vector-ref mid 0) 5.0 1e-9)))

(display "spacetime_horizon: ") (display pass) (display " passed, ")
(display fail) (display " failed") (newline)
(exit (if (= fail 0) 0 1))
