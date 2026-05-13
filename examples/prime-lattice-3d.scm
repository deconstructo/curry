;;; prime-lattice-3d.scm — 3D cubic-spiral prime lattice
;;; Version: 1.0
;;;
;;; Integers 0..N-1 are mapped to a 3D integer lattice shell by shell
;;; (Chebyshev norm: shell k = {(x,y,z) : max(|x|,|y|,|z|) = k}).
;;; Primes: bright gold.  Composites: dim blue fog.
;;; Prime-rich diagonal planes emerge like Bragg reflection planes
;;; in a crystal — the 3D analogue of the Ulam spiral.
;;;
;;; Controls:
;;;   Left-drag   — rotate (azimuth + elevation)
;;;   Sliders     — zoom, point size, spin speed, shell depth k
;;;   Rebuild     — regenerate lattice at current k
;;;   r           — reset view
;;;   q / Escape  — quit
;;;
;;; Run: ./build/curry examples/prime-lattice-3d.scm

(import (curry qt6))
(import (scheme base))
(import (scheme inexact))

;;; ─── Primality (trial division) ──────────────────────────────────────────────

(define (prime? n)
  (and (>= n 2)
       (or (= n 2)
           (and (odd? n)
                (let loop ((d 3))
                  (cond ((> (* d d) n)       #t)
                        ((zero? (modulo n d)) #f)
                        (else (loop (+ d 2)))))))))

;;; ─── Shell enumeration ───────────────────────────────────────────────────────
;;;
;;; Enumerate Chebyshev shells 0, 1, 2, ... outward.  Within each shell k,
;;; iterate z-major, y-minor, x-innermost, keeping only surface points
;;; (those where max(|x|,|y|,|z|) = k exactly).
;;;
;;; Integer n → the n-th point in this sequence, stored in lx/ly/lz/lp.
;;; lp[n] = #t iff n is prime.  Returns prime count.

(define (build-lattice! N lx ly lz lp)
  (let ((n 0) (np 0))
    (let k-loop ((k 0))
      (when (< n N)
        (do ((z (- k) (+ z 1))) ((> z k))
          (do ((y (- k) (+ y 1))) ((> y k))
            (do ((x (- k) (+ x 1))) ((> x k))
              (when (and (< n N)
                         (= k (max (abs x) (abs y) (abs z))))
                (vector-set! lx n (inexact x))
                (vector-set! ly n (inexact y))
                (vector-set! lz n (inexact z))
                (let ((p (prime? n)))
                  (vector-set! lp n p)
                  (when p (set! np (+ np 1))))
                (set! n (+ n 1))))))
        (k-loop (+ k 1))))
    np))

;;; ─── Lattice state ───────────────────────────────────────────────────────────
;;;
;;; Pre-separated prime/composite coordinate vectors so draw-frame can call
;;; the C gfx-project-batch function directly with no per-frame Scheme loop.

(define (shell-volume k) (let ((d (+ (* 2 k) 1))) (* d d d)))

;;; After filling lx/ly/lz/lp, partition into separate prime/composite arrays.
;;; Returns #(prime-count prime-lx prime-ly prime-lz comp-lx comp-ly comp-lz).
(define (partition-lattice N lx ly lz lp)
  (let* ((np (let loop ((i 0) (c 0))
               (if (= i N) c
                   (loop (+ i 1) (if (vector-ref lp i) (+ c 1) c)))))
         (nc  (- N np))
         (plx (make-vector np 0.0))  (ply (make-vector np 0.0))
         (plz (make-vector np 0.0))
         (clx (make-vector nc 0.0))  (cly (make-vector nc 0.0))
         (clz (make-vector nc 0.0))
         (pi  0)  (ci 0))
    (do ((i 0 (+ i 1))) ((= i N))
      (if (vector-ref lp i)
          (begin (vector-set! plx pi (vector-ref lx i))
                 (vector-set! ply pi (vector-ref ly i))
                 (vector-set! plz pi (vector-ref lz i))
                 (set! pi (+ pi 1)))
          (begin (vector-set! clx ci (vector-ref lx i))
                 (vector-set! cly ci (vector-ref ly i))
                 (vector-set! clz ci (vector-ref lz i))
                 (set! ci (+ ci 1)))))
    (vector np plx ply plz clx cly clz)))

(define *shell-k*     12)
(define *N*           (shell-volume 12))   ; 25^3 = 15625
(define *prime-count* 0)
(define *prime-lx* #f) (define *prime-ly* #f) (define *prime-lz* #f)
(define *comp-lx*  #f) (define *comp-ly*  #f) (define *comp-lz*  #f)

(define (rebuild! k)
  (let* ((n   (shell-volume k))
         (lx  (make-vector n 0.0))
         (ly  (make-vector n 0.0))
         (lz  (make-vector n 0.0))
         (lp  (make-vector n #f))
         (_   (build-lattice! n lx ly lz lp))
         (par (partition-lattice n lx ly lz lp)))
    (set! *shell-k*     k)
    (set! *N*           n)
    (set! *prime-count* (vector-ref par 0))
    (set! *prime-lx*    (vector-ref par 1))
    (set! *prime-ly*    (vector-ref par 2))
    (set! *prime-lz*    (vector-ref par 3))
    (set! *comp-lx*     (vector-ref par 4))
    (set! *comp-ly*     (vector-ref par 5))
    (set! *comp-lz*     (vector-ref par 6))))

(rebuild! 12)

;;; ─── View state ──────────────────────────────────────────────────────────────

(define *azimuth*   0.5)
(define *elevation* 0.28)
(define *zoom*      1.0)
(define *pt-size*   2.5)
(define *spin*      0.004)
(define *auto-spin* #t)

;;; Drag tracking
(define *dragging*  #f)
(define *drag-x0*   0)
(define *drag-y0*   0)
(define *az0*       0.0)
(define *el0*       0.0)

;;; ─── Axis overlay ────────────────────────────────────────────────────────────
;;;
;;; Three green lines through the origin, ±k units long, with tick marks
;;; every 10 integers and an X/Y/Z label at each positive end.
;;; Tick perpendiculars are chosen so they stay visible at oblique angles:
;;;   X-axis ticks → Y direction
;;;   Y-axis ticks → Z direction
;;;   Z-axis ticks → X direction

(define (draw-axes painter ca sa ce se cx cy scale dist k)
  (let* ((kf (inexact k))
         (tk 0.55))  ; tick half-length in lattice units

    ;; Project one 3D point → screen (sx . sy)
    (define (p3d x y z)
      (let* ((rx (+ (* ca x) (* sa z)))
             (ry y)
             (rz (- (* ca z) (* sa x)))
             (fx rx)
             (fy (- (* ce ry) (* se rz)))
             (fz (+ (* se ry) (* ce rz)))
             (ww (/ dist (+ dist fz))))
        (cons (+ cx (* scale fx ww))
              (- cy (* scale fy ww)))))

    ;; Convert a list of (x1 y1 x2 y2) screen-coord lists to a flat vector
    (define (segs->vec lst)
      (let* ((n (length lst))
             (v (make-vector (* n 4) 0.0))
             (i 0))
        (for-each (lambda (s)
                    (vector-set! v i       (car s))
                    (vector-set! v (+ i 1) (cadr s))
                    (vector-set! v (+ i 2) (caddr s))
                    (vector-set! v (+ i 3) (cadr (cddr s)))
                    (set! i (+ i 4)))
                  lst)
        v))

    ;; Project a 3D segment to a screen-coord list entry
    (define (seg x1 y1 z1 x2 y2 z2)
      (let ((a (p3d x1 y1 z1)) (b (p3d x2 y2 z2)))
        (list (car a)(cdr a)(car b)(cdr b))))

    ;; Build tick segments for one axis, given a tick-maker lambda
    (define (ticks make-pair)
      (let loop ((t 10) (acc '()))
        (if (> t k) acc
            (let ((tf (inexact t)))
              (loop (+ t 10)
                    (append (make-pair tf) acc))))))

    ;; Draw one coloured axis: thick line + thinner ticks + label
    (define (draw-axis! r g b ax-seg tk-segs label lx ly lz)
      (gfx-draw-lines! painter (segs->vec (list ax-seg)) r g b 0.88 2.5)
      (when (pair? tk-segs)
        (gfx-draw-lines! painter (segs->vec tk-segs) r g b 0.70 1.1))
      (let ((p (p3d lx ly lz)))
        (gfx-set-color! painter r g b 0.95)
        (gfx-set-font! painter "Monospace" 13)
        (gfx-draw-text! painter (car p) (cdr p) label)))

    ;; X axis — red, ticks in Y direction
    (draw-axis! 0.90 0.18 0.18
                (seg (- kf) 0.0 0.0  kf 0.0 0.0)
                (ticks (lambda (tf)
                         (list (seg tf     (- tk) 0.0  tf     tk  0.0)
                               (seg (- tf) (- tk) 0.0  (- tf) tk  0.0))))
                "X"  (+ kf 1.5) 0.0 0.0)

    ;; Y axis — green, ticks in Z direction
    (draw-axis! 0.18 0.88 0.22
                (seg 0.0 (- kf) 0.0  0.0 kf 0.0)
                (ticks (lambda (tf)
                         (list (seg 0.0 tf     (- tk)  0.0 tf     tk)
                               (seg 0.0 (- tf) (- tk)  0.0 (- tf) tk))))
                "Y"  0.0 (+ kf 1.5) 0.0)

    ;; Z axis — blue, ticks in X direction
    (draw-axis! 0.20 0.45 0.95
                (seg 0.0 0.0 (- kf)  0.0 0.0 kf)
                (ticks (lambda (tf)
                         (list (seg (- tk) 0.0 tf    tk  0.0 tf)
                               (seg (- tk) 0.0 (- tf) tk 0.0 (- tf)))))
                "Z"  0.0 0.0 (+ kf 1.5))))

;;; ─── Rotation matrix helper ──────────────────────────────────────────────────
;;;
;;; Build a row-major 3×3 matrix for Ry(az)·Rx(el) — azimuth around Y then
;;; elevation around X.  Pass to vec3-project-batch (a core builtin, no Qt).

(define (mat3-euler az el)
  (let ((ca (cos az)) (sa (sin az))
        (ce (cos el)) (se (sin el)))
    (vector ca          0.0    sa
            (* sa se)   ce     (- (* ca se))
            (- (* sa ce)) se   (* ca ce))))

;;; ─── Per-frame draw ──────────────────────────────────────────────────────────
;;;
;;; Hot loop runs entirely in C via vec3-project-batch (core builtin).
;;; Scheme only builds the rotation matrix and calls the draw functions.

(define (draw-frame painter w h)
  (gfx-clear! painter 0.03 0.03 0.08)

  (let* ((cx    (* 0.5 (inexact w)))
         (cy    (* 0.5 (inexact h)))
         (scale (* (min (inexact w) (inexact h)) 0.038 *zoom*))
         (dist  8.0)
         (az    *azimuth*)
         (el    *elevation*)
         (R     (mat3-euler az el))
         (ca    (vector-ref R 0))  (sa (vector-ref R 2))
         (ce    (vector-ref R 4))  (se (vector-ref R 7))
         ;; Project both groups entirely in C — no Scheme loop
         (proj-c (vec3-project-batch *comp-lx*  *comp-ly*  *comp-lz*
                                     R cx cy scale dist))
         (proj-p (vec3-project-batch *prime-lx* *prime-ly* *prime-lz*
                                     R cx cy scale dist)))

    ;; Axes (projected with same params)
    (draw-axes painter ca sa ce se cx cy scale dist *shell-k*)

    ;; Composites: dim fog
    (gfx-draw-points! painter
                      (vector-ref proj-c 0) (vector-ref proj-c 1)
                      0.18 0.22 0.50  0.10  *pt-size*)

    ;; Primes: bright gold planes
    (gfx-draw-points! painter
                      (vector-ref proj-p 0) (vector-ref proj-p 1)
                      1.0  0.85 0.10  0.88  (* *pt-size* 1.6)))

  ;; HUD
  (gfx-set-color! painter 0.55 0.65 0.78 0.85)
  (gfx-set-font! painter "Monospace" 11)
  (gfx-draw-text! painter 10 18
    (string-append "k=" (number->string *shell-k*)
                   "  N=" (number->string *N*)
                   "  primes=" (number->string *prime-count*)
                   "  drag=rotate  sliders=zoom/size/spin")))

;;; ─── Animation step ──────────────────────────────────────────────────────────

(define (step!)
  (when *auto-spin*
    (set! *azimuth* (+ *azimuth* *spin*))))

;;; ─── Window and sidebar ──────────────────────────────────────────────────────

(define win (make-window "Prime Lattice 3D — Cubic-Spiral Ulam" 1040 740))
(define sb  (window-sidebar win))

(box-add! sb (make-label "── View ──"))

(box-add! sb
  (make-slider "Zoom" 2 80 1 10
    (lambda (v) (set! *zoom* (* v 0.1)))))

(box-add! sb
  (make-slider "Point size" 3 40 1 10
    (lambda (v) (set! *pt-size* (* v 0.25)))))

(box-add! sb (make-separator))
(box-add! sb (make-label "── Rotation ──"))

(box-add! sb
  (make-slider "Spin speed" -40 40 1 4
    (lambda (v) (set! *spin* (* v 0.001)))))

(box-add! sb
  (make-toggle "Auto-spin" #t
    (lambda (on) (set! *auto-spin* on))))

(box-add! sb (make-separator))
(box-add! sb (make-label "── Lattice ──"))
(box-add! sb (make-label "(2k+1)³ points)"))

(define *k-choice* 12)

(box-add! sb
  (make-slider "Shell depth k" 3 20 1 12
    (lambda (v)
      (set! *k-choice* (inexact->exact (round v))))))

(box-add! sb
  (make-button "Rebuild lattice"
    (lambda () (rebuild! *k-choice*))))

(box-add! sb (make-separator))

(box-add! sb
  (make-button "Reset view"
    (lambda ()
      (set! *azimuth*   0.5)
      (set! *elevation* 0.28))))

(box-add! sb
  (make-button "Quit" quit-event-loop))

;;; ─── Canvas setup ────────────────────────────────────────────────────────────

(define canvas #f)
(define anim-timer #f)

(window-on-realize! win
  (lambda ()
    (set! canvas (window-canvas win))

    (canvas-on-draw! canvas draw-frame)

    (canvas-on-mouse! canvas
      (lambda (type btn mx my mods)
        (cond
          ((and (eq? type 'press) (eq? btn 'left))
           (set! *dragging* #t)
           (set! *drag-x0*  mx)
           (set! *drag-y0*  my)
           (set! *az0*      *azimuth*)
           (set! *el0*      *elevation*))
          ((eq? type 'release)
           (set! *dragging* #f))
          ((and (eq? type 'move) *dragging*)
           (set! *azimuth*
             (+ *az0* (* (- mx *drag-x0*) 0.007)))
           (set! *elevation*
             (max -1.5 (min 1.5
               (+ *el0* (* (- my *drag-y0*) 0.007)))))))))

    (set! anim-timer
      (make-timer 40   ; 25 fps — headroom for projection of ~16k points
        (lambda ()
          (step!)
          (canvas-redraw! canvas))))
    (timer-start! anim-timer)))

(window-on-key! win
  (lambda (key mods)
    (cond
      ((equal? key "r")
       (set! *azimuth*   0.5)
       (set! *elevation* 0.28))
      ((or (equal? key "q") (equal? key "Escape"))
       (quit-event-loop)))))

(window-on-close! win
  (lambda ()
    (when anim-timer (timer-stop! anim-timer))
    (quit-event-loop)))

(window-show! win)
(run-event-loop)
