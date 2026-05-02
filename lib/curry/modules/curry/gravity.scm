;;; (curry gravity) — Continuous-Dimension Gravitational Physics Simulator
;;;
;;; ṭuppi ša šamê u erṣetim — Tablet of Heaven and Earth
;;;
;;; A real-time N-body gravitational simulator in which the number of spatial
;;; dimensions d is a continuous parameter (2.0 to 6.0, including non-integer
;;; values).  Computes gravitational dynamics in d dimensions, projects to 3D
;;; via iterative perspective division, and renders via the Qt6/QRhi pipeline.
;;;
;;; The force law generalises from Gauss's law:
;;;   F(r) = -G_d * M * m / r^(d-1)
;;; giving, with softening ε to prevent singularities:
;;;   F_ij = -G_d * m_i * m_j * (r_i - r_j) / (|r_i - r_j|^2 + ε²)^(d/2)
;;;
;;; Integration uses velocity-Verlet (leapfrog), which is symplectic.
;;;
;;; Entry point:
;;;   (gravity-run!)                    ; launch standalone window
;;;   (gravity-run! 'three-body-figure-8) ; with a preset scenario
;;;
;;; REPL API (live during simulation):
;;;   (gravity-dimension)   (set-dimension! 3.5)
;;;   (gravity-bodies)      (add-body! mass pos vel)   (remove-body! id)
;;;   (set-g! G)
;;;   (total-energy)        (angular-momentum)
;;;   (orbit-period id)     (lyapunov-exponent id)
;;;   (start-recording! "file.csv")     (stop-recording!)
;;;   (screenshot! "frame.png")

(import (curry qt6))
(import (curry qt6 stereo))
(import (curry gravity scenarios))
(import (scheme base))
(import (scheme inexact))
(import (scheme write))

;;; ════════════════════════════════════════════════════════════
;;; § 1  SIMULATION PARAMETERS
;;; ════════════════════════════════════════════════════════════

(define *d*              3.0)   ; spatial dimension (real or surreal)
(define *G*              1.0)   ; gravitational constant
(define *eps*            0.15)  ; force softening length
(define *dt*             0.005) ; timestep (adaptive when *adaptive-dt*=#t)
(define *adaptive-dt*    #t)    ; enable adaptive timestep (§4.3)
(define *dt-safety*      0.3)   ; safety factor for adaptive dt
(define *paused*         #f)
(define *trails*         #t)
(define *trail-max*      180)   ; maximum trail length (positions)
(define *speed*          3)     ; physics steps per render frame
(define *bodies*         '())   ; list of body vectors
(define *frame-count*    0)
(define *stereo*         #f)    ; stereo-renderer or #f for mono
(define *show-field*     #f)    ; show gravitational field lines
(define *surreal-mode*   #f)    ; use surreal ε perturbation around integer d
(define *surreal-eps-n*  0)     ; integer n: d = floor(d_real) + n*ε
(define *view-scale*     80.0)  ; pixels per world unit
(define *recording-port* #f)    ; output port or #f

;;; For energy tracking (energy conservation error in HUD)
(define *energy-t0*      0.0)
(define *energy-now*     0.0)

;;; ════════════════════════════════════════════════════════════
;;; § 2  BODY REPRESENTATION: #(mass pos vel trail color)
;;; ════════════════════════════════════════════════════════════

(define (body-mass  b) (vector-ref b 0))
(define (body-pos   b) (vector-ref b 1))
(define (body-vel   b) (vector-ref b 2))
(define (body-trail b) (vector-ref b 3))
(define (body-color b) (vector-ref b 4))

(define (body-set-pos!   b v) (vector-set! b 1 v))
(define (body-set-vel!   b v) (vector-set! b 2 v))
(define (body-set-trail! b t) (vector-set! b 3 t))

;;; Dimension of the position space for a real d value.
;;; Non-integer d uses ⌈d⌉ spatial dimensions (analytically continued force law).
(define (dim-for-d d)
  (inexact->exact (ceiling (max 2.0 (inexact d)))))

;;; ════════════════════════════════════════════════════════════
;;; § 3  VECTOR MATH  (vectors as Scheme vectors of doubles)
;;; ════════════════════════════════════════════════════════════

(define (vzero n) (make-vector n 0.0))

(define (vec-dist2 a b)
  (let loop ((i 0) (s 0.0))
    (if (= i (vector-length a)) s
        (let ((d (- (vector-ref a i) (vector-ref b i))))
          (loop (+ i 1) (+ s (* d d)))))))

(define (vec-norm2 a)
  (let loop ((i 0) (s 0.0))
    (if (= i (vector-length a)) s
        (loop (+ i 1) (+ s (* (vector-ref a i) (vector-ref a i)))))))

(define (vec-add! dst src scale)
  ;; dst[k] += src[k] * scale  (in-place)
  (let loop ((i 0))
    (when (< i (vector-length dst))
      (vector-set! dst i (+ (vector-ref dst i)
                             (* (vector-ref src i) scale)))
      (loop (+ i 1)))))

(define (vec-copy v)
  (let* ((n (vector-length v)) (w (make-vector n 0.0)))
    (do ((i 0 (+ i 1))) ((= i n) w)
      (vector-set! w i (vector-ref v i)))))

;;; Project a (dim-for-d d)-dimensional vector to 3D.
;;; For D ≤ 3: embed (pad extra dims with 0).
;;; For D > 3: iteratively project via perspective division until 3D.
;;;
;;; The focal-lengths vector has one entry per projection step.
;;; Spec §5.1: x_k = x_k * f / (f + x_D)  for k = 1..D-1

(define (project-to-3d pos focal-lengths)
  (let ((D (vector-length pos)))
    (cond
      ((< D 3)
       (let ((out (make-vector 3 0.0)))
         (do ((k 0 (+ k 1))) ((= k D) out)
           (vector-set! out k (vector-ref pos k)))))
      ((= D 3) pos)
      (else
       (let loop ((cur pos) (step 0))
         (let ((d (vector-length cur)))
           (if (<= d 3)
               cur
               (let* ((f    (if (< step (vector-length focal-lengths))
                                (vector-ref focal-lengths step)
                                4.0))
                      (xd   (vector-ref cur (- d 1)))
                      (scale (/ f (+ f xd)))
                      (next (make-vector (- d 1) 0.0)))
                 (do ((k 0 (+ k 1))) ((= k (- d 1)) next)
                   (vector-set! next k (* (vector-ref cur k) scale)))
                 (loop next (+ step 1))))))))))

;;; Project a 3D point to 2D screen with optional eye offset for stereo.
;;; Returns (cons screen-x screen-y).
(define (project-3d-screen p3 cx cy scale-px eye-offset-wu f3d)
  ;; f3d: 3D→2D focal length (depth perspective)
  ;; eye-offset-wu: camera horizontal offset in world units
  (let* ((x  (vector-ref p3 0))
         (y  (vector-ref p3 1))
         (z  (vector-ref p3 2))
         (w  (/ f3d (+ f3d z)))
         (sx (+ cx (* scale-px (- x eye-offset-wu) w)))
         (sy (+ cy (* scale-px (- y) w))))
    (cons sx sy)))

;;; ════════════════════════════════════════════════════════════
;;; § 4  PHYSICS: FORCE COMPUTATION & LEAPFROG INTEGRATION
;;; ════════════════════════════════════════════════════════════

;;; Acceleration on body i due to body j (§4.1 with softening):
;;;   a_ij = -G_d * m_j * (r_i - r_j) / (|r_i - r_j|^2 + ε²)^(d/2)

(define (pairwise-accel ri rj mj G-d d eps)
  (let* ((n    (vector-length ri))
         (r2   (+ (vec-dist2 ri rj) (* eps eps)))
         (denom (expt r2 (/ (inexact d) 2.0)))
         (out  (make-vector n 0.0)))
    (do ((k 0 (+ k 1))) ((= k n) out)
      (vector-set! out k
        (/ (* (- G-d) mj (- (vector-ref ri k) (vector-ref rj k)))
           denom)))))

;;; Compute O(N²) pairwise accelerations for all bodies.
;;; Returns a list of acceleration vectors, one per body.

(define (compute-all-accels bodies d G eps)
  (map (lambda (bi)
         (let ((ai (vzero (vector-length (body-pos bi)))))
           (for-each
             (lambda (bj)
               (when (not (eq? bi bj))
                 (vec-add! ai
                           (pairwise-accel (body-pos bi) (body-pos bj)
                                           (body-mass bj) G d eps)
                           1.0)))
             bodies)
           ai))
       bodies))

;;; Leapfrog (velocity-Verlet) integrator — symplectic (§4.2):
;;;   v(t+½dt) = v(t) + a(t)·dt/2
;;;   x(t+dt)  = x(t) + v(t+½dt)·dt
;;;   a(t+dt)  = F(x(t+dt)) / m
;;;   v(t+dt)  = v(t+½dt) + a(t+dt)·dt/2

(define (leapfrog-step! bodies d G eps dt)
  (let ((a0 (compute-all-accels bodies d G eps)))
    ;; Kick: v += a * dt/2
    (for-each (lambda (b a) (vec-add! (body-vel b) a (* 0.5 dt))) bodies a0)
    ;; Drift: x += v * dt
    (for-each (lambda (b) (vec-add! (body-pos b) (body-vel b) dt)) bodies)
    ;; Recompute accelerations at new positions
    (let ((a1 (compute-all-accels bodies d G eps)))
      ;; Kick: v += a_new * dt/2
      (for-each (lambda (b a) (vec-add! (body-vel b) a (* 0.5 dt))) bodies a1)
      a1)))   ; return for energy tracking

;;; Adaptive timestep (§4.3): dt = min_i(|r_i| / |v_i|) * safety_factor

(define (adaptive-dt bodies safety)
  (let ((dt-min +inf.0))
    (for-each
      (lambda (b)
        (let* ((r  (sqrt (vec-norm2 (body-pos b))))
               (v  (sqrt (vec-norm2 (body-vel b))))
               (dt (if (< v 1e-12) +inf.0 (* safety (/ r v)))))
          (set! dt-min (min dt-min dt))))
      bodies)
    (if (> dt-min 1e10) 0.005 (min 0.02 (max 1e-5 dt-min)))))

;;; Update trails (keep last *trail-max* xy screen positions from 3D projection)

(define *focal-lengths* (vector 4.0 4.0 4.0))  ; d→3D focal lengths, one per step

(define (update-trails! bodies cx cy scale)
  (when *trails*
    (for-each
      (lambda (b)
        (let* ((p3 (project-to-3d (body-pos b) *focal-lengths*))
               (sc (project-3d-screen p3 cx cy scale 0.0 3.0))
               (trail (body-trail b))
               (new-trail (cons sc trail)))
          (body-set-trail! b
            (if (> (length new-trail) *trail-max*)
                (list-head new-trail *trail-max*)
                new-trail))))
      bodies)))

;;; ════════════════════════════════════════════════════════════
;;; § 5  VISUAL ENCODING OF DIMENSION  (§5.3)
;;; ════════════════════════════════════════════════════════════

;;; Background colour gradient: cool (d=2) → familiar dark (d=3) → warm (d=6)
(define (dim-bg-color d)
  (let ((t (/ (- (min (max (inexact d) 2.0) 6.0) 2.0) 4.0)))  ; [0,1]
    (cond
      ((< t 0.25)   ; d=2..3: cool blue → near-black
       (let ((u (* t 4.0)))
         (values (+ 0.08 (* u -0.06)) (+ 0.05 (* u -0.03)) (+ 0.18 (* u -0.10)))))
      ((< t 0.5)    ; d=3..4: near-black → warm dark
       (let ((u (* (- t 0.25) 4.0)))
         (values (+ 0.02 (* u 0.10)) (+ 0.02 (* u 0.03)) (+ 0.08 (* u -0.06)))))
      (else          ; d=4..6: warm dark → deep orange-red
       (let ((u (min 1.0 (* (- t 0.5) 2.0))))
         (values (+ 0.12 (* u 0.07)) (+ 0.05 (* u -0.03)) (+ 0.02 (* u -0.01))))))))

;;; Trail colour for a body at current dimension d: hue shifts with d
(define (dim-trail-color d)
  (let ((t (/ (- (min (max (inexact d) 2.0) 6.0) 2.0) 4.0)))
    (list (+ 0.25 (* t 0.65))
          (+ 0.65 (* t -0.35))
          (+ 0.90 (* t -0.70)))))

;;; Stability indicator text
(define (stability-label d)
  (cond
    ((< (inexact d) 2.5) "stable (d<3)")
    ((< (inexact d) 3.0) "marginal")
    ((< (inexact d) 3.05) "d ≈ 3  classical")
    (else "UNSTABLE (d>3)")))  ; Akkadian: ḥarrānum ul kênat

;;; ════════════════════════════════════════════════════════════
;;; § 6  DRAWING
;;; ════════════════════════════════════════════════════════════

(define (lerp a b t) (+ a (* (- b a) t)))

;;; Reference grid: planar at d≈2, cubic at d≈3, hypercubic hints at d≥4

(define (draw-grid! painter w h cx cy scale d)
  (let* ((t   (/ (- (min (max (inexact d) 2.0) 4.0) 2.0) 2.0))
         (alpha (lerp 0.04 0.12 (- 1.0 t)))
         (step  (* scale 1.0))
         (n     (inexact->exact (ceiling (/ (max w h) step)))))
    (gfx-set-pen-color! painter 0.3 0.4 0.6 alpha)
    (gfx-set-pen-width! painter 0.5)
    ;; Grid lines
    (do ((i (- n) (+ i 1)))
        ((> i n))
      (let* ((x (+ cx (* i step)))
             (y (+ cy (* i step))))
        (gfx-draw-line! painter x 0.0 x (exact->inexact h))
        (gfx-draw-line! painter 0.0 y (exact->inexact w) y)))
    ;; At d≥3: draw a reference cube outline projected to screen
    (when (>= (inexact d) 2.8)
      (let* ((cube-alpha (min 1.0 (* (- (inexact d) 2.8) 3.0)))
             (cs (* scale 1.5)))
        (gfx-set-pen-color! painter 0.3 0.5 0.7 (* alpha cube-alpha 1.5))
        (gfx-set-pen-width! painter 0.8)
        ;; Bottom face
        (gfx-draw-rect! painter (- cx cs) (- cy cs) (* 2.0 cs) (* 2.0 cs))
        ;; Top face (offset to simulate depth)
        (let ((off (* cs 0.4)))
          (gfx-draw-rect! painter (+ (- cx cs) off) (+ (- cy cs) (- off))
                                  (* 2.0 cs) (* 2.0 cs))
          ;; Connecting edges
          (for-each (lambda (dx dy)
            (gfx-draw-line! painter
              (+ cx (* dx cs)) (+ cy (* dy cs))
              (+ cx (* dx cs) off) (+ cy (* dy cs) (- off))))
            '(-1  1  1 -1) '(-1 -1  1  1)))))))

;;; Optional gravitational field lines (§5.3, *show-field* toggle)

(define (draw-field-lines! painter cx cy scale d G bodies)
  (let* ((grid-n 10)
         (step   (* scale 0.6))
         (da     (* step 0.3)))   ; arrow length
    (gfx-set-pen-color! painter 0.6 0.5 0.2 0.18)
    (gfx-set-pen-width! painter 0.8)
    (do ((gy 0 (+ gy 1)))
        ((= gy grid-n))
      (do ((gx 0 (+ gx 1)))
          ((= gx grid-n))
        (let* ((wx (/ (* (- gx (/ (- grid-n 1) 2.0)) scale 2.0) 1.0))
               (wy (/ (* (- gy (/ (- grid-n 1) 2.0)) scale 2.0) 1.0))
               (rp (vector wx wy 0.0))
               (ax 0.0)
               (ay 0.0))
          (for-each
            (lambda (b)
              (let* ((ri rp)
                     (rj (body-pos b))
                     (dx (- (vector-ref rj 0) (vector-ref ri 0)))
                     (dy (- (if (>= (vector-length rj) 2) (vector-ref rj 1) 0.0)
                            (vector-ref ri 1)))
                     (r2 (max 0.01 (+ (* dx dx) (* dy dy))))
                     (r  (sqrt r2))
                     (mag (/ (* G (body-mass b)) (expt (+ r2 0.0001) (/ (inexact d) 2.0)))))
                (set! ax (+ ax (* mag (/ dx r))))
                (set! ay (+ ay (* mag (/ dy r))))))
            bodies)
          (let* ((amag (sqrt (+ (* ax ax) (* ay ay))))
                 (sx   (+ cx wx))
                 (sy   (+ cy (- wy))))
            (when (> amag 1e-6)
              (let* ((norm (/ da amag))
                     (ex   (* ax norm))
                     (ey   (* (- ay) norm)))
                (gfx-draw-line! painter sx sy (+ sx ex) (+ sy ey))))))))))

;;; Orbital trails

(define (draw-trails! painter)
  (for-each
    (lambda (b)
      (let ((trail (body-trail b))
            (col   (body-color b)))
        (when (pair? trail)
          (let loop ((pts trail) (alpha 0.7))
            (when (and (pair? pts) (> alpha 0.01))
              (let* ((pt (car pts))
                     (sx (car pt))
                     (sy (cdr pt)))
                (gfx-set-color! painter
                  (car col) (cadr col) (caddr col) (* alpha 0.06))
                (gfx-fill-circle! painter sx sy 1.8))
              (loop (cdr pts) (* alpha 0.968)))))))
    *bodies*))

;;; Bodies: glow halo + solid disc

(define (draw-bodies! painter cx cy scale eye-offset-px)
  (for-each
    (lambda (b)
      (let* ((p3  (project-to-3d (body-pos b) *focal-lengths*))
             (sc  (project-3d-screen p3 cx cy scale
                                     (/ eye-offset-px scale) 3.0))
             (sx  (car sc))
             (sy  (cdr sc))
             (m   (body-mass b))
             (col (body-color b))
             (r   (max 2.5 (* 3.2 (expt (/ m 5.0) 0.333)))))
        ;; Glow halo
        (gfx-set-color! painter (car col) (cadr col) (caddr col) 0.12)
        (gfx-fill-circle! painter sx sy (* r 2.8))
        ;; Solid core
        (gfx-set-color! painter (car col) (cadr col) (caddr col) 1.0)
        (gfx-fill-circle! painter sx sy r)))
    *bodies*))

;;; HUD (§5.4)

(define (draw-hud! painter w d G energy-err)
  (gfx-set-color! painter 0.55 0.68 0.85 0.88)
  (gfx-set-font!  painter "Monospace" 11)
  (let* ((d-str  (let ((dv (inexact d)))
                   (if *surreal-mode*
                       (string-append (number->string (inexact->exact (floor dv)))
                                      (if (>= *surreal-eps-n* 0) "+" "")
                                      (number->string *surreal-eps-n*) "ε")
                       (let ((s (number->string (round (* dv 10000.0)))))
                         (string-append (substring s 0 (min (string-length s) 6)))))))
         (stab   (stability-label d))
         (lines
           (list
             (string-append "d = " d-str "    " stab)
             (string-append "G = " (number->string G)
                            "   ε = " (number->string *eps*))
             (string-append "force ∝ 1/r^" (number->string (- (inexact d) 1.0)))
             (string-append "ΔE/E₀ = " (number->string (round (* energy-err 1e6))))
             (string-append "bodies = " (number->string (length *bodies*))
                            "   stereo: "
                            (if *stereo* (symbol->string (stereo-mode *stereo*)) "off"))
             (string-append "frame " (number->string *frame-count*)))))
    (let loop ((ls lines) (y 16))
      (when (pair? ls)
        (gfx-draw-text! painter 10 y (car ls))
        (loop (cdr ls) (+ y 14))))))

;;; Stability indicator: pulsing border when d > 3

(define (draw-stability-indicator! painter w h d frame)
  (when (> (inexact d) 3.05)
    (let* ((pulse (abs (sin (* frame 0.04))))
           (alpha (* pulse (min 0.55 (* 0.15 (- (inexact d) 3.0))))))
      (gfx-set-pen-color! painter 1.0 0.3 0.12 alpha)
      (gfx-set-pen-width! painter 8.0)
      (gfx-draw-rect! painter 4.0 4.0 (- w 8.0) (- h 8.0)))))

;;; Master draw callback (called from timer via canvas-redraw!)
;;; Uses the stereo-renderer when *stereo* is set.

(define (make-draw-scene cx cy scale)
  ;; Returns a (lambda (painter eye-offset-wu color-fn) ...) for stereo-render!
  (lambda (painter eye-offset-wu color-fn)
    (draw-trails! painter)
    (when *show-field*
      (draw-field-lines! painter cx cy scale *d* *G* *bodies*))
    (draw-bodies! painter cx cy scale (* eye-offset-wu scale))))

(define (draw-frame painter w h)
  (let* ((d     *d*)
         (cx    (/ w 2.0))
         (cy    (/ h 2.0))
         (scale *view-scale*))
    ;; Background
    (call-with-values (lambda () (dim-bg-color d))
      (lambda (r g b) (gfx-clear! painter r g b)))
    ;; Grid
    (draw-grid! painter w h cx cy scale d)
    ;; Scene (stereo or mono)
    (if *stereo*
        (stereo-render! painter *stereo* w h (make-draw-scene cx cy scale))
        ((make-draw-scene cx cy scale) painter 0.0 (lambda (r g b) (values r g b))))
    ;; Stability pulsing border
    (draw-stability-indicator! painter w h d *frame-count*)
    ;; HUD
    (let ((energy-err (if (> (abs *energy-t0*) 1e-10)
                          (/ (abs (- *energy-now* *energy-t0*)) (abs *energy-t0*))
                          0.0)))
      (draw-hud! painter w d *G* energy-err))
    (set! *frame-count* (+ *frame-count* 1))))

;;; ════════════════════════════════════════════════════════════
;;; § 7  ACTOR SYSTEM: SIM ACTOR + WORKER POOL
;;;      (parallel force computation, §3.2 spirit)
;;; ════════════════════════════════════════════════════════════

;;; Worker: compute acceleration for one body
;;; Message in:  #(reply-to body-idx bodies-vec d G eps)
;;; Message out: (list idx accel-vec)  → to reply-to (gatherer)

(define (worker-loop)
  (let loop ()
    (let* ((msg      (receive))
           (reply-to (vector-ref msg 0))
           (idx      (vector-ref msg 1))
           (bvec     (vector-ref msg 2))
           (d        (vector-ref msg 3))
           (G        (vector-ref msg 4))
           (eps      (vector-ref msg 5))
           (bi       (vector-ref bvec idx))
           (n        (vector-length (body-pos bi)))
           (ai       (vzero n)))
      (do ((j 0 (+ j 1)))
          ((= j (vector-length bvec)))
        (when (not (= j idx))
          (let ((bj (vector-ref bvec j)))
            (vec-add! ai (pairwise-accel (body-pos bi) (body-pos bj)
                                         (body-mass bj) G d eps) 1.0))))
      (send! reply-to (list idx ai)))
    (loop)))

(define *max-workers* 8)
(define *worker-pool* (make-vector *max-workers* #f))

(define (init-workers!)
  (do ((i 0 (+ i 1)))
      ((= i *max-workers*))
    (vector-set! *worker-pool* i (spawn worker-loop))))

;;; Gatherer: collects N worker replies, sends sorted list to sim-actor

(define *gatherer* #f)

(define (init-gatherer!)
  (set! *gatherer*
    (spawn
      (lambda ()
        (let loop ()
          (let* ((ctrl     (receive))         ; (list n reply-to)
                 (n        (car ctrl))
                 (reply-to (cadr ctrl))
                 (accels   (make-vector n #f)))
            (do ((i 0 (+ i 1)))
                ((= i n))
              (let ((msg (receive)))           ; (list idx accel-vec)
                (vector-set! accels (car msg) (cadr msg))))
            (send! reply-to (cons 'accels (vector->list accels))))
          (loop))))))

;;; Parallel acceleration dispatch (used from sim-actor when N ≥ 4)

(define (parallel-accels bodies d G eps)
  (let* ((n    (length bodies))
         (bvec (list->vector bodies))
         (me   (self)))
    (send! *gatherer* (list n me))
    (do ((i 0 (+ i 1)))
        ((= i n))
      (send! (vector-ref *worker-pool* (modulo i *max-workers*))
             (vector *gatherer* i bvec d G eps)))
    (let wait ()
      (let ((msg (receive)))
        (if (and (pair? msg) (eq? (car msg) 'accels))
            (cdr msg)
            (wait))))))

;;; Sim actor: receives 'step, runs one physics tick, updates *bodies*

(define *sim-actor* #f)
(define *canvas*    #f)   ; set during window-on-realize!

(define (init-sim-actor!)
  (set! *sim-actor*
    (spawn
      (lambda ()
        (let loop ()
          (let ((msg (receive)))
            (when (eq? msg 'step)
              (when (not *paused*)
                (let* ((bodies *bodies*)
                       (n      (length bodies))
                       (d      *d*)
                       (G      *G*)
                       (eps    *eps*)
                       (dt     (if *adaptive-dt*
                                   (adaptive-dt bodies *dt-safety*)
                                   *dt*)))
                  (do ((s 0 (+ s 1)))
                      ((= s *speed*))
                    (leapfrog-step! bodies d G eps dt))
                  (when *canvas*
                    (let* ((w (qt-widget-width  *canvas*))
                           (h (qt-widget-height *canvas*))
                           (cx (/ (exact->inexact w) 2.0))
                           (cy (/ (exact->inexact h) 2.0)))
                      (update-trails! bodies cx cy *view-scale*)))
                  (set! *energy-now* (total-energy-compute d G eps))
                  ;; Record if active
                  (when *recording-port*
                    (write-recording-frame! *recording-port* d bodies))))))
          (loop))))))

;;; ════════════════════════════════════════════════════════════
;;; § 8  ENERGY & ANGULAR MOMENTUM (REPL Analysis API)
;;; ════════════════════════════════════════════════════════════

(define (kinetic-energy-compute bodies)
  (apply + (map (lambda (b)
                  (* 0.5 (body-mass b) (vec-norm2 (body-vel b))))
                bodies)))

(define (potential-energy-compute bodies d G eps)
  ;; U = sum_{i<j} Φ_ij
  ;; Φ_ij = -G*mi*mj / ((d-2) * r^(d-2))   for d≠2
  ;; Φ_ij =  G*mi*mj * ln(r)                for d≈2
  (let ((U 0.0))
    (let loop-i ((bs bodies))
      (when (pair? bs)
        (let loop-j ((bss (cdr bs)))
          (when (pair? bss)
            (let* ((bi (car bs))
                   (bj (car bss))
                   (r  (max eps (sqrt (vec-dist2 (body-pos bi) (body-pos bj)))))
                   (mi (body-mass bi))
                   (mj (body-mass bj))
                   (dv (inexact d)))
              (set! U
                (+ U (if (< (abs (- dv 2.0)) 1e-4)
                         (* G mi mj (log r))
                         (/ (* (- G) mi mj)
                            (* (- dv 2.0) (expt r (- dv 2.0))))))))
            (loop-j (cdr bss))))
        (loop-i (cdr bs))))
    U))

(define (total-energy-compute d G eps)
  (+ (kinetic-energy-compute *bodies*)
     (potential-energy-compute *bodies* d G eps)))

;;; Angular momentum as list of antisymmetric L_ij components:
;;;   L_ij = Σ_k m_k (r_ki v_kj - r_kj v_ki)   for all i < j

(define (angular-momentum-compute bodies)
  (when (null? bodies) (error "ul ibašši kippatum ina šaplānum"))  ; no rotation if no bodies
  (let* ((D (vector-length (body-pos (car bodies))))
         (comps '()))
    (do ((i 0 (+ i 1)))
        ((= i D))
      (do ((j (+ i 1) (+ j 1)))
          ((= j D))
        (let ((Lij 0.0))
          (for-each
            (lambda (b)
              (let ((r (body-pos b)) (v (body-vel b)) (m (body-mass b)))
                (set! Lij (+ Lij
                             (* m (- (* (vector-ref r i) (vector-ref v j))
                                     (* (vector-ref r j) (vector-ref v i))))))))
            bodies)
          (set! comps (append comps (list Lij))))))
    comps))

;;; Orbit period estimate for body id (§6.5)
;;;   T ≈ 2π r / v  (approximate for nearly-circular orbits)

(define (orbit-period-estimate body-id)
  (let* ((bs *bodies*)
         (n  (length bs)))
    (if (>= body-id n)
        (error "remove-body!: body-id out of range" body-id)
        (let* ((b  (list-ref bs body-id))
               (r  (sqrt (vec-norm2 (body-pos b))))
               (v  (sqrt (vec-norm2 (body-vel b)))))
          (if (< v 1e-12) +inf.0
              (* 2.0 3.14159265358979 (/ r v)))))))

;;; Finite-time Lyapunov exponent (chaos indicator):
;;; Shadow trajectory with tiny perturbation; track divergence rate.
;;; Returns approximate λ (positive = chaotic, ≈0 = regular).
;;; Note: this is expensive; call at low frequency from REPL only.

(define (lyapunov-exponent-estimate body-id steps)
  (let* ((bodies *bodies*)
         (n (length bodies)))
    (if (>= body-id n)
        (error "lyapunov-exponent: body-id out of range" body-id)
        (let* ((delta0   1e-5)
               (ref-b    (list-ref bodies body-id))
               ;; Shadow copy of just this body with perturbed position
               (shadow-pos (vec-copy (body-pos ref-b)))
               (shadow-vel (vec-copy (body-vel ref-b))))
          ;; Perturb shadow position slightly in first dimension
          (vector-set! shadow-pos 0 (+ (vector-ref shadow-pos 0) delta0))
          (let ((sum-log 0.0))
            (do ((s 0 (+ s 1)))
                ((= s steps))
              ;; Advance both reference and shadow bodies
              (leapfrog-step! bodies *d* *G* *eps* *dt*)
              (let* ((dx (- (vector-ref (body-pos ref-b) 0) (vector-ref shadow-pos 0)))
                     (dy (if (>= (vector-length shadow-pos) 2)
                             (- (vector-ref (body-pos ref-b) 1)
                                (vector-ref shadow-pos 1))
                             0.0))
                     (dist (sqrt (+ (* dx dx) (* dy dy)))))
                (when (> dist 1e-20)
                  (set! sum-log (+ sum-log (log (/ dist delta0))))
                  ;; Re-normalise shadow
                  (let ((scale (/ delta0 dist)))
                    (vector-set! shadow-pos 0
                      (+ (vector-ref (body-pos ref-b) 0) (* dx scale)))
                    (when (>= (vector-length shadow-pos) 2)
                      (vector-set! shadow-pos 1
                        (+ (vector-ref (body-pos ref-b) 1) (* dy scale))))))))
            (/ sum-log (* steps *dt*)))))))

;;; ════════════════════════════════════════════════════════════
;;; § 9  RECORDING
;;; ════════════════════════════════════════════════════════════

(define (write-recording-frame! port d bodies)
  (write (list 'd (inexact d)
               'bodies (map (lambda (b)
                              (list 'm (body-mass b)
                                    'p (vector->list (body-pos b))
                                    'v (vector->list (body-vel b))))
                            bodies))
         port)
  (newline port))

;;; ════════════════════════════════════════════════════════════
;;; § 10  LOAD SCENARIO
;;; ════════════════════════════════════════════════════════════

(define (load-scenario! name-or-sym)
  (let-values (((d G bodies) (scenario-run name-or-sym)))
    (set! *d*       d)
    (set! *G*       G)
    ;; Re-dimension bodies to match ceil(d)
    (let ((Dint (dim-for-d d)))
      (define (pad-vec v n)
        (let ((out (make-vector n 0.0)))
          (do ((k 0 (+ k 1))) ((= k (min n (vector-length v))) out)
            (vector-set! out k (vector-ref v k)))))
      (set! *bodies*
        (map (lambda (b)
               (vector (body-mass b)
                       (pad-vec (body-pos b) Dint)
                       (pad-vec (body-vel b) Dint)
                       '()
                       (body-color b)))
             bodies)))
    (set! *frame-count* 0)
    (set! *energy-t0*   (total-energy-compute d G *eps*))
    (set! *energy-now*  *energy-t0*)))

;;; ════════════════════════════════════════════════════════════
;;; § 11  UI CONSTRUCTION
;;; ════════════════════════════════════════════════════════════

(define *window*    #f)
(define *d-label*   #f)
(define *d-slider*  #f)
(define *g-slider*  #f)
(define *anim-timer* #f)

(define (build-window!)
  (set! *window* (make-window "kabtum ina šamê — Gravity in D Dimensions" 980 720))
  (window-on-close! *window* quit-event-loop)
  (window-on-key! *window*
    (lambda (key mods)
      (cond
        ((equal? key "space")    (set! *paused* (not *paused*)))
        ((equal? key "[")        (set! *d* (max 2.0 (- *d* 0.1))))
        ((equal? key "]")        (set! *d* (min 6.0 (+ *d* 0.1))))
        ((equal? key "{")        (set! *d* (max 2.0 (- *d* 1.0))))
        ((equal? key "}")        (set! *d* (min 6.0 (+ *d* 1.0))))
        ((equal? key "t")        (set! *trails* (not *trails*)))
        ((equal? key "f")        (set! *show-field* (not *show-field*)))
        ((or (equal? key "q")
             (equal? key "Escape")) (quit-event-loop)))))

  (window-on-realize! *window*
    (lambda ()
      (set! *canvas* (window-canvas *window*))
      (let ((sb (window-sidebar *window*)))

        ;; ---- Dimension slider ----------------------------------------
        (box-add! sb (make-label "Dimension  [ ] ← d → { }"))
        (set! *d-label*  (make-label (string-append "d = " (number->string *d*))))
        (set! *d-slider*
          (make-slider "d" 2.0 6.0 0.001 *d*
            (lambda (v)
              (set! *d* v)
              (let* ((Dint    (dim-for-d v))
                     (cur-dim (if (pair? *bodies*)
                                  (vector-length (body-pos (car *bodies*)))
                                  Dint)))
                ;; Resize body vectors if dimensionality crosses an integer
                (when (not (= Dint cur-dim))
                  (for-each
                    (lambda (b)
                      (let ((new-p (make-vector Dint 0.0))
                            (new-v (make-vector Dint 0.0)))
                        (do ((k 0 (+ k 1))) ((= k (min Dint cur-dim)))
                          (vector-set! new-p k (vector-ref (body-pos b) k))
                          (vector-set! new-v k (vector-ref (body-vel b) k)))
                        (body-set-pos! b new-p)
                        (body-set-vel! b new-v)))
                    *bodies*))))))
        (box-add! sb *d-slider*)
        (box-add! sb *d-label*)

        (box-add! sb (make-separator))

        ;; ---- Surreal mode toggle ------------------------------------
        (box-add! sb
          (make-toggle "Surreal ε mode" #f
            (lambda (on)
              (set! *surreal-mode* on))))
        (box-add! sb
          (make-slider "ε multiplier n" -10.0 10.0 1.0 0.0
            (lambda (v)
              (set! *surreal-eps-n* (inexact->exact (round v))))))

        (box-add! sb (make-separator))

        ;; ---- G slider -----------------------------------------------
        (box-add! sb (make-label "Gravity constant G"))
        (set! *g-slider*
          (make-slider "G" 0.1 5.0 0.01 *G*
            (lambda (v) (set! *G* v))))
        (box-add! sb *g-slider*)

        (box-add! sb (make-separator))

        ;; ---- Softening ----------------------------------------------
        (box-add! sb (make-label "Softening ε"))
        (box-add! sb
          (make-slider "ε" 0.01 1.0 0.01 *eps*
            (lambda (v) (set! *eps* v))))

        (box-add! sb (make-separator))

        ;; ---- Speed --------------------------------------------------
        (box-add! sb (make-label "Steps / frame"))
        (box-add! sb
          (make-slider "speed" 1.0 12.0 1.0 (exact->inexact *speed*)
            (lambda (v) (set! *speed* (inexact->exact (round v))))))

        (box-add! sb (make-separator))

        ;; ---- View scale ---------------------------------------------
        (box-add! sb (make-label "View scale"))
        (box-add! sb
          (make-slider "scale" 20.0 300.0 5.0 *view-scale*
            (lambda (v) (set! *view-scale* v))))

        (box-add! sb (make-separator))

        ;; ---- Projection focal lengths (higher dims) -----------------
        (box-add! sb (make-label "d→3 focal (d≥4)"))
        (box-add! sb
          (make-slider "f₁" 0.5 12.0 0.1 (vector-ref *focal-lengths* 0)
            (lambda (v) (vector-set! *focal-lengths* 0 v))))
        (box-add! sb
          (make-slider "f₂" 0.5 12.0 0.1 (vector-ref *focal-lengths* 1)
            (lambda (v) (vector-set! *focal-lengths* 1 v))))
        (box-add! sb
          (make-slider "f₃" 0.5 12.0 0.1 (vector-ref *focal-lengths* 2)
            (lambda (v) (vector-set! *focal-lengths* 2 v))))

        (box-add! sb (make-separator))

        ;; ---- Stereo -------------------------------------------------
        (box-add! sb (make-label "Stereo mode"))
        (box-add! sb
          (make-radio-group '("Off" "Anaglyph" "Side-by-side") 0
            (lambda (i)
              (case i
                ((0) (set! *stereo* #f))
                ((1) (set! *stereo* (make-stereo-renderer 'anaglyph 0.065)))
                ((2) (set! *stereo* (make-stereo-renderer 'sbs      0.065)))))))
        (box-add! sb
          (make-slider "Eye separation" 1.0 200.0 1.0 65.0
            (lambda (v)
              (when *stereo* (stereo-ipd-set! *stereo* (* v 0.001))))))

        (box-add! sb (make-separator))

        ;; ---- Toggles ------------------------------------------------
        (box-add! sb
          (make-toggle "Trails  [t]" #t
            (lambda (on)
              (set! *trails* on)
              (when (not on)
                (for-each (lambda (b) (body-set-trail! b '())) *bodies*)))))
        (box-add! sb
          (make-toggle "Field lines  [f]" #f
            (lambda (on) (set! *show-field* on))))
        (box-add! sb
          (make-toggle "Adaptive Δt" #t
            (lambda (on) (set! *adaptive-dt* on))))

        (box-add! sb (make-separator))

        ;; ---- Scenario presets ---------------------------------------
        (box-add! sb (make-label "Scenario"))
        (box-add! sb
          (make-dropdown
            '("two-body-circular" "three-body-figure-8" "binary-test-particle"
              "radial-infall" "random-cluster" "orbit-zoo" "dimension-sweep")
            0
            (lambda (i)
              (let ((names (scenario-names)))
                (when (< i (length names))
                  (load-scenario! (list-ref names i)))))))

        (box-add! sb (make-separator))

        ;; ---- Buttons ------------------------------------------------
        (box-add! sb
          (make-button "Pause / Resume [space]"
            (lambda () (set! *paused* (not *paused*)))))
        (box-add! sb
          (make-button "Clear trails"
            (lambda ()
              (for-each (lambda (b) (body-set-trail! b '())) *bodies*))))
        (box-add! sb
          (make-button "Quit  [q]" quit-event-loop))

        ;; ---- Canvas draw callback -----------------------------------
        (canvas-on-draw! *canvas* draw-frame)

        ;; ---- Animation timer (16ms ≈ 60fps) -------------------------
        (set! *anim-timer*
          (make-timer 16
            (lambda ()
              (when *d-slider*
                (set! *d* (slider-value *d-slider*)))
              (send! *sim-actor* 'step)
              (canvas-redraw! *canvas*))))
        (timer-start! *anim-timer*))))

  ;; Menu bar
  (let ((mb (window-menu-bar *window*))
        (names '("two-body-circular" "three-body-figure-8" "binary-test-particle"
                 "radial-infall" "random-cluster" "orbit-zoo" "dimension-sweep")))
    (let ((scen-menu (menubar-add-menu! mb "Scenarios")))
      (for-each
        (lambda (name)
          (menu-add-action! scen-menu name
            (lambda ()
              (load-scenario! (string->symbol name)))))
        names))
    (let ((sim-menu (menubar-add-menu! mb "Simulation")))
      (menu-add-action! sim-menu "Pause / Resume" (lambda () (set! *paused* (not *paused*))) "space")
      (menu-add-action! sim-menu "Clear Trails" (lambda () (for-each (lambda (b) (body-set-trail! b '())) *bodies*)))
      (menu-add-separator! sim-menu)
      (menu-add-action! sim-menu "Quit" quit-event-loop "Ctrl+Q"))))

;;; ════════════════════════════════════════════════════════════
;;; § 12  PUBLIC REPL API  (§6.5 of spec)
;;; ════════════════════════════════════════════════════════════

;; ---- Query ----

(define (gravity-dimension)
  ;; → current d (real or surreal notation)
  *d*)

(define (gravity-bodies)
  ;; → list of body vectors (mutable)
  *bodies*)

(define (body-position body-id)
  ;; → d-dimensional position vector
  (list-ref *bodies* body-id))

(define (body-energy body-id)
  ;; → kinetic + potential energy of this body
  (let* ((b  (list-ref *bodies* body-id))
         (KE (* 0.5 (body-mass b) (vec-norm2 (body-vel b))))
         (PE (apply +
               (map (lambda (bj)
                      (if (eq? b bj) 0.0
                          (let* ((r   (max *eps* (sqrt (vec-dist2 (body-pos b) (body-pos bj)))))
                                 (dv  (inexact *d*))
                                 (mi  (body-mass b))
                                 (mj  (body-mass bj)))
                            (if (< (abs (- dv 2.0)) 1e-4)
                                (* *G* mi mj (log r))
                                (/ (* (- *G*) mi mj)
                                   (* (- dv 2.0) (expt r (- dv 2.0))))))))
                    *bodies*))))
    (+ KE PE)))

;; ---- Modify ----

(define (set-dimension! d)
  ;; Change dimension live; resizes body position vectors
  (let* ((new-Dint (dim-for-d d))
         (cur-Dint (if (pair? *bodies*)
                       (vector-length (body-pos (car *bodies*)))
                       new-Dint)))
    (set! *d* (inexact d))
    (when (not (= new-Dint cur-Dint))
      (for-each
        (lambda (b)
          (let ((np (make-vector new-Dint 0.0))
                (nv (make-vector new-Dint 0.0)))
            (do ((k 0 (+ k 1))) ((= k (min new-Dint cur-Dint)))
              (vector-set! np k (vector-ref (body-pos b) k))
              (vector-set! nv k (vector-ref (body-vel b) k)))
            (body-set-pos! b np)
            (body-set-vel! b nv)))
        *bodies*))))

(define (add-body! mass position velocity)
  ;; Add a body with given mass, position list/vector, velocity list/vector
  (let* ((Dint (dim-for-d *d*))
         (to-vec (lambda (v)
                   (if (vector? v) v
                       (list->vector v))))
         (pos  (let ((pv (to-vec position)))
                 (let ((out (make-vector Dint 0.0)))
                   (do ((k 0 (+ k 1))) ((= k (min Dint (vector-length pv))) out)
                     (vector-set! out k (vector-ref pv k))))))
         (vel  (let ((vv (to-vec velocity)))
                 (let ((out (make-vector Dint 0.0)))
                   (do ((k 0 (+ k 1))) ((= k (min Dint (vector-length vv))) out)
                     (vector-set! out k (vector-ref vv k))))))
         (col  (list 0.8 0.8 0.8))
         (b    (vector mass pos vel '() col)))
    (set! *bodies* (append *bodies* (list b)))
    (- (length *bodies*) 1)))   ; return new body id

(define (remove-body! body-id)
  (set! *bodies*
    (let loop ((bs *bodies*) (i 0))
      (cond
        ((null? bs) '())
        ((= i body-id) (cdr bs))
        (else (cons (car bs) (loop (cdr bs) (+ i 1))))))))

(define (set-g! G)
  (set! *G* (inexact G)))

;; ---- Analysis ----

(define (total-energy)
  ;; → total mechanical energy (should be conserved)
  (total-energy-compute *d* *G* *eps*))

(define (angular-momentum)
  ;; → list of L_ij components (antisymmetric bivector in d dimensions)
  (if (< (inexact *d*) 2.0)
      (error "ul ibašši kippatum ina šaplānum")   ; no rotation below d=2
      (angular-momentum-compute *bodies*)))

(define (orbit-period body-id)
  ;; → estimated orbital period (approximate for circular orbits)
  (orbit-period-estimate body-id))

(define (lyapunov-exponent body-id)
  ;; → finite-time Lyapunov exponent (chaos indicator); expensive
  (lyapunov-exponent-estimate body-id 200))

;; ---- Recording ----

(define (start-recording! filename)
  (when *recording-port* (close-output-port *recording-port*))
  (set! *recording-port* (open-output-file filename)))

(define (stop-recording!)
  (when *recording-port*
    (close-output-port *recording-port*)
    (set! *recording-port* #f)))

(define (screenshot! filename)
  ;; Placeholder: actual screenshot requires Qt QWidget::grab(), not yet in Scheme API
  (display "screenshot!: not yet implemented — use Qt screenshot tools\n"))

;;; ════════════════════════════════════════════════════════════
;;; § 13  MAIN ENTRY POINT
;;; ════════════════════════════════════════════════════════════

(define (gravity-run! . args)
  ;; (gravity-run!)                     → launch with default scenario
  ;; (gravity-run! 'three-body-figure-8) → launch with named scenario
  (let ((scenario-name
          (if (pair? args) (car args) 'two-body-circular)))

    ;; Load scenario first (sets *bodies*, *d*, *G*)
    (load-scenario! scenario-name)

    ;; Spin up actor system
    (init-workers!)
    (init-gatherer!)
    (init-sim-actor!)

    ;; Build and show Qt6 window
    (build-window!)
    (window-show! *window*)
    (run-event-loop)))
