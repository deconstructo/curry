;;; (curry gravity scenarios) — Preset initial conditions for the gravity simulator.
;;;
;;; Each scenario procedure returns three values:
;;;   (values d G bodies)
;;; where
;;;   d      : initial spatial dimension (real)
;;;   G      : gravitational constant appropriate for the scenario
;;;   bodies : list of body vectors #(mass pos-vec vel-vec trail color)
;;;
;;; The scenarios correspond to the preset table in §6.2 of the spec.
;;;
;;; Run any scenario via:
;;;   (gravity-load-scenario! 'two-body-circular)
;;; or directly:
;;;   (define-values (d G bodies) (scenario-two-body-circular))

(import (scheme base))
(import (scheme inexact))

;;; ---- Constants ----

(define *PI* 3.14159265358979323846)

;;; ---- Body constructor ----

(define (make-body mass pos vel color)
  (vector mass pos vel '() color))

(define (vzero n)  (make-vector n 0.0))
(define (v3 x y z) (vector x y z))
(define (v2 x y)   (vector x y))

;;; ---- Colour palette ----

(define *palette*
  (vector
    '(1.00 0.90 0.25)   ; 0 gold
    '(0.40 0.72 1.00)   ; 1 sky-blue
    '(0.38 0.95 0.45)   ; 2 green
    '(1.00 0.50 0.28)   ; 3 orange
    '(0.80 0.42 1.00)   ; 4 purple
    '(1.00 0.70 0.70)   ; 5 pink
    '(0.35 1.00 0.90)   ; 6 cyan
    '(0.95 0.95 0.42))) ; 7 lemon

(define (col i) (vector-ref *palette* (modulo i (vector-length *palette*))))

;;; ---- Simple deterministic PRNG (LCG) ----

(define *rng-state* 0)

(define (rng-seed! s) (set! *rng-state* (modulo s (expt 2 32))))

(define (rng-next!)
  (set! *rng-state*
        (modulo (+ (* *rng-state* 1664525) 1013904223) (expt 2 32)))
  (/ (exact->inexact *rng-state*) (exact->inexact (expt 2 32))))

(define (rng-range! lo hi)
  (+ lo (* (rng-next!) (- hi lo))))

;;; ====================================================================
;;; 1. Two-body circular
;;;    Two equal masses in a stable circular orbit.
;;;    Best viewed at d=3; sliding d upward breaks the orbit.
;;; ====================================================================

(define (scenario-two-body-circular)
  (let* ((m   1.0)
         (G   1.0)
         ;; Separation = 2r; each body orbits at radius r about origin.
         ;; Force: F = G*m^2/(2r)^2.  Centripetal: m*v^2/r = F.
         ;; → v = sqrt(G*m / (4r))
         (r   2.5)
         (v   (sqrt (/ (* G m) (* 4.0 r)))))
    (values 3.0 G
            (list
              (make-body m (v3 (- r) 0.0 0.0) (v3 0.0 (- v) 0.0) (col 0))
              (make-body m (v3    r  0.0 0.0) (v3 0.0    v  0.0) (col 1))))))

;;; ====================================================================
;;; 2. Three-body figure-8
;;;    The Chenciner-Montgomery choreographic solution (2000).
;;;    Three equal masses chasing each other along a figure-8.
;;;    Stable only at d=3; breaks immediately for d≠3.
;;;
;;;    Initial conditions (G=1, m=1):
;;;      q1 = (-0.97000436,  0.24308753, 0)
;;;      q2 = (0, 0, 0)
;;;      q3 = ( 0.97000436, -0.24308753, 0)
;;;      v1 = v3 = (vx/2, vy/2, 0)
;;;      v2 = (-vx, -vy, 0)
;;;    where vx = 0.93240737, vy = 0.86473146.
;;; ====================================================================

(define (scenario-three-body-figure-8)
  (let* ((m  1.0)
         (G  1.0)
         (x0 0.97000436)
         (y0 0.24308753)
         (vx 0.93240737)
         (vy 0.86473146))
    (values 3.0 G
            (list
              (make-body m (v3 (- x0)  y0     0.0) (v3 (* 0.5 vx)  (* 0.5 vy)  0.0) (col 0))
              (make-body m (v3  0.0    0.0    0.0) (v3 (- vx)      (- vy)      0.0) (col 1))
              (make-body m (v3  x0    (- y0)  0.0) (v3 (* 0.5 vx)  (* 0.5 vy)  0.0) (col 2))))))

;;; ====================================================================
;;; 3. Binary with test particle
;;;    Heavy binary pair + one light test particle.
;;;    Restricted three-body problem; start at d=3, then explore.
;;; ====================================================================

(define (scenario-binary-test-particle)
  (let* ((M   10.0)   ; heavy star mass
         (m    0.01)  ; test particle mass
         (G    1.0)
         (r    3.0)   ; binary separation / 2
         ;; Binary circular orbit velocity
         (vB  (sqrt (/ (* G M) (* 4.0 r))))
         ;; Test particle at 1.5× the binary radius, given nearly-circular velocity
         (rP  (* 1.5 r))
         (vP  (sqrt (/ (* G (* 2.0 M)) rP))))  ; orbit around CoM
    (values 3.0 G
            (list
              (make-body M (v3 (- r) 0.0 0.0) (v3 0.0 (- vB) 0.0) (col 0))
              (make-body M (v3    r  0.0 0.0) (v3 0.0    vB  0.0) (col 1))
              (make-body m (v3  rP   0.0 0.0) (v3 0.0    vP  0.0) (col 2))))))

;;; ====================================================================
;;; 4. Radial infall
;;;    Bodies falling straight toward each other.
;;;    Demonstrates steepness of force law; works at all d.
;;; ====================================================================

(define (scenario-radial-infall)
  (let* ((G 1.0)
         (sep 4.0))
    (values 3.0 G
            (list
              (make-body 1.0 (v3 (- sep) 0.0 0.0) (v3  0.05 0.0 0.0) (col 0))
              (make-body 1.0 (v3    sep  0.0 0.0) (v3 -0.05 0.0 0.0) (col 1))
              (make-body 0.5 (v3  0.0   (- sep) 0.0) (v3 0.0  0.05 0.0) (col 2))
              (make-body 0.5 (v3  0.0      sep  0.0) (v3 0.0 -0.05 0.0) (col 3))))))

;;; ====================================================================
;;; 5. Random cluster
;;;    N bodies with random positions and small velocities.
;;;    Demonstrates gravitational collapse; collapse speed varies with d.
;;;    Best viewed at d≥3.
;;; ====================================================================

(define (scenario-random-cluster . args)
  (let* ((n   (if (pair? args) (car args) 8))
         (G   1.0)
         (R   3.0))   ; cluster radius
    (rng-seed! 42)
    (values 3.0 G
            (let loop ((i 0) (acc '()))
              (if (= i n) (reverse acc)
                  (let* ((theta (rng-range! 0.0 (* 2.0 *PI*)))
                         (phi   (rng-range! 0.0 *PI*))
                         (r     (* R (expt (rng-next!) 0.333)))
                         (vscale 0.05)
                         (mass  (rng-range! 0.5 2.0))
                         (pos   (v3 (* r (sin phi) (cos theta))
                                    (* r (sin phi) (sin theta))
                                    (* r (cos phi))))
                         (vel   (v3 (* vscale (- (rng-next!) 0.5))
                                    (* vscale (- (rng-next!) 0.5))
                                    (* vscale (- (rng-next!) 0.5)))))
                    (loop (+ i 1) (cons (make-body mass pos vel (col i)) acc))))))))

;;; ====================================================================
;;; 6. Orbit zoo
;;;    Multiple test particles at different radii around a central mass.
;;;    Shows which orbits are stable at d=2.9–3.1 (stability transition).
;;;    Use the dimension slider to sweep through d=2.9 to 3.1.
;;; ====================================================================

(define (scenario-orbit-zoo)
  (let* ((M   100.0)   ; central star
         (G   1.0)
         (radii '(0.5 1.0 1.5 2.0 2.5 3.0 3.5 4.0))
         ;; For 3D: v_circ = sqrt(G*M/r)
         (bodies
           (map-with-index
             (lambda (i r)
               (let ((v (sqrt (/ (* G M) r))))
                 (make-body 0.01
                            (v3 r 0.0 0.0)
                            (v3 0.0 v 0.0)
                            (col (+ i 1)))))
             radii)))
    (values 3.0 G
            (cons (make-body M (vzero 3) (vzero 3) (col 0))
                  bodies))))

;;; Helper: map with index
(define (map-with-index f lst)
  (let loop ((i 0) (ls lst) (acc '()))
    (if (null? ls) (reverse acc)
        (loop (+ i 1) (cdr ls) (cons (f i (car ls)) acc)))))

;;; ====================================================================
;;; 7. Dimension sweep
;;;    Automated slow sweep from d=2 to d=5.
;;;    The simulation is not reset; d changes continuously.
;;;    Returns initial bodies (two-body) and the sweep thunk.
;;;
;;; Usage:
;;;   (define-values (d G bodies) (scenario-dimension-sweep))
;;;   ;; Load into sim, then at each frame call (gravity-dimension-sweep-tick!)
;;; ====================================================================

(define *sweep-d*    2.0)
(define *sweep-dir*  0.002)  ; d units per sim step
(define *sweep-active* #f)

(define (scenario-dimension-sweep)
  (set! *sweep-d*    2.0)
  (set! *sweep-dir*  0.002)
  (set! *sweep-active* #t)
  (define-values (d G bodies) (scenario-two-body-circular))
  (values 2.0 G bodies))

;;; Called each physics step when sweep is active.
;;; Returns new d, bouncing between 2.0 and 5.0.
(define (dimension-sweep-step! current-d)
  (when *sweep-active*
    (set! *sweep-d* (+ *sweep-d* *sweep-dir*))
    (when (or (>= *sweep-d* 5.0) (<= *sweep-d* 2.0))
      (set! *sweep-dir* (- *sweep-dir*))))
  *sweep-d*)

(define (dimension-sweep-active?) *sweep-active*)
(define (dimension-sweep-stop!)   (set! *sweep-active* #f))
(define (dimension-sweep-start!)  (set! *sweep-active* #t))

;;; ====================================================================
;;; Scenario registry
;;; ====================================================================

(define *scenario-registry*
  (list
    (cons 'two-body-circular     scenario-two-body-circular)
    (cons 'three-body-figure-8   scenario-three-body-figure-8)
    (cons 'binary-test-particle  scenario-binary-test-particle)
    (cons 'radial-infall         scenario-radial-infall)
    (cons 'random-cluster        scenario-random-cluster)
    (cons 'orbit-zoo             scenario-orbit-zoo)
    (cons 'dimension-sweep       scenario-dimension-sweep)))

(define (scenario-names)
  (map car *scenario-registry*))

(define (scenario-run name)
  (let ((entry (assq name *scenario-registry*)))
    (if entry
        ((cdr entry))
        (error "gravity: unknown scenario" name))))
