;;; frac-calc-demo.scm
;;;
;;; Fractional derivative visualiser: D^α[f(x)] for α ∈ [0, 2].
;;;
;;; A rainbow of curves shows D^α f for the discrete family
;;; α = 0, 0.25, 0.5, … 2.0.  The current α from the slider is drawn
;;; bright and thick on top.  As α sweeps from 0 → 1 → 2 the curves
;;; interpolate smoothly between the function, its derivative, and its
;;; second derivative.
;;;
;;; Controls:
;;;   Function dropdown   — choose f(x)
;;;   α slider            — fractional order (0 = f itself, 1 = f', 2 = f'')
;;;   n slider            — exponent for the xⁿ preset
;;;   x max / y scale     — viewport
;;;
;;; Run: ./build-release/curry examples/frac-calc-demo.scm

(import (curry qt6))
(import (scheme base))
(import (scheme inexact))
(import (scheme write))

;;; ---- Symbolic setup ------------------------------------------------

(symbolic x)

(define presets
  (list
    (list "xⁿ  (adjust n)"    #f)
    (list "x² + x"            (+ (expt x 2) x))
    (list "3x³ − 2x"          (- (* 3 (expt x 3)) (* 2 x)))
    (list "x⁴ − 4x²"         (- (expt x 4) (* 4 (expt x 2))))
    (list "eˣ"                (exp x))
    (list "e^(2x)"            (exp (* 2 x)))))

;;; ---- State ---------------------------------------------------------

(define *alpha*      0.5)
(define *n-exp*      2.0)
(define *preset-idx* 0)
(define *x-max*      4.0)
(define *y-scale*    1.0)

;;; Discrete α family for the background rainbow
(define *family-alphas*
  '(0.0 0.25 0.5 0.75 1.0 1.25 1.5 1.75 2.0))

;;; Cached evaluators: list of (alpha . lambda)
(define *family-fns*  '())
(define *current-fn*  #f)   ; evaluator for the current α

;;; Sidebar labels
(define *label-f*     #f)
(define *label-da*    #f)
(define *canvas*      #f)

;;; ---- Helpers -------------------------------------------------------

(define (expr->str e)
  (let ((p (open-output-string)))
    (write e p)
    (get-output-string p)))

(define (get-base-expr)
  (if (= *preset-idx* 0)
      (expt x (exact->inexact *n-exp*))
      (cadr (list-ref presets *preset-idx*))))

;;; Evaluate a symbolic expr at a numeric x-value; return +nan.0 on failure.
(define (make-evaluator sym-expr)
  (lambda (xv)
    (let ((r (substitute sym-expr x xv)))
      (if (or (symbolic? r) (not (real? r)))
          +nan.0
          (exact->inexact r)))))

;;; Recompute family curves and labels.
(define (rebuild-all!)
  (let ((base (get-base-expr)))
    (set! *family-fns*
      (map (lambda (a)
             (cons a (make-evaluator (frac-diff base a x))))
           *family-alphas*))
    (rebuild-current! base)
    (when *label-f*
      (label-set-text! *label-f*
        (string-append "f(x) = " (expr->str base))))))

(define (rebuild-current! base)
  (let ((d (frac-diff base *alpha* x)))
    (set! *current-fn* (make-evaluator d))
    (when *label-da*
      (label-set-text! *label-da*
        (string-append "D^α f = " (expr->str d))))))

;;; HSV → RGB (all values in [0, 1]).
(define (hsv->rgb h s v)
  (let* ((h6 (* h 6.0))
         (i  (inexact->exact (floor h6)))
         (f  (- h6 (exact->inexact i)))
         (p  (* v (- 1.0 s)))
         (q  (* v (- 1.0 (* s f))))
         (tk (* v (- 1.0 (* s (- 1.0 f))))))
    (case (modulo i 6)
      ((0) (list v  tk p))
      ((1) (list q  v  p))
      ((2) (list p  v  tk))
      ((3) (list p  q  v))
      ((4) (list tk p  v))
      (else (list v  p  q)))))

;;; α ∈ [0,2] → hue: blue (0) → cyan → green (1) → yellow → red (2).
(define (alpha->hue a)
  (* (/ (- 2.0 a) 2.0) 0.67))

;;; ---- Coordinate mapping --------------------------------------------

(define (x->px xv w) (* (/ xv *x-max*) (- w 40)))
(define (y->py yv h) (- (* h 0.75) (* yv *y-scale* (/ h 9.0))))

;;; ---- Drawing -------------------------------------------------------

(define (draw-curve! painter w h fn r g b opacity thick?)
  (gfx-set-pen-color! painter r g b opacity)
  (gfx-set-pen-width! painter (if thick? 2.5 1.0))
  (let* ((steps 500)
         (dx    (/ *x-max* steps)))
    (let loop ((i 0) (px0 #f) (py0 #f))
      (when (<= i steps)
        (let* ((xv  (* i dx))
               (yv  (fn xv))
               (px  (x->px xv w))
               (py  (y->py yv h))
               (ok? (and (finite? yv) (< (abs yv) 400.0))))
          (when (and ok? px0 (< (abs (- py py0)) (* 2.5 h)))
            (gfx-draw-line! painter px0 py0 px py))
          (loop (+ i 1)
                (if ok? px #f)
                (if ok? py #f)))))))

(define (draw-axes! painter w h)
  (gfx-set-pen-color! painter 0.35 0.35 0.35 1.0)
  (gfx-set-pen-width! painter 1.0)
  (let ((ay (y->py 0.0 h)))
    (gfx-draw-line! painter 0 ay (- w 40) ay))
  (gfx-draw-line! painter 1 0 1 h)
  ;; x-axis ticks and labels
  (let loop ((i 0))
    (when (<= i (inexact->exact (floor *x-max*)))
      (let ((px (x->px (exact->inexact i) w))
            (ay (y->py 0.0 h)))
        (gfx-set-pen-color! painter 0.35 0.35 0.35 1.0)
        (gfx-draw-line! painter px (- ay 5) px (+ ay 5))
        (gfx-set-color! painter 0.5 0.5 0.5 1.0)
        (gfx-set-font! painter "monospace" 10)
        (gfx-draw-text! painter (- px 3) (+ ay 16) (number->string i)))
      (loop (+ i 1)))))

;;; Vertical color bar on the right: maps α range to hue.
(define (draw-color-bar! painter w h)
  (let* ((bx   (- w 22))
         (by   30)
         (bh   (- h 60))
         (steps 40))
    (let loop ((i 0))
      (when (< i steps)
        (let* ((t  (/ (exact->inexact i) steps))
               (a  (* t 2.0))
               (y1 (+ by (* t bh)))
               (y2 (+ by (* (/ (+ i 1) steps) bh)))
               (rgb (hsv->rgb (alpha->hue a) 0.85 0.9)))
          (gfx-set-pen-color! painter (car rgb) (cadr rgb) (caddr rgb) 0.85)
          (gfx-set-pen-width! painter 14.0)
          (gfx-draw-line! painter bx y1 bx y2))
        (loop (+ i 1))))
    ;; α labels
    (gfx-set-color! painter 0.6 0.6 0.6 1.0)
    (gfx-set-font! painter "monospace" 9)
    (gfx-draw-text! painter (- w 38) (+ by 4)      "α=0")
    (gfx-draw-text! painter (- w 38) (+ by (/ bh 2)) "α=1")
    (gfx-draw-text! painter (- w 38) (+ by bh -4) "α=2")
    ;; tick for current α position
    (let ((cy (+ by (* (/ *alpha* 2.0) bh))))
      (gfx-set-pen-color! painter 1.0 1.0 1.0 1.0)
      (gfx-set-pen-width! painter 1.5)
      (gfx-draw-line! painter (- bx 9) cy (+ bx 9) cy))))

(define (draw-on! painter w h)
  (gfx-clear! painter 0.07 0.07 0.11)
  (gfx-set-antialias! painter #t)
  (draw-axes! painter w h)
  ;; Background rainbow family
  (for-each
    (lambda (entry)
      (let* ((a    (car entry))
             (fn   (cdr entry))
             (rgb  (hsv->rgb (alpha->hue a) 0.8 0.85))
             (r    (car rgb)) (g (cadr rgb)) (b (caddr rgb)))
        (draw-curve! painter w h fn r g b 0.30 #f)))
    *family-fns*)
  ;; Current α — bright and thick
  (when *current-fn*
    (let* ((rgb (hsv->rgb (alpha->hue *alpha*) 0.9 1.0))
           (r   (car rgb)) (g (cadr rgb)) (b (caddr rgb)))
      (draw-curve! painter w h *current-fn* r g b 1.0 #t)))
  ;; α readout
  (gfx-set-color! painter 1.0 1.0 1.0 0.9)
  (gfx-set-font! painter "monospace" 14)
  (gfx-draw-text! painter 12 22
    (string-append "D^" (number->string (exact->inexact *alpha*)) " f(x)"))
  (draw-color-bar! painter w h))

;;; ---- Main window ---------------------------------------------------

(define win    (make-window "Fractional Calculus  —  D^α[f(x)]" 1100 680))
(define canvas (window-canvas win))
(set! *canvas* canvas)

(canvas-on-draw! canvas draw-on!)

;;; ---- Sidebar -------------------------------------------------------

(define sb (window-sidebar win))

(box-add! sb (make-label "Function  f(x)"))
(box-add! sb
  (make-dropdown
    (map car presets) 0
    (lambda (idx)
      (set! *preset-idx* idx)
      (rebuild-all!)
      (canvas-redraw! canvas))))

(box-add! sb (make-separator))

(box-add! sb (make-label "Fractional order  α"))
(box-add! sb
  (make-slider "α" 0.0 2.0 0.01 *alpha*
    (lambda (v)
      (set! *alpha* v)
      (rebuild-current! (get-base-expr))
      (canvas-redraw! canvas))))

(box-add! sb (make-separator))

(box-add! sb (make-label "Exponent  n  (xⁿ preset)"))
(box-add! sb
  (make-slider "n" 0.5 5.0 0.1 *n-exp*
    (lambda (v)
      (set! *n-exp* v)
      (when (= *preset-idx* 0)
        (rebuild-all!)
        (canvas-redraw! canvas)))))

(box-add! sb (make-separator))

(box-add! sb
  (make-slider "x max" 1.0 10.0 0.5 *x-max*
    (lambda (v)
      (set! *x-max* v)
      (canvas-redraw! canvas))))

(box-add! sb
  (make-slider "y scale" 0.1 5.0 0.05 *y-scale*
    (lambda (v)
      (set! *y-scale* v)
      (canvas-redraw! canvas))))

(box-add! sb (make-separator))

(set! *label-f*  (make-label "f(x) = "))
(set! *label-da* (make-label "D^α f = "))
(box-add! sb *label-f*)
(box-add! sb *label-da*)

(define sbar (window-status-bar win))
(statusbar-set-text! sbar
  "Dim rainbow = D^α family  ·  Bright = current α  ·  Blue α=0 → Green α=1 → Red α=2")

;;; ---- Go ------------------------------------------------------------

(rebuild-all!)
(window-on-close! win quit-event-loop)
(window-show! win)
(run-event-loop)
