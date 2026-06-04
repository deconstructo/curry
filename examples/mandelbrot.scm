;;; mandelbrot.scm — Hypercomplex Mandelbrot viewer (GPU edition)
;;; Version: 2.0
;;;
;;; Renders directly on the GPU via a GLSL fragment shader.
;;; Each pixel is computed independently on thousands of shader cores;
;;; the whole frame finishes in a few milliseconds at any resolution.
;;;
;;; Iterates  z ← z² + c  in three Cayley-Dickson algebras:
;;;   Complex    (2 real dims)  — the classic Mandelbrot set
;;;   Quaternion (4 real dims)  — 2-D slice through ℍ
;;;   Octonion   (8 real dims)  — 2-D slice through 𝕆
;;;
;;; Slice controls (Quaternion / Octonion mode):
;;;   θ rotates the first  pixel axis through the e₀-e₂ plane
;;;   φ rotates the second pixel axis through the e₁-e₃ plane
;;; Sweeping these sliders flies through the 4D/8D set.
;;;
;;; Controls:
;;;   Left-drag          pan
;;;   Scroll wheel       zoom (centred on cursor); trackpad pinch works too
;;;   Double-click       zoom 2× to cursor
;;;   R                  reset view
;;;
;;; Run:  ./build/curry examples/mandelbrot.scm

(import (curry qt6))
(import (scheme base))
(import (scheme inexact))

;;; ── View state ───────────────────────────────────────────────────────────────

(define *algebra*   'complex)   ; 'complex | 'quaternion | 'octonion
(define *max-iter*  150)
(define *cx*       -0.5)        ; world-space view centre
(define *cy*        0.0)
(define *zoom*    200.0)        ; pixels per world unit
(define *theta*     0.0)        ; slice rotation θ  (quaternion/octonion)
(define *phi*       0.0)        ; slice rotation φ

(define *W* 800)
(define *H* 600)

;;; ── Drag state ───────────────────────────────────────────────────────────────

(define *drag-x* #f)
(define *drag-y* #f)
(define *drag-cx* 0.0)
(define *drag-cy* 0.0)

;;; ── Canvas handle ────────────────────────────────────────────────────────────

(define *canvas* #f)

(define (set-algebra! sym)
  (set! *algebra* sym))

;;; ── Draw ─────────────────────────────────────────────────────────────────────

(define (draw-frame painter w h)
  ;; GPU renders the fractal directly into the framebuffer.
  (gfx-mandelbrot-gpu! painter *cx* *cy* *zoom* *max-iter* *algebra* *theta* *phi*)
  ;; HUD drawn by QPainter on top (beginNativePainting/end already closed above).
  (gfx-set-antialias! painter #t)
  (gfx-set-color! painter 1.0 1.0 1.0 0.65)
  (gfx-draw-text! painter 8 (- h 10)
    (string-append
      (case *algebra*
        ((complex)    "C  2D")
        ((quaternion) "H  4D slice")
        (else         "O  8D slice"))
      "  iter=" (number->string *max-iter*)
      "  zoom×" (number->string (inexact->exact (round *zoom*))))))

;;; ── Input ────────────────────────────────────────────────────────────────────

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
    (canvas-redraw! *canvas*)))

(define (reset-view!)
  (set! *cx* -0.5)
  (set! *cy*  0.0)
  (set! *zoom* 200.0)
  (canvas-redraw! *canvas*))

(define (zoom-step! factor)
  (set! *zoom* (* *zoom* factor))
  (canvas-redraw! *canvas*))

(define (zoom-to-cursor! factor sx sy)
  (let* ((wx (/ (- (inexact sx) (/ (inexact *W*) 2.0)) *zoom*))
         (wy (/ (- (inexact sy) (/ (inexact *H*) 2.0)) *zoom*))
         (new-zoom (* *zoom* factor)))
    (set! *cx* (- (+ *cx* wx) (/ (- (inexact sx) (/ (inexact *W*) 2.0)) new-zoom)))
    (set! *cy* (- (+ *cy* wy) (/ (- (inexact sy) (/ (inexact *H*) 2.0)) new-zoom)))
    (set! *zoom* new-zoom)
    (canvas-redraw! *canvas*)))

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
      (reset-view!))))

(box-add! sidebar (make-separator))

;; Iterations
(box-add! sidebar (make-label "Max iterations"))
(box-add! sidebar
  (make-slider "Iterations" 20 400 10 150
    (lambda (v)
      (set! *max-iter* v)
      (canvas-redraw! *canvas*))))

(box-add! sidebar (make-separator))

;; Slice plane (meaningful for ℍ/𝕆)
(box-add! sidebar (make-label "Slice plane  (ℍ / 𝕆)"))
(box-add! sidebar
  (make-slider "θ  e₀↔e₂" -100 100 1 0
    (lambda (v)
      (set! *theta* (* v 0.031416))
      (canvas-redraw! *canvas*))))
(box-add! sidebar
  (make-slider "φ  e₁↔e₃" -100 100 1 0
    (lambda (v)
      (set! *phi* (* v 0.031416))
      (canvas-redraw! *canvas*))))

(box-add! sidebar (make-separator))

(box-add! sidebar
  (make-button "Reset view  (R)" reset-view!))

;; Canvas draw callback
(canvas-on-draw! canvas
  (lambda (painter w h)
    (set! *W* w)
    (set! *H* h)
    (draw-frame painter w h)))

;; Mouse
(canvas-on-mouse! canvas
  (lambda (ev btn x y mods)
    (cond
      ((equal? ev 'press)        (on-mouse-press!   x y btn))
      ((equal? ev 'release)      (on-mouse-release! x y btn))
      ((equal? ev 'move)         (on-mouse-move!    x y))
      ((equal? ev 'double-press) (when (equal? btn 'left)
                                   (zoom-to-cursor! 2.0 x y))))))

;; Scroll wheel / trackpad pinch
(canvas-on-scroll! canvas
  (lambda (dx dy x y mods)
    (zoom-to-cursor! (expt 1.002 dy) x y)))

;; Keyboard
(window-on-key! win
  (lambda (key mods)
    (cond
      ((equal? key "r")                        (reset-view!))
      ((or (equal? key "=") (equal? key "+"))  (zoom-step! 1.5))
      ((equal? key "-")                        (zoom-step! (/ 1.0 1.5)))
      ((or (equal? key "q")
           (equal? key "Escape"))              (quit-event-loop)))))

(window-on-realize! win
  (lambda ()
    (unless (qt-gpu?)
      (display "[mandelbrot] warning: OpenGL unavailable — GPU render will not work\n")
      (flush-output-port (current-output-port)))
    (canvas-redraw! canvas)))

(window-on-close! win
  (lambda ()
    (quit-event-loop)))

(window-show! win)
(run-event-loop)
