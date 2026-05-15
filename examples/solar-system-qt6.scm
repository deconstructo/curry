;;; solar-system-qt6.scm
;;; Version: 1.1
;;;
;;; N-body gravitational simulation in D spatial dimensions — Qt6 + actor physics.
;;;
;;; Thread layout:
;;;   main thread  — Qt6 event loop (timer, draw, UI)
;;;   sim-actor    — physics integration loop; receives 'step from the timer
;;;   gatherer     — collects N worker replies per step; isolates them from
;;;                  the sim-actor's control mailbox
;;;   worker × 12 — each computes one body's acceleration vector
;;;
;;; Because workers reply to the gatherer (not the sim-actor), the sim-actor's
;;; mailbox only ever holds 'step control messages and tagged '(accels . list)
;;; summary messages from the gatherer.  Any 'step messages that arrive while
;;; the sim-actor is waiting for an accels summary are silently dropped (the
;;; physics step already in flight covers that frame).
;;;
;;; In D dimensions the force law changes: F = G*m1*m2 / r^(D-1)
;;; At D=3 this is the familiar inverse-square law.
;;;
;;; Controls:
;;;   D slider       — spatial dimension (2.0–6.0, takes effect at Reset)
;;;   G slider       — gravitational constant
;;;   dt slider      — time step
;;;   Bodies slider  — number of bodies (applied at Reset)
;;;   Speed slider   — simulation steps per frame
;;;   Trails toggle  — show orbital trails
;;;   Profile toggle — show live profiling HUD (level 2 timing)
;;;   Pause/Resume   — pause simulation
;;;   Reset          — restart with new random conditions
;;;   q / Escape     — quit
;;;
;;; Run: ./build/curry examples/solar-system-qt6.scm

(import (curry qt6))
(import (scheme base))
(import (scheme inexact))
(import (scheme write))

;;; ---- Parameters ----

(define *D*        3.0)
(define *G*        1.0)
(define *dt*       0.005)
(define *nbodies*  5)
(define *speed*    4)
(define *paused*   #f)
(define *trails*   #t)
(define *show-profile* #f)

;;; Enable wall-clock timing for named closures.
(set! **eval-profiler** 2)

;;; ---- Body representation ----
;;; #(mass pos-vec vel-vec trail-list colour)

(define (body-mass b)  (vector-ref b 0))
(define (body-pos  b)  (vector-ref b 1))
(define (body-vel  b)  (vector-ref b 2))
(define (body-trail b) (vector-ref b 3))
(define (body-color b) (vector-ref b 4))

(define (body-set-pos!   b v) (vector-set! b 1 v))
(define (body-set-vel!   b v) (vector-set! b 2 v))
(define (body-set-trail! b t) (vector-set! b 3 t))

(define *bodies* '())

;;; ---- Math helpers ----

(define (vec-zero n)  (make-vector n 0.0))
(define (vec-len v)   (vector-length v))

(define (vec-dist2 a b)
  (let loop ((i 0) (s 0.0))
    (if (= i (vec-len a)) s
        (let ((d (- (vector-ref a i) (vector-ref b i))))
          (loop (+ i 1) (+ s (* d d)))))))

(define (vec-dist a b) (sqrt (vec-dist2 a b)))

(define (vec-add! dst src scale)
  (let loop ((i 0))
    (when (< i (vec-len dst))
      (vector-set! dst i (+ (vector-ref dst i)
                             (* (vector-ref src i) scale)))
      (loop (+ i 1)))))

;;; ---- Gravity in D dimensions ----
;;; Acceleration on body i due to j, component k = G*mj*Δk / r^D

(define (gravity-accel-k D G mj ri rj k)
  (let* ((r      (vec-dist ri rj))
         (r-soft (max r 0.1))
         (delta  (- (vector-ref rj k) (vector-ref ri k))))
    (/ (* G mj delta) (expt r-soft D))))

;;; ---- Worker actor ----
;;;
;;; Message in:  #(reply-to body-idx bodies-vector D G)
;;; Message out: (list body-idx accel-vector)  →  sent to reply-to (the gatherer)

(define (worker-loop)
  (let loop ()
    (let* ((msg      (receive))
           (reply-to (vector-ref msg 0))
           (idx      (vector-ref msg 1))
           (bvec     (vector-ref msg 2))
           (D        (vector-ref msg 3))
           (G        (vector-ref msg 4))
           (bi       (vector-ref bvec idx))
           (Dint     (inexact->exact (round D)))
           (a        (vec-zero Dint)))
      (let jloop ((j 0))
        (when (< j (vector-length bvec))
          (when (not (= j idx))
            (let ((bj (vector-ref bvec j)))
              (let kloop ((k 0))
                (when (< k Dint)
                  (vector-set! a k
                    (+ (vector-ref a k)
                       (gravity-accel-k D G
                         (body-mass bj)
                         (body-pos bi) (body-pos bj) k)))
                  (kloop (+ k 1))))))
          (jloop (+ j 1))))
      (send! reply-to (list idx a)))
    (loop)))

;;; ---- Worker pool ----
;;; Fixed pool of *max-workers* actors, created once at startup.
;;; For N bodies we use workers 0..N-1.

(define *max-workers* 12)
(define *workers* (make-vector *max-workers* #f))

(define (init-workers!)
  (let loop ((i 0))
    (when (< i *max-workers*)
      (vector-set! *workers* i (spawn worker-loop))
      (loop (+ i 1)))))

;;; ---- Gatherer actor ----
;;;
;;; Separates worker replies from the sim-actor's control mailbox.
;;; Protocol per step:
;;;   1. Sim-actor sends (list n sim-actor) to gatherer
;;;   2. Gatherer collects N worker replies in any order
;;;   3. Gatherer sends (cons 'accels sorted-accel-list) to sim-actor

(define gatherer-actor #f)

(define (start-gatherer!)
  (set! gatherer-actor
    (spawn
      (lambda ()
        (let loop ()
          (let* ((ctrl     (receive))        ; (list n reply-to)
                 (n        (car ctrl))
                 (reply-to (cadr ctrl))
                 (accels   (make-vector n #f)))
            (let inner ((i 0))
              (when (< i n)
                (let ((msg (receive)))       ; worker reply: (list idx accel-vec)
                  (vector-set! accels (car msg) (cadr msg)))
                (inner (+ i 1))))
            (send! reply-to (cons 'accels (vector->list accels))))
          (loop))))))

;;; ---- Parallel acceleration ----
;;;
;;; Called from within the sim-actor.  Dispatches N work items to the worker
;;; pool, then waits for the gatherer's (accels . list) reply.  Any 'step
;;; messages that sneak in while we wait are dropped — that frame's physics is
;;; already running.

(define (parallel-compute-accel bodies D G)
  (let* ((n    (length bodies))
         (bvec (list->vector bodies))
         (me   (self)))
    ; Tell gatherer: expect n replies, send result back to me
    (send! gatherer-actor (list n me))
    ; Dispatch one work item per body (reply-to = gatherer, not me)
    (let loop ((i 0))
      (when (< i n)
        (send! (vector-ref *workers* i)
               (vector gatherer-actor i bvec D G))
        (loop (+ i 1))))
    ; Wait for the tagged summary; discard any 'step messages that race in
    (let wait ()
      (let ((msg (receive)))
        (if (and (pair? msg) (eq? (car msg) 'accels))
            (cdr msg)
            (wait))))))

;;; ---- One physics step (runs inside sim-actor) ----
;;;
;;; Velocity-Verlet (2nd-order symplectic):
;;;   x_{n+1} = x_n + h·v_n + h²/2·a_n        (position half-step)
;;;   a_{n+1} = f(x_{n+1})                      (forces at new positions)
;;;   v_{n+1} = v_n + h/2·(a_n + a_{n+1})      (velocity full step)
;;;
;;; Two force evaluations per step vs. one for symplectic Euler, but
;;; energy error drops from O(h) to O(h²) over long integrations.

(define (run-one-step!)
  (let* ((D   (inexact *D*))
         (bs  *bodies*)
         (dt  *dt*)
         (dt2 (* 0.5 dt dt)))
    (let step-n ((n 0))
      (when (< n *speed*)
        (let ((a0 (parallel-compute-accel bs D *G*)))
          ;; x_{n+1} = x_n + h·v_n + h²/2·a_n
          (for-each (lambda (b a)
                      (vec-add! (body-pos b) (body-vel b) dt)
                      (vec-add! (body-pos b) a            dt2))
                    bs a0)
          ;; a_{n+1} at updated positions
          (let ((a1 (parallel-compute-accel bs D *G*)))
            ;; v_{n+1} = v_n + h/2·(a_n + a_{n+1})
            (for-each (lambda (b a-old a-new)
                        (vec-add! (body-vel b) a-old (* 0.5 dt))
                        (vec-add! (body-vel b) a-new (* 0.5 dt))
                        (when *trails*
                          (let* ((pos   (body-pos b))
                                 (px    (vector-ref pos 0))
                                 (py    (if (>= (vector-length pos) 2)
                                            (vector-ref pos 1) 0.0))
                                 (trail (body-trail b)))
                            (body-set-trail! b
                              (let ((t (cons (cons px py) trail)))
                                (if (> (length t) 120)
                                    (list-head t 120)
                                    t))))))
                      bs a0 a1)))
        (step-n (+ n 1))))))

;;; ---- Simulation actor ----
;;;
;;; Receives 'step from the Qt6 timer (sent by the animation timer, fire-and-
;;; forget).  Uses parallel-compute-accel for the O(n²) kernel.
;;;
;;; G, dt, speed are read from shared globals (scalar writes are atomic at the
;;; 64-bit word level, acceptable for a live-update slider).
;;; D takes effect only at Reset — changing it mid-step would mismatch the
;;; dimension of existing body position/velocity vectors.

(define sim-actor #f)

(define (start-sim-actor!)
  (set! sim-actor
    (spawn
      (lambda ()
        (let loop ()
          (let ((msg (receive)))
            (when (eq? msg 'step)
              (when (not *paused*)
                (run-one-step!))))
          (loop))))))

;;; ---- Initial conditions ----

(define rand-state 12345678)
(define (rand01)
  (set! rand-state (modulo (+ (* rand-state 1664525) 1013904223) (expt 2 32)))
  (/ (exact->inexact rand-state) (exact->inexact (expt 2 32))))

(define *body-colors*
  '((1.0 0.9 0.2)  (0.6 0.8 1.0)  (0.4 0.9 0.4)  (1.0 0.5 0.3)
    (0.8 0.4 0.9)  (1.0 0.7 0.7)  (0.4 1.0 0.9)  (0.9 0.9 0.4)
    (0.7 0.5 0.3)  (0.5 0.7 1.0)  (1.0 0.9 0.6)  (0.6 1.0 0.6)))

(define (init-bodies! n D)
  (let ((star-mass 100.0)
        (star-pos  (vec-zero D))
        (star-vel  (vec-zero D))
        (bodies    '()))
    (set! bodies
      (cons (vector star-mass star-pos star-vel '() (list-ref *body-colors* 0))
            bodies))
    (let loop ((i 1))
      (when (< i n)
        (let* ((angle  (* 2 3.14159265358979
                          (/ (exact->inexact i) (exact->inexact (- n 1)))))
               (radius (* 80.0 (+ 0.5 (rand01))))
               (r-used (max radius 10.0))
               (v-circ (sqrt (max 0.0 (/ (* *G* star-mass)
                                          (expt r-used (- D 2.0))))))
               (pos    (vec-zero D))
               (vel    (vec-zero D)))
          (vector-set! pos 0 (* radius (cos angle)))
          (when (>= D 2) (vector-set! pos 1 (* radius (sin angle))))
          (when (>= D 3)
            (let lp ((k 2)) (when (< k D) (vector-set! pos k (* 5.0 (- (rand01) 0.5))) (lp (+ k 1)))))
          (vector-set! vel 0 (* (- (sin angle)) v-circ))
          (when (>= D 2) (vector-set! vel 1 (* (cos angle) v-circ)))
          (when (>= D 3)
            (let lp ((k 2)) (when (< k D) (vector-set! vel k (* 0.1 (- (rand01) 0.5))) (lp (+ k 1)))))
          (set! bodies
            (cons (vector (* 1.0 (+ 0.5 (rand01))) pos vel '()
                          (list-ref *body-colors* (modulo i (length *body-colors*))))
                  bodies))
          (loop (+ i 1)))))
    (set! *bodies* (reverse bodies))))

;;; ---- Profiling HUD ----

;;; Snapshot updated each frame when profiling is on.
(define *prof-snapshot* '())

(define (refresh-profile-snapshot!)
  (set! *prof-snapshot* (profiling-report)))

;;; Format nanoseconds into a compact human-readable string.
(define (fmt-ns ns)
  (cond
    ((>= ns 1000000000) (string-append (number->string (round (/ ns 1000000))) "ms"))
    ((>= ns 1000000)    (string-append (number->string (round (/ ns 1000)))    "µs"))
    (else               (string-append (number->string ns)                     "ns"))))

;;; Draw the profiling overlay — semi-transparent panel, top-12 functions.
(define (draw-profile-hud painter w h)
  (let* ((rows    (min 12 (length *prof-snapshot*)))
         (col-w   340)
         (row-h   16)
         (pad     8)
         (panel-w (+ col-w (* 2 pad)))
         (panel-h (+ (* rows row-h) (* 2 pad) 20))
         (px      (- w panel-w 10))
         (py      10))

    ; Panel background
    (gfx-set-color! painter 0.05 0.05 0.12 0.82)
    (gfx-fill-rect! painter px py panel-w panel-h)
    (gfx-set-color! painter 0.4 0.5 0.7 0.6)
    (gfx-draw-rect! painter px py panel-w panel-h)

    ; Header
    (gfx-set-color! painter 0.7 0.8 1.0 1.0)
    (gfx-set-font! painter "Monospace" 11 #t)
    (gfx-draw-text! painter (+ px pad) (+ py pad 10) "PROFILER  (level 2 — wall clock)")

    ; Column rows
    (gfx-set-font! painter "Monospace" 11)
    (let loop ((entries *prof-snapshot*) (i 0))
      (when (and (pair? entries) (< i rows))
        (let* ((entry  (car entries))
               (name   (symbol->string (car entry)))
               (calls  (cadr entry))
               (ns     (cddr entry))
               (y      (+ py pad 20 (* i row-h)))
               ; Colour: yellow for hottest, fading to grey
               (heat   (- 1.0 (/ i (max 1 (- rows 1)))))
               (r      (+ 0.4 (* 0.6 heat)))
               (g      (+ 0.4 (* 0.5 (- 1.0 heat))))
               (b      0.4)
               ; Truncate long names
               (disp   (if (> (string-length name) 28)
                           (string-append (substring name 0 25) "...")
                           name)))
          (gfx-set-color! painter r g b 0.95)
          (gfx-draw-text! painter (+ px pad) (+ y 11)
            (string-append
              (let pad-r ((s disp) (n 28))
                (if (>= (string-length s) n) s
                    (pad-r (string-append s " ") n)))
              " "
              (let lpad ((s (number->string calls)) (n 7))
                (if (>= (string-length s) n) s
                    (lpad (string-append " " s) n)))
              "  "
              (fmt-ns ns))))
        (loop (cdr entries) (+ i 1))))))

;;; ---- Drawing ----

(define (w->s x cx scale) (+ cx (* x scale)))
(define (h->s y cy scale) (+ cy (* y scale)))

(define (draw painter w h)
  ; Dark space background
  (gfx-clear! painter 0.02 0.02 0.08)

  (let* ((cx    (/ w 2.0))
         (cy    (/ h 2.0))
         (scale 1.5))

    ; Orbital trails
    (when *trails*
      (for-each
        (lambda (body)
          (let ((trail (body-trail body))
                (col   (body-color body)))
            (when (pair? trail)
              (let loop ((pts trail) (alpha 0.6))
                (when (pair? pts)
                  (let* ((pt (car pts))
                         (sx (w->s (car pt) cx scale))
                         (sy (h->s (cdr pt) cy scale)))
                    (gfx-set-color! painter
                      (car col) (cadr col) (caddr col)
                      (* alpha 0.08))
                    (gfx-fill-circle! painter sx sy 1.5))
                  (loop (cdr pts) (* alpha 0.97)))))))
        *bodies*))

    ; Bodies — glow halo then solid disc
    (for-each
      (lambda (body)
        (let* ((pos  (body-pos body))
               (px   (vector-ref pos 0))
               (py   (if (>= (vector-length pos) 2) (vector-ref pos 1) 0.0))
               (mass (body-mass body))
               (col  (body-color body))
               (sx   (w->s px cx scale))
               (sy   (h->s py cy scale))
               (r    (max 2.0 (* 3.0 (expt (/ mass 5.0) 0.33)))))
          (gfx-set-color! painter (car col) (cadr col) (caddr col) 0.15)
          (gfx-fill-circle! painter sx sy (* r 2.5))
          (gfx-set-color! painter (car col) (cadr col) (caddr col) 1.0)
          (gfx-fill-circle! painter sx sy r)))
      *bodies*)

    ; HUD text
    (gfx-set-color! painter 0.6 0.7 0.8 0.9)
    (gfx-set-font! painter "Monospace" 12)
    (gfx-draw-text! painter 10 18
      (string-append "D = " (number->string *D*)
                     (if (= *D* 3.0) "  (classical)" "")))
    (gfx-draw-text! painter 10 34
      (string-append "G = " (number->string *G*)))
    (gfx-draw-text! painter 10 50
      (string-append "dt = " (number->string *dt*)
                     "   bodies = " (number->string (length *bodies*))))
    (gfx-draw-text! painter 10 66
      (string-append "velocity-Verlet   threads: 1+1+1+"
                     (number->string *max-workers*) " workers"))
    (when *paused*
      (gfx-set-color! painter 1.0 0.6 0.2 0.9)
      (gfx-set-font! painter "Monospace" 14 #t)
      (gfx-draw-text! painter 10 82 "PAUSED"))

    ; Profiling overlay
    (when *show-profile*
      (draw-profile-hud painter w h))))

;;; ---- Widget handles (set during realize) ----

(define canvas       #f)
(define d-slider     #f)
(define g-slider     #f)
(define dt-slider    #f)
(define n-slider     #f)
(define speed-slider #f)
(define anim-timer   #f)
(define sb           #f)

(define (do-reset!)
  (when d-slider
    (set! *D*       (slider-value d-slider))
    (set! *G*       (slider-value g-slider))
    (set! *dt*      (slider-value dt-slider))
    (set! *nbodies* (inexact->exact (round (slider-value n-slider)))))
  (init-bodies! *nbodies* (inexact->exact (round *D*))))

;;; ---- Window ----

(define win (make-window "Solar System in D Dimensions (parallel actors)" 920 700))

(window-on-close! win quit-event-loop)

(window-on-key! win
  (lambda (key mods)
    (cond
      ((equal? key "space")  (set! *paused* (not *paused*)))
      ((equal? key "r")      (do-reset!))
      ((equal? key "p")      (set! *show-profile* (not *show-profile*)))
      ((equal? key "q")      (quit-event-loop))
      ((equal? key "Escape") (quit-event-loop)))))

(window-on-realize! win
  (lambda ()
    (set! canvas (window-canvas win))
    (set! sb (window-sidebar win))

    ; ---- Dimension ----
    (box-add! sb (make-label "Dimension D"))
    (set! d-slider (make-slider "D" 2.0 6.0 0.01 3.0))
    (box-add! sb d-slider)

    (box-add! sb (make-separator))

    ; ---- Gravity ----
    (box-add! sb (make-label "Gravity G"))
    (set! g-slider (make-slider "G" 0.1 5.0 0.01 1.0))
    (box-add! sb g-slider)

    (box-add! sb (make-separator))

    ; ---- Time step ----
    (box-add! sb (make-label "Time step dt"))
    (set! dt-slider (make-slider "dt" 0.001 0.05 0.001 0.005))
    (box-add! sb dt-slider)

    (box-add! sb (make-separator))

    ; ---- Bodies ----
    (box-add! sb (make-label "Bodies (apply at Reset)"))
    (set! n-slider (make-slider "N" 2.0 12.0 1.0 5.0))
    (box-add! sb n-slider)

    (box-add! sb (make-separator))

    ; ---- Speed ----
    (box-add! sb (make-label "Steps / frame"))
    (set! speed-slider (make-slider "speed" 1.0 20.0 1.0 4.0))
    (box-add! sb speed-slider)

    (box-add! sb (make-separator))

    ; ---- Toggles ----
    (box-add! sb
      (make-toggle "Trails" #t
        (lambda (on)
          (set! *trails* on)
          (when (not on)
            (for-each (lambda (b) (body-set-trail! b '())) *bodies*)))))

    (box-add! sb
      (make-toggle "Profile HUD  [p]" #f
        (lambda (on)
          (set! *show-profile* on)
          (when (not on)
            (set! *prof-snapshot* '())))))

    (box-add! sb (make-separator))

    ; ---- Buttons ----
    (box-add! sb
      (make-button "Pause / Resume"
        (lambda () (set! *paused* (not *paused*)))))

    (box-add! sb
      (make-button "Reset"
        (lambda ()
          (profiling-reset)
          (do-reset!))))

    (box-add! sb
      (make-button "Reset Profiler"
        (lambda ()
          (profiling-reset)
          (set! *prof-snapshot* '()))))

    (box-add! sb
      (make-button "Quit" quit-event-loop))

    ; ---- Canvas callback ----
    (canvas-on-draw! canvas draw)

    ; ---- Animation timer ----
    ; Sends 'step to sim-actor (non-blocking fire-and-forget) then repaints.
    ; G, dt, speed update live — scalar writes are effectively atomic.
    ; D is NOT updated here: changing mid-sim mismatches body vector dimensions;
    ; it takes effect only when Reset is pressed.
    (set! anim-timer
      (make-timer 16
        (lambda ()
          (when speed-slider
            (set! *G*     (slider-value g-slider))
            (set! *dt*    (slider-value dt-slider))
            (set! *speed* (inexact->exact (round (slider-value speed-slider)))))
          (when *show-profile*
            (refresh-profile-snapshot!))
          (send! sim-actor 'step)
          (canvas-redraw! canvas))))
    (timer-start! anim-timer)))

;;; ---- Launch ----
;;;
;;; Startup order matters: workers → gatherer → bodies → sim-actor → window.
;;; The sim-actor must not start before bodies exist and the gatherer is ready.

(init-workers!)
(start-gatherer!)
(init-bodies! *nbodies* (inexact->exact (round *D*)))
(start-sim-actor!)
(window-show! win)
(run-event-loop)
