;;; kerr-slice.scm -- the equatorial plane of a Kerr black hole as a
;;; 2+1 dimensional spacetime (t, x, y), in ingoing Kerr-Schild coordinates.
;;;
;;; The metric is the (curry spacetime) one, g = eta + f l⊗l, evaluated at
;;; z = 0 with M = 1:
;;;
;;;   f   = 2/r                     r² = x² + y² - a²   (equatorial plane)
;;;   l   = (1, cos φ, sin φ)       the unit vector at Kerr-Schild azimuth φ,
;;;                                  x + i y = (r + i a) e^{iφ}
;;;   g^-1 = eta^-1 - f L⊗L         L = (-1, cos φ, sin φ), l raised
;;;
;;; Because the metric is written in these coordinates there is no
;;; coordinate singularity at the horizons: a worldline crosses r+ and r-
;;; smoothly, and the light cones can be drawn everywhere.
;;;
;;; Plain top-level definitions (no define-library), meant to be (load)ed.
;;; Names start with ks- so they do not collide with the GUI's names.

;; ---- horizons ----------------------------------------------------------

(define (ks-r-plus a)  (+ 1.0 (sqrt (max 0.0 (- 1.0 (* a a))))))
(define (ks-r-minus a) (- 1.0 (sqrt (max 0.0 (- 1.0 (* a a))))))
(define (ks-rho-of-r r a) (sqrt (+ (* r r) (* a a))))
(define (ks-r-of-rho rho a) (sqrt (max 0.0 (- (* rho rho) (* a a)))))

;; ---- metric at a point (x, y) -------------------------------------------

;; -> (vector f lx ly r); lx, ly are the spatial part of l.
(define (ks-point x y a)
  (let* ((rho2 (+ (* x x) (* y y)))
         (r (sqrt (max 1e-12 (- rho2 (* a a)))))
         (d (+ (* r r) (* a a)))
         (lx (/ (+ (* r x) (* a y)) d))
         (ly (/ (- (* r y) (* a x)) d)))
    (vector (/ 2.0 r) lx ly r)))

;; g(u, v) for 3-vectors (vector t x y) at the point described by P.
(define (ks-dot P u v)
  (let* ((f (vector-ref P 0)) (lx (vector-ref P 1)) (ly (vector-ref P 2))
         (lu (+ (vector-ref u 0) (* lx (vector-ref u 1)) (* ly (vector-ref u 2))))
         (lv (+ (vector-ref v 0) (* lx (vector-ref v 1)) (* ly (vector-ref v 2)))))
    (+ (- (* (vector-ref u 0) (vector-ref v 0)))
       (* (vector-ref u 1) (vector-ref v 1))
       (* (vector-ref u 2) (vector-ref v 2))
       (* f lu lv))))

(define (ks-axpy c u v)                 ; v + c u
  (vector (+ (vector-ref v 0) (* c (vector-ref u 0)))
          (+ (vector-ref v 1) (* c (vector-ref u 1)))
          (+ (vector-ref v 2) (* c (vector-ref u 2)))))

(define (ks-scale c u)
  (vector (* c (vector-ref u 0)) (* c (vector-ref u 1)) (* c (vector-ref u 2))))

;; ---- the light cone at (x, y) --------------------------------------------

;; Orthonormal frame (e0 e1 e2): e0 is timelike and points along the
;; gradient of t, so it is the axis of the cone; e1 and e2 are spacelike.
;; Every future null vector at the point is e0 + cos α e1 + sin α e2.
(define (ks-tetrad x y a)
  (let* ((P (ks-point x y a))
         (f (vector-ref P 0)) (lx (vector-ref P 1)) (ly (vector-ref P 2))
         (k (/ 1.0 (sqrt (+ 1.0 f))))
         (e0 (vector (* k (+ 1.0 f)) (* k (- (* f lx))) (* k (- (* f ly)))))
         (v1 (ks-axpy (ks-dot P (vector 0.0 1.0 0.0) e0) e0 (vector 0.0 1.0 0.0)))
         (e1 (ks-scale (/ 1.0 (sqrt (ks-dot P v1 v1))) v1))
         (v2a (ks-axpy (ks-dot P (vector 0.0 0.0 1.0) e0) e0 (vector 0.0 0.0 1.0)))
         (v2 (ks-axpy (- (ks-dot P v2a e1)) e1 v2a))
         (e2 (ks-scale (/ 1.0 (sqrt (ks-dot P v2 v2))) v2)))
    (vector e0 e1 e2)))

;; Rim of the cone at height h above (x, y): a list of n (dx . dy) offsets.
(define (ks-cone-rim x y a n h)
  (let* ((T (ks-tetrad x y a))
         (e0 (vector-ref T 0)) (e1 (vector-ref T 1)) (e2 (vector-ref T 2)))
    (let loop ((i 0) (acc '()))
      (if (= i n)
          (reverse acc)
          (let* ((al (* 2.0 3.141592653589793 (/ i n)))
                 (c (cos al)) (s (sin al))
                 (kt (+ (vector-ref e0 0) (* c (vector-ref e1 0)) (* s (vector-ref e2 0))))
                 (kx (+ (vector-ref e0 1) (* c (vector-ref e1 1)) (* s (vector-ref e2 1))))
                 (ky (+ (vector-ref e0 2) (* c (vector-ref e1 2)) (* s (vector-ref e2 2)))))
            (loop (+ i 1)
                  (cons (cons (* h (/ kx kt)) (* h (/ ky kt))) acc)))))))

;; Light's speed along the outward radial direction, per unit t: the two
;; roots of (1 + f c²) s² + 2 f c s + (f - 1) = 0, c = l·n.  Returns
;; (cons outward inward) as dρ/dt, or #f when the radial line misses the cone.
(define (ks-radial-light-speeds x y a)
  (let* ((P (ks-point x y a))
         (f (vector-ref P 0))
         (rho (sqrt (+ (* x x) (* y y))))
         (nx (/ x rho)) (ny (/ y rho))
         (c (+ (* (vector-ref P 1) nx) (* (vector-ref P 2) ny)))
         (disc (+ (* f c c) (- 1.0 f))))
    (if (< disc 0.0)
        #f
        (let ((den (+ 1.0 (* f c c))) (sq (sqrt disc)))
          (cons (/ (+ (- (* f c)) sq) den)
                (/ (- (- (* f c)) sq) den))))))

;; ---- a freely falling observer -------------------------------------------
;;
;; Equatorial geodesic with energy E = 1 (dropped from rest at infinity) and
;; angular momentum L, integrated in ingoing Kerr coordinates (v, r, φ):
;;
;;   R(r) = P² - Δ Q,   P = r² + a² - a L,   Q = r² + (L - a)²,   Δ = r² - 2r + a²
;;   (r² dr/dτ)² = R
;;   dv/dτ = [a (L - a) + (r² + a²) W] / r²
;;   dφ/dτ = [(L - a) + a W] / r²,       W = (P + r² ṙ) / Δ
;;
;; W is finite at the horizons; for an ingoing worldline it is computed as
;; Q / (P - r² ṙ), which has no 0/0.

;; For E = 1, P² - Δ Q collapses to 2r³ - L² r² + 2 (L - a)² r.  Written out
;; in full it subtracts two nearly equal numbers near the ring, and
;; differentiating that numerically is useless, so use the simple form.
(define (ks-R r a L)
  (+ (* 2.0 r r r) (- (* L L r r)) (* 2.0 (- L a) (- L a) r)))

;; r̈ = ½ d/dr (R / r⁴) = -1/r² + L²/r³ - 3 (L - a)² / r⁴
(define (ks-rdd r a L)
  (let ((q (* (- L a) (- L a))))
    (+ (- (/ 1.0 (* r r))) (/ (* L L) (* r r r)) (- (/ (* 3.0 q) (* r r r r))))))

(define (ks-W r rd a L)
  (let* ((P (- (+ (* r r) (* a a)) (* a L)))
         (Q (+ (* r r) (* (- L a) (- L a))))
         (D (+ (- (* r r) (* 2.0 r)) (* a a)))
         (s (* r r rd)))
    (if (<= s 0.0)
        (/ Q (- P s))
        (/ (+ P s) (if (< (abs D) 1e-9) 1e-9 D)))))

(define (ks-vdot r rd a L)
  (/ (+ (* a (- L a)) (* (+ (* r r) (* a a)) (ks-W r rd a L))) (* r r)))
(define (ks-phidot r rd a L)
  (/ (+ (- L a) (* a (ks-W r rd a L))) (* r r)))

;; The largest L for which the worldline stays inside this chart.  Ingoing
;; Kerr coordinates cover the crossing of the inner horizon only while
;; P(r-) = 2 r- - a L > 0; a prograde observer with more angular momentum
;; than that leaves through the inner horizon into a region the chart does
;; not reach, and dt/dτ diverges there.  Retrograde L is never limited.
(define (ks-max-L a)
  (if (< a 1e-6) 1e9 (* 0.98 (/ (* 2.0 (ks-r-minus a)) a))))

(define (ks-deriv y a L)                 ; y = #(r rd v phi)
  (let ((r (vector-ref y 0)) (rd (vector-ref y 1)))
    (vector rd (ks-rdd r a L) (ks-vdot r rd a L) (ks-phidot r rd a L))))

(define (ks-rk4-step y h a L)
  (define (adv y k c) (vector (+ (vector-ref y 0) (* c (vector-ref k 0)))
                              (+ (vector-ref y 1) (* c (vector-ref k 1)))
                              (+ (vector-ref y 2) (* c (vector-ref k 2)))
                              (+ (vector-ref y 3) (* c (vector-ref k 3)))))
  (let* ((k1 (ks-deriv y a L))
         (k2 (ks-deriv (adv y k1 (* 0.5 h)) a L))
         (k3 (ks-deriv (adv y k2 (* 0.5 h)) a L))
         (k4 (ks-deriv (adv y k3 h) a L)))
    (vector-map (lambda (y0 a1 b2 c3 d4) (+ y0 (* (/ h 6.0) (+ a1 (* 2.0 b2) (* 2.0 c3) d4))))
                y k1 k2 k3 k4)))

;; One sample of a worldline: #(tau t x y r rdot tdot xdot ydot), with t the
;; Kerr-Schild time (v - r) and (x, y) the Kerr-Schild position.
(define (ks-sample tau y a L)
  (let* ((r (vector-ref y 0)) (rd (vector-ref y 1))
         (v (vector-ref y 2)) (phi (vector-ref y 3))
         (c (cos phi)) (s (sin phi))
         (vd (ks-vdot r rd a L)) (pd (ks-phidot r rd a L)))
    (vector tau (- v r)
            (- (* r c) (* a s)) (+ (* r s) (* a c))
            r rd (- vd rd)
            (- (* rd c) (* (+ (* r s) (* a c)) pd))
            (+ (* rd s) (* (- (* r c) (* a s)) pd)))))

;; Worldline sampled in proper time.  Returns a vector of samples, each
;;   #(tau t x y r rdot tdot xdot ydot)
;; with t the Kerr-Schild time (v - r) and (x, y) the Kerr-Schild position.
;; (L is clamped to ks-max-L.)
;; Stops at r < r-min (the singularity), after 400 of proper time, when
;; the worldline has turned around and climbed back out to 3 r0, or when it
;; turns around inside the horizons and heads back out through r-.
(define (ks-worldline a L0 r0 . opt)
  (let* ((r-min (if (pair? opt) (car opt) 0.04))
         (L (min L0 (ks-max-L a)))
         (R0 (ks-R r0 a L))
         (y0 (vector r0 (- (/ (sqrt (max 0.0 R0)) (* r0 r0))) r0 0.0)))
    (let loop ((y y0) (tau 0.0) (n 0) (acc (list (ks-sample 0.0 y0 a L))))
      (let ((r (vector-ref y 0)))
        (if (or (< r r-min) (> n 60000) (> r (* 3.0 r0)) (> tau 400.0)
                ;; turned around inside the outer horizon and heading back out:
                ;; this leaves through r- into a region ingoing coordinates
                ;; do not cover, so the worldline ends here
                (and (> a 0.0) (> (vector-ref y 1) 0.0)
                     (> r (* 0.97 (ks-r-minus a))) (< r (ks-r-plus a))))
            (list->vector (reverse acc))
            ;; Near the singularity ṙ grows like r^(-3/2) (the ring) or
            ;; r^(-1/2) (the point), so each step may change r by at most
            ;; a small fraction of itself.
            (let* ((h (max 1e-7 (min 0.02 (/ (* 0.004 r) (max (abs (vector-ref y 1)) 1e-3)))))
                   (y2 (ks-rk4-step y h a L))
                   (tau2 (+ tau h)))
              (loop y2 tau2 (+ n 1) (cons (ks-sample tau2 y2 a L) acc))))))))

;; Linear interpolation of a worldline at proper time tau (clamped).
;; -> a sample vector of the same shape.
(define (ks-sample-at wl tau)
  (let* ((n (vector-length wl)))
    (cond ((<= tau (vector-ref (vector-ref wl 0) 0)) (vector-ref wl 0))
          ((>= tau (vector-ref (vector-ref wl (- n 1)) 0)) (vector-ref wl (- n 1)))
          (else
           (let loop ((lo 0) (hi (- n 1)))
             (if (= (- hi lo) 1)
                 (let* ((A (vector-ref wl lo)) (B (vector-ref wl hi))
                        (u (/ (- tau (vector-ref A 0)) (- (vector-ref B 0) (vector-ref A 0)))))
                   (vector-map (lambda (p q) (+ p (* u (- q p)))) A B))
                 (let ((mid (quotient (+ lo hi) 2)))
                   (if (<= (vector-ref (vector-ref wl mid) 0) tau)
                       (loop mid hi)
                       (loop lo mid)))))))))
