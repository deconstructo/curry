;;; symbolic-grapher.scm
;;; Version: 1.0
;;;
;;; Interactive function grapher using the symbolic CAS + Qt6.
;;;
;;; For each selected function f(x) the grapher:
;;;   - Computes the symbolic derivative  f'(x) = ∂f/∂x
;;;   - Computes the symbolic antiderivative F(x) = ∫f dx
;;;   - Displays those expressions as text in the sidebar
;;;   - Draws f (white), f' (cyan), and F (orange) on the canvas
;;;
;;; Controls:
;;;   Function dropdown — switch between preset functions
;;;   x-min / x-max sliders — control the visible x range
;;;   y-scale slider — zoom the y axis
;;;   Mouse drag — pan the view
;;;
;;; Run:  ./build/curry examples/symbolic-grapher.scm

(import (curry qt6))
(import (scheme base))
(import (scheme inexact))
(import (scheme write))

;;; ---- Symbolic setup ----------------------------------------------------

(symbolic x)   ; x is now a symbolic variable

;;; Preset functions: (label expr-thunk)
;;; Each thunk is called once to get the symbolic expression for f(x).
;;; We use thunks so the symbolic variable 'x' is captured at call time.

(define presets
  (list
    (list "x²"             (lambda () (expt x 2)))
    (list "x³ − 3x"        (lambda () (+ (expt x 3) (* -3 x))))
    (list "sin(x)"         (lambda () (sin x)))
    (list "cos(x)"         (lambda () (cos x)))
    (list "exp(x)"         (lambda () (exp x)))
    (list "1/x"            (lambda () (/ 1 x)))
    (list "x·sin(x)"       (lambda () (* x (sin x))))
    (list "sin(x)/x"       (lambda () (/ (sin x) x)))
    (list "√x"             (lambda () (sqrt x)))
    (list "x²·exp(−x²)"   (lambda () (* (expt x 2) (exp (- (expt x 2))))))))

;;; ---- Current CAS state -------------------------------------------------

(define *f-expr*  #f)   ; symbolic expression for f
(define *df-expr* #f)   ; symbolic expression for f'
(define *Fi-expr* #f)   ; symbolic expression for ∫f

;;; Build evaluation lambdas from a symbolic expression.
;;; Returns a lambda (v → number) that substitutes x=v and returns a flonum.
;;; Returns #f if the expression is still symbolic after substitution
;;; (e.g. unevaluated ∫ node) — the grapher skips those curves.

(define (compile-expr expr)
  (lambda (v)
    (let ((r (substitute expr x v)))
      (if (symbolic? r)
          +nan.0              ; unevaluated node — produce NaN so line breaks
          (exact->inexact r)))))

(define *f-fn*  #f)
(define *df-fn* #f)
(define *Fi-fn* #f)

(define (load-preset! idx)
  (let* ((entry  (list-ref presets idx))
         (expr   ((cadr entry)))
         (d-expr (∂ expr x))
         (i-expr (∫ expr x)))
    (set! *f-expr*  expr)
    (set! *df-expr* d-expr)
    (set! *Fi-expr* i-expr)
    (set! *f-fn*    (compile-expr expr))
    (set! *df-fn*   (compile-expr d-expr))
    (set! *Fi-fn*   (compile-expr i-expr))))

;;; ---- View state ---------------------------------------------------------

(define *x-min*   -4.0)
(define *x-max*    4.0)
(define *y-scale*  1.0)   ; pixels per unit at scale 1

;;; ---- Sidebar labels for the CAS output ---------------------------------

(define *label-f*  #f)
(define *label-df* #f)
(define *label-Fi* #f)

(define (expr->str e)
  (if e
      (let ((p (open-output-string)))
        (write e p)
        (get-output-string p))
      ""))

(define (update-labels!)
  (when *label-f*
    (label-set-text! *label-f*  (string-append "f  = " (expr->str *f-expr*)))
    (label-set-text! *label-df* (string-append "f' = " (expr->str *df-expr*)))
    (label-set-text! *label-Fi* (string-append "∫f = " (expr->str *Fi-expr*)))))

;;; ---- Drawing -----------------------------------------------------------

;;; Map (x-value, y-value) → canvas pixel coordinates.
;;; canvas centre is the origin; y increases upward.
(define (x->px xv w)
  (+ (* (/ (- xv *x-min*) (- *x-max* *x-min*)) w)))

(define (y->py yv h)
  (- (/ h 2) (* yv *y-scale* (/ h 6))))

;;; Draw a function given a lambda (v → flonum).
;;; Segments where the value is NaN or infinite are skipped (no line drawn),
;;; which naturally handles poles, unevaluated nodes, etc.

(define (draw-fn! painter w h fn r g b)
  (gfx-set-pen-color! painter r g b 1.0)
  (gfx-set-pen-width! painter 2.0)
  (let* ((steps 600)
         (dx    (/ (- *x-max* *x-min*) steps)))
    (let loop ((i 0) (prev-px #f) (prev-py #f))
      (when (<= i steps)
        (let* ((xv  (+ *x-min* (* i dx)))
               (yv  (fn xv))
               (px  (x->px xv w))
               (py  (y->py yv h))
               (ok? (and (finite? yv)
                         (< (abs yv) 1e6))))   ; clamp extreme values
          (when (and ok? prev-px
                     (< (abs (- py prev-py)) (* 3 h)))   ; no clipped jumps
            (gfx-draw-line! painter prev-px prev-py px py))
          (loop (+ i 1)
                (if ok? px #f)
                (if ok? py #f)))))))

;;; Draw axes (x=0 and y=0 lines).

(define (draw-axes! painter w h)
  (gfx-set-pen-color! painter 0.4 0.4 0.4 1.0)
  (gfx-set-pen-width! painter 1.0)
  ; x-axis (y=0)
  (let ((py (y->py 0.0 h)))
    (gfx-draw-line! painter 0 py w py))
  ; y-axis (x=0)
  (let ((px (x->px 0.0 w)))
    (gfx-draw-line! painter px 0 px h))
  ; tick marks on x-axis
  (gfx-set-pen-color! painter 0.3 0.3 0.3 1.0)
  (let ((py (y->py 0.0 h))
        (n  (inexact->exact (floor (- *x-max* *x-min*)))))
    (let loop ((i (inexact->exact (ceiling *x-min*))))
      (when (<= i (inexact->exact (floor *x-max*)))
        (let ((px (x->px (exact->inexact i) w)))
          (gfx-draw-line! painter px (- py 4) px (+ py 4)))
        (loop (+ i 1)))))
  ; x-range labels
  (gfx-set-color! painter 0.5 0.5 0.5 1.0)
  (gfx-set-font! painter "monospace" 10)
  (gfx-draw-text! painter 4 (- h 4)
                  (string-append "x: "
                    (number->string (inexact->exact (round (* *x-min* 100)))  )
                    "/100 … "
                    (number->string (inexact->exact (round (* *x-max* 100))))
                    "/100")))

;;; Legend drawn at top-left.

(define (draw-legend! painter)
  (gfx-set-font! painter "monospace" 12)
  (gfx-set-color! painter 1.0 1.0 1.0 1.0)
  (gfx-draw-text! painter 10 20 "f(x)")
  (gfx-set-color! painter 0.0 1.0 1.0 1.0)
  (gfx-draw-text! painter 10 36 "f'(x)")
  (gfx-set-color! painter 1.0 0.6 0.1 1.0)
  (gfx-draw-text! painter 10 52 "∫f(x)dx"))

(define (on-draw! painter w h)
  (gfx-clear! painter 0.08 0.08 0.12)
  (gfx-set-antialias! painter #t)
  (draw-axes! painter w h)
  (when *Fi-fn* (draw-fn! painter w h *Fi-fn* 1.0 0.6 0.1))  ; orange — antideriv
  (when *df-fn* (draw-fn! painter w h *df-fn* 0.0 1.0 1.0))  ; cyan   — derivative
  (when *f-fn*  (draw-fn! painter w h *f-fn*  1.0 1.0 1.0))  ; white  — function
  (draw-legend! painter))

;;; ---- Mouse pan ---------------------------------------------------------

(define *drag-start-x*   #f)
(define *drag-start-xmin* #f)
(define *drag-start-xmax* #f)

(define (on-mouse! event btn mx my canvas)
  (cond
    ((eq? event 'press)
     (set! *drag-start-x*    mx)
     (set! *drag-start-xmin* *x-min*)
     (set! *drag-start-xmax* *x-max*))
    ((and (eq? event 'move) *drag-start-x*)
     (let* ((range (- *drag-start-xmax* *drag-start-xmin*))
            (w     (qt-widget-width canvas))
            (dx    (* (/ (- *drag-start-x* mx) w) range)))
       (set! *x-min* (+ *drag-start-xmin* dx))
       (set! *x-max* (+ *drag-start-xmax* dx))
       (canvas-redraw! canvas)))
    ((eq? event 'release)
     (set! *drag-start-x* #f))))

;;; ---- Main window -------------------------------------------------------

(define win (make-window "Symbolic Grapher" 1100 700))
(define canvas (window-canvas win))

;;; Draw callback
(canvas-on-draw! canvas on-draw!)

;;; Mouse callback — capture canvas in closure
(canvas-on-mouse! canvas
  (lambda (event btn mx my)
    (on-mouse! event btn mx my canvas)))

;;; ---- Sidebar -----------------------------------------------------------

(define sb (window-sidebar win))

;;; Function picker
(box-add! sb (make-label "Function"))
(box-add! sb
  (make-dropdown
    (map car presets)
    0
    (lambda (idx)
      (load-preset! idx)
      (update-labels!)
      (canvas-redraw! canvas))))

(box-add! sb (make-separator))

;;; CAS output labels
(set! *label-f*  (make-label "f  = "))
(set! *label-df* (make-label "f' = "))
(set! *label-Fi* (make-label "∫f = "))
(box-add! sb *label-f*)
(box-add! sb *label-df*)
(box-add! sb *label-Fi*)

(box-add! sb (make-separator))

;;; x-range sliders
(define sl-xmin
  (make-slider "x min" -20.0 0.0 0.5 *x-min*
    (lambda (v)
      (set! *x-min* v)
      (canvas-redraw! canvas))))

(define sl-xmax
  (make-slider "x max" 0.0 20.0 0.5 *x-max*
    (lambda (v)
      (set! *x-max* v)
      (canvas-redraw! canvas))))

(box-add! sb sl-xmin)
(box-add! sb sl-xmax)

;;; y-scale slider
(define sl-yscale
  (make-slider "y scale" 0.1 5.0 0.1 *y-scale*
    (lambda (v)
      (set! *y-scale* v)
      (canvas-redraw! canvas))))

(box-add! sb sl-yscale)

(box-add! sb (make-separator))

;;; Status bar
(define status-bar (window-status-bar win))
(statusbar-set-text! status-bar
  "Drag to pan  ·  Use sliders to adjust range and scale")

;;; ---- Load first preset and go -----------------------------------------

(load-preset! 0)
(update-labels!)

(window-on-close! win quit-event-loop)
(window-show! win)
(run-event-loop)
