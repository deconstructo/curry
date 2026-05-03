;;; mandelbrot.scm — Hypercomplex Mandelbrot viewer
;;;
;;; Iterates  z ← z² + c  in three Cayley-Dickson algebras:
;;;   Complex    (2 real dims)  — the classic Mandelbrot set
;;;   Quaternion (4 real dims)  — 2-D slice through ℍ
;;;   Octonion   (8 real dims)  — 2-D slice through 𝕆
;;;
;;; In every Cayley-Dickson algebra, squaring follows:
;;;   (a + v)² = (a² − |v|²)  +  2a·v
;;; where a is the scalar part and v the pure-imaginary part.
;;; Higher-dimensional sets are therefore "fatter" and more symmetric
;;; than the classic Mandelbrot set.
;;;
;;; Slice controls (Quaternion / Octonion mode):
;;;   θ rotates the first  pixel axis through the e₀-e₂ plane
;;;   φ rotates the second pixel axis through the e₁-e₃ plane
;;; Sweeping these sliders flies through the 4D/8D set.
;;;
;;; Controls:
;;;   Left-drag          pan
;;;   Scroll wheel       zoom (centred on cursor)
;;;   Double-click       zoom 2× to cursor
;;;   R                  reset view
;;;
;;; Run:  ./build/curry examples/mandelbrot.scm

(import (curry qt6))
(import (scheme base))
(import (scheme inexact))

;;; ── Configuration ────────────────────────────────────────────────────────────

(define *ESCAPE-SQ* 4.0)
(define *LOG2*      (log 2.0))
(define *STEP*      4)       ; block size in pixels (4 = 200×150 at 800×600)
(define *ROWS-PER-TICK* 8)   ; scanlines rendered per 16ms timer tick

;;; ── View state ───────────────────────────────────────────────────────────────

(define *algebra*   'complex)   ; 'complex | 'quaternion | 'octonion
(define *dims*      2)
(define *max-iter*  150)
(define *cx*       -0.5)        ; world-space view centre
(define *cy*        0.0)
(define *zoom*    200.0)        ; pixels per world unit
(define *theta*     0.0)        ; slice rotation θ  (quaternion/octonion)
(define *phi*       0.0)        ; slice rotation φ

(define *W* 800)
(define *H* 600)

;;; ── Render state (all owned by main thread / timer) ──────────────────────────

(define *frame-buf*  #f)   ; flat vector, *buf-bw* × *buf-bh* smooth-t values
(define *buf-bw*     0)    ; columns in *frame-buf* (= quotient W STEP at alloc time)
(define *buf-bh*     0)    ; rows    in *frame-buf*
(define *render-row* 0)    ; next block-row to compute
(define *rendering*  #f)   ; #t while a render is in progress
(define *render-tag* 0)    ; incremented on each new request (for cancellation)

(define *canvas* #f)
(define *render-timer* #f)

;;; ── Drag state ───────────────────────────────────────────────────────────────

(define *drag-x* #f)
(define *drag-y* #f)
(define *drag-cx* 0.0)
(define *drag-cy* 0.0)

;;; ── Algebra ──────────────────────────────────────────────────────────────────

(define (set-algebra! sym)
  (set! *algebra* sym)
  (set! *dims*
    (case sym
      ((complex)    2)
      ((quaternion) 4)
      (else         8))))

;;; ── Colour palette ───────────────────────────────────────────────────────────

(define *PAL-N* 512)
(define *palette* (make-vector (* *PAL-N* 3) 0.0))

(define (build-palette!)
  (do ((i 0 (+ i 1))) ((= i *PAL-N*))
    (let* ((t (* (/ (inexact i) (inexact *PAL-N*)) 6.2831853))
           (r (+ 0.5 (* 0.5 (sin t))))
           (g (+ 0.5 (* 0.5 (sin (+ t 2.094)))))
           (b (+ 0.5 (* 0.5 (sin (+ t 4.189))))))
      (vector-set! *palette* (* i 3)       r)
      (vector-set! *palette* (+ (* i 3) 1) g)
      (vector-set! *palette* (+ (* i 3) 2) b))))

(define (fmod x y)
  ;; Flonum modulo: result in [0, y)
  (let ((q (* (floor (/ x y)) y)))
    (- x q)))

(define (palette-rgb t max-t)
  ;; Returns (values r g b).  Interior (t >= max-t) → black.
  (if (or (>= t max-t) (not (finite? t)))
      (values 0.0 0.0 0.0)
      (let* ((phase (fmod (* t 7.3) (inexact *PAL-N*)))
             (i     (inexact->exact (floor (max 0.0 phase))))
             (i     (min (- *PAL-N* 1) i))
             (b     (* i 3)))
        (values (vector-ref *palette* b)
                (vector-ref *palette* (+ b 1))
                (vector-ref *palette* (+ b 2))))))

;;; ── 2-D (complex) Mandelbrot — flat arithmetic, no allocation ────────────────

(define (mandel-2d cx cy mi)
  (let loop ((zx 0.0) (zy 0.0) (n 0))
    (let ((x2 (* zx zx)) (y2 (* zy zy)))
      (cond
        ((> (+ x2 y2) *ESCAPE-SQ*)
         (- (+ (inexact n) 1.0)
            (/ (log (/ (log (+ x2 y2)) (* 2.0 *LOG2*))) *LOG2*)))
        ((= n mi)
         (inexact mi))
        (else
         (loop (+ (- x2 y2) cx)
               (+ (* 2.0 zx zy) cy)
               (+ n 1)))))))

;;; ── N-D Mandelbrot — pre-allocated work vectors ──────────────────────────────
;;;
;;; (a + v)² = (a²−|v|²) + 2a·v   for any Cayley-Dickson algebra.
;;; Allocate work vectors once per request, reuse across all pixels.

(define (make-nd-workspace dims)
  (list (make-vector dims 0.0)   ; z
        (make-vector dims 0.0)   ; tmp (z²)
        (make-vector dims 0.0))) ; c

(define (nd-sq! out in dims)
  (let ((a (vector-ref in 0)))
    (let loop ((k 1) (sq 0.0))
      (if (= k dims)
          (begin
            (vector-set! out 0 (- (* a a) sq))
            (let lp ((k 1))
              (when (< k dims)
                (vector-set! out k (* 2.0 a (vector-ref in k)))
                (lp (+ k 1)))))
          (loop (+ k 1) (+ sq (let ((v (vector-ref in k))) (* v v))))))))

(define (nd-add! out a b dims)
  (let loop ((k 0))
    (when (< k dims)
      (vector-set! out k (+ (vector-ref a k) (vector-ref b k)))
      (loop (+ k 1)))))

(define (nd-norm-sq v dims)
  (let loop ((k 0) (s 0.0))
    (if (= k dims) s
        (loop (+ k 1) (+ s (let ((x (vector-ref v k))) (* x x)))))))

(define (fill-c-nd! cvec wx wy dims theta phi)
  ;; Zero, then set axes using slice rotation.
  ;; u = (cosθ, 0, sinθ, 0, ...)  ← multiplied by wx
  ;; v = (0, cosφ, 0, sinφ, ...)  ← multiplied by wy
  (do ((k 0 (+ k 1))) ((= k dims)) (vector-set! cvec k 0.0))
  (vector-set! cvec 0 (* wx (cos theta)))
  (when (> dims 2)
    (vector-set! cvec 2 (* wx (sin theta))))
  (vector-set! cvec 1 (* wy (cos phi)))
  (when (> dims 3)
    (vector-set! cvec 3 (* wy (sin phi)))))

(define (mandel-nd cvec dims mi z tmp)
  (do ((k 0 (+ k 1))) ((= k dims)) (vector-set! z k 0.0))
  (let loop ((n 0))
    (let ((ns (nd-norm-sq z dims)))
      (cond
        ((> ns *ESCAPE-SQ*)
         (- (+ (inexact n) 1.0)
            (/ (log (/ (log ns) (* 2.0 *LOG2*))) *LOG2*)))
        ((= n mi)
         (inexact mi))
        (else
         (nd-sq!  tmp z dims)
         (nd-add! z tmp cvec dims)
         (loop (+ n 1)))))))

;;; ── Pixel computation (dispatches to 2D or ND path) ─────────────────────────

(define (compute-pixel px py bw bh dims mi theta phi nd-ws)
  ;; bw, bh: canvas dims (for centre calculation)
  (let* ((step *STEP*)
         (wx   (/ (- (inexact px) (/ (inexact bw) 2.0)) *zoom*))
         (wy   (/ (- (inexact py) (/ (inexact bh) 2.0)) *zoom*))
         (cx   (+ *cx* wx))
         (cy   (+ *cy* wy)))
    (if (= dims 2)
        (mandel-2d cx cy mi)
        (let ((z   (list-ref nd-ws 0))
              (tmp (list-ref nd-ws 1))
              (c   (list-ref nd-ws 2)))
          (fill-c-nd! c wx wy dims theta phi)
          ;; For ND: cx, cy are the first two algebra coords via fill-c-nd!
          ;; The view-centre offset is applied to c[0] and c[1] directly.
          (vector-set! c 0 (+ (vector-ref c 0) *cx*))
          (vector-set! c 1 (+ (vector-ref c 1) *cy*))
          (mandel-nd c dims mi z tmp)))))

;;; ── Frame buffer management ──────────────────────────────────────────────────

(define (alloc-frame-buf!)
  (let* ((bw (max 1 (quotient *W* *STEP*)))
         (bh (max 1 (quotient *H* *STEP*))))
    (set! *buf-bw*   bw)
    (set! *buf-bh*   bh)
    (set! *frame-buf* (make-vector (* bw bh) (inexact *max-iter*)))))

(define (request-render!)
  (set! *render-tag* (+ *render-tag* 1))
  (set! *render-row* 0)
  (set! *rendering*  #t)
  (alloc-frame-buf!)
  (when *render-timer* (timer-start! *render-timer*)))

;;; ── Timer tick: render ROWS-PER-TICK block-rows, then redraw ─────────────────

(define (render-tick!)
  (when *rendering*
    (let* ((step  *STEP*)
           (bw    *buf-bw*)
           (bh    *buf-bh*)
           (mi    *max-iter*)
           (dims  *dims*)
           (theta *theta*)
           (phi   *phi*)
           (nd-ws (if (> dims 2) (make-nd-workspace dims) #f))
           (buf   *frame-buf*))
      (let loop ((rows 0))
        (when (and (< *render-row* bh) (< rows *ROWS-PER-TICK*))
          (let ((by *render-row*))
            (do ((bx 0 (+ bx 1)))
                ((= bx bw))
              (let ((t (compute-pixel (* bx step) (* by step) *W* *H* dims mi theta phi nd-ws)))
                (vector-set! buf (+ (* by bw) bx) t))))
          (set! *render-row* (+ *render-row* 1))
          (loop (+ rows 1))))
      (when (>= *render-row* bh)
        (set! *rendering* #f)
        (timer-stop! *render-timer*))
      (when *canvas* (canvas-redraw! *canvas*)))))

;;; ── Draw ─────────────────────────────────────────────────────────────────────

(define (draw-frame painter w h)
  ;; Re-render if the canvas has been resized since the last allocation.
  (when (or (not (= w *W*)) (not (= h *H*)))
    (set! *W* w)
    (set! *H* h)
    (request-render!))

  (gfx-clear! painter 0.0 0.0 0.0)
  (gfx-set-antialias! painter #f)

  ;; Use *buf-bw*/*buf-bh* — dimensions the buffer was actually allocated with —
  ;; never compute from canvas w/h here, which could exceed the buffer size.
  (when (and *frame-buf* (> *buf-bw* 0) (> *buf-bh* 0))
    (let* ((step  *STEP*)
           (bw    *buf-bw*)
           (bh    *buf-bh*)
           (mi    (inexact *max-iter*))
           (buf   *frame-buf*))
      (do ((by 0 (+ by 1)))
          ((= by bh))
        (do ((bx 0 (+ bx 1)))
            ((= bx bw))
          (let ((t (vector-ref buf (+ (* by bw) bx))))
            (unless (>= t mi)
              (call-with-values
                (lambda () (palette-rgb t mi))
                (lambda (r g b)
                  (gfx-set-color! painter r g b 1.0)
                  (gfx-fill-rect! painter
                                  (inexact (* bx step)) (inexact (* by step))
                                  (inexact step) (inexact step))))))))))

  ;; HUD
  (gfx-set-antialias! painter #t)
  (gfx-set-color! painter 1.0 1.0 1.0 0.65)
  (gfx-draw-text! painter 8 (- h 10)
    (string-append
      (case *algebra*
        ((complex)    "C  2D")
        ((quaternion) "H  4D slice")
        (else         "O  8D slice"))
      "  iter=" (number->string *max-iter*)
      "  zoom*" (number->string (inexact->exact (round *zoom*)))
      (if *rendering* "  ..." ""))))

;;; ── Input ────────────────────────────────────────────────────────────────────

(define (screen->world sx sy)
  (values (+ *cx* (/ (- (inexact sx) (/ (inexact *W*) 2.0)) *zoom*))
          (+ *cy* (/ (- (inexact sy) (/ (inexact *H*) 2.0)) *zoom*))))

(define (on-mouse-press! x y btn)
  (when (equal? btn 'left)
    (set! *drag-x*  x)
    (set! *drag-y*  y)
    (set! *drag-cx* *cx*)
    (set! *drag-cy* *cy*)))

(define (on-mouse-release! x y btn)
  (when (equal? btn 'left) (set! *drag-x* #f)))

(define (on-mouse-move! x y)
  (when *drag-x*
    (set! *cx* (- *drag-cx* (/ (- x *drag-x*) *zoom*)))
    (set! *cy* (- *drag-cy* (/ (- y *drag-y*) *zoom*)))
    (request-render!)))

(define (reset-view!)
  (set! *cx* -0.5)
  (set! *cy*  0.0)
  (set! *zoom* 200.0)
  (request-render!))

;;; ── Window and UI ────────────────────────────────────────────────────────────

(define win     (make-window "Hypercomplex Mandelbrot" 1060 680))
(define canvas  (window-canvas win))
(define sidebar (window-sidebar win))

(set! *canvas* canvas)

;; Algebra
(box-add! sidebar (make-label "Algebra"))
(box-add! sidebar
  (make-radio-group
    '("Complex  ℂ (d=2)" "Quaternion  ℍ (d=4)" "Octonion  𝕆 (d=8)")
    0
    (lambda (i)
      (set-algebra! (list-ref '(complex quaternion octonion) i))
      (reset-view!)
      (request-render!))))

(box-add! sidebar (make-separator))

;; Iterations
(box-add! sidebar (make-label "Max iterations"))
(box-add! sidebar
  (make-slider "Iterations" 20 400 10 150
    (lambda (v)
      (set! *max-iter* v)
      (request-render!))))

(box-add! sidebar (make-separator))

;; Slice plane (meaningful for ℍ/𝕆)
(box-add! sidebar (make-label "Slice plane  (ℍ / 𝕆)"))
(box-add! sidebar
  (make-slider "θ  e₀↔e₂" -100 100 1 0
    (lambda (v)
      (set! *theta* (* v 0.031416))   ; v × π/100
      (request-render!))))
(box-add! sidebar
  (make-slider "φ  e₁↔e₃" -100 100 1 0
    (lambda (v)
      (set! *phi* (* v 0.031416))
      (request-render!))))

(box-add! sidebar (make-separator))

(box-add! sidebar
  (make-button "Reset view  (R)" reset-view!))

;; Canvas
(canvas-on-draw! canvas
  (lambda (painter w h)
    (set! *W* w)
    (set! *H* h)
    (draw-frame painter w h)))

;; Qt6 mouse callback: (event-type button x y mods)
;;   event-type : 'press | 'release | 'move
;;   button     : 'left | 'right | 'middle | 'none
(canvas-on-mouse! canvas
  (lambda (ev btn x y mods)
    (cond
      ((equal? ev 'press)   (on-mouse-press!   x y btn))
      ((equal? ev 'release) (on-mouse-release! x y btn))
      ((equal? ev 'move)    (on-mouse-move!    x y)))))

(define (zoom-step! factor)
  (set! *zoom* (* *zoom* factor))
  (request-render!))

(window-on-key! win
  (lambda (key mods)
    (cond
      ((equal? key "r")                     (reset-view!))
      ((or (equal? key "=") (equal? key "+")) (zoom-step! 1.5))
      ((equal? key "-")                     (zoom-step! (/ 1.0 1.5)))
      ((or (equal? key "q")
           (equal? key "Escape"))           (quit-event-loop)))))

(window-on-realize! win
  (lambda ()
    (build-palette!)
    (set! *render-timer*
      (make-timer 16 render-tick!))
    (request-render!)))

(window-on-close! win
  (lambda ()
    (when *render-timer* (timer-stop! *render-timer*))
    (quit-event-loop)))

(window-show! win)
(run-event-loop)
