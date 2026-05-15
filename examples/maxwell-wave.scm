;;; maxwell-wave.scm  — v1.0
;;;
;;; Interactive animation of a transverse electromagnetic plane wave.
;;;
;;; E_y(x,t) = A·cos(kx − ωt)   polarised in y  (red)
;;; B_z(x,t) = A·cos(kx − ωt)   polarised in z  (blue, oblique)
;;; Propagation direction: +x
;;;
;;; The sidebar displays the symbolic Maxwell equations (verified for k=ω=1):
;;;   Faraday:        ∂E_y/∂x + ∂B_z/∂t = 0  ✓
;;;   Ampere-Maxwell: ∂B_z/∂x + ∂E_y/∂t = 0  ✓
;;;
;;; Controls:
;;;   ω slider  — animation speed (angular frequency)
;;;   k slider  — wavenumber (affects phase velocity vφ = ω/k)
;;;   A slider  — field amplitude
;;;   [Space]   — pause / resume
;;;   [r]       — reset time to 0
;;;   [q]       — quit
;;;
;;; Run:  ./build/curry examples/maxwell-wave.scm

(import (curry qt6))
(import (scheme base))
(import (scheme inexact))
(import (scheme write))

;;; ═══ Symbolic Maxwell verification (unit wave: k=ω=1) ════════════════════════

(symbolic x t)

;; E_y = B_z = cos(x − t)  →  plane wave at light speed (c = 1)
(define dEdx        (simplify (∂ (cos (- x t)) x)))   ; −sin(x−t)
(define dBdt        (simplify (∂ (cos (- x t)) t)))   ;  sin(x−t)
(define dBdx        (simplify (∂ (cos (- x t)) x)))   ; −sin(x−t)
(define dEdt        (simplify (∂ (cos (- x t)) t)))   ;  sin(x−t)
(define faraday-res (simplify (+ dEdx dBdt)))          ; 0 (Faraday)
(define ampere-res  (simplify (+ dBdx dEdt)))          ; 0 (Ampere-Maxwell)

(define (residual->str r)
  (cond ((and (integer? r) (zero? r)) "0   ✓")
        ((symbolic? r)                (sym->infix r))
        (else                         (number->string r))))

;;; ═══ Wave parameters ═════════════════════════════════════════════════════════

(define *omega*  1.0)   ; angular frequency — drives animation speed
(define *k*      1.0)   ; wavenumber — only changes vφ label; display always 2.5 cycles
(define *amp*    0.85)  ; amplitude
(define *time*   0.0)
(define *paused* #f)

(define pi     3.14159265358979323846)
(define two-pi (* 2.0 pi))
(define n-disp 2.5)   ; number of visible wave cycles on screen

;; Field value at screen fraction f ∈ [0,1] and time tv.
;; Phase = 2π · n-disp · f  so we always see exactly n-disp cycles.
(define (field-at f tv)
  (* *amp* (cos (- (* two-pi n-disp f) (* *omega* tv)))))

;;; ═══ Drawing helpers ═════════════════════════════════════════════════════════

;; Filled arrowhead from (x1,y1) toward (x2,y2), drawn at the tip.
(define (draw-arrow! painter x1 y1 x2 y2 r g b a lw)
  (gfx-set-pen-color! painter r g b a)
  (gfx-set-pen-width! painter lw)
  (gfx-draw-line! painter x1 y1 x2 y2)
  (let* ((dx  (- x2 x1))
         (dy  (- y2 y1))
         (len (sqrt (+ (* dx dx) (* dy dy)))))
    (when (> len 6.0)
      (let* ((ux  (/ dx len))   (uy  (/ dy len))   ; unit along arrow
             (px  (- uy))       (py  ux)            ; unit perpendicular
             (hs  4.5)          (hl  11.0)
             (bx  (- x2 (* ux hl)))
             (by  (- y2 (* uy hl))))
        (gfx-set-color! painter r g b a)
        (gfx-fill-polygon! painter
          (list (cons x2 y2)
                (cons (+ bx (* px hs)) (+ by (* py hs)))
                (cons (- bx (* px hs)) (- by (* py hs)))))))))

;; Flat segment vector for gfx-draw-lines!: n-1 connected segments from
;; n consecutive points produced by (sample i) → (x . y).
(define (poly-segs n sample)
  (let ((v (make-vector (* 4 (- n 1)) 0.0)))
    (let loop ((i 0))
      (when (< i (- n 1))
        (let ((p0 (sample i)) (p1 (sample (+ i 1))))
          (vector-set! v (* i 4)       (car p0))
          (vector-set! v (+ (* i 4) 1) (cdr p0))
          (vector-set! v (+ (* i 4) 2) (car p1))
          (vector-set! v (+ (* i 4) 3) (cdr p1)))
        (loop (+ i 1))))
    v))

;;; ═══ Draw callback ═══════════════════════════════════════════════════════════

(define n-curve 400)  ; smooth curve sample count
(define n-arr    14)  ; number of field arrows

;; B-field z-axis projected as 30° oblique (upper-right for +B, lower-left for -B)
(define bz-dx  (cos (/ pi 6.0)))          ;  0.866 (rightward)
(define bz-dy  (- (sin (/ pi 6.0))))      ; -0.5   (upward on screen)

(define (draw painter w h)
  (let* ((tv      *time*)
         (xL      88.0)
         (xR      (- w 20.0))
         (xspan   (- xR xL))
         (cy      (* h 0.5))
         (Es      (/ h 5.0))    ; pixels per unit: E field (vertical)
         (Bs      (/ h 5.2))    ; pixels per unit: B field (oblique)
         (p->sx   (lambda (f) (+ xL (* f xspan)))))

    ;; Background
    (gfx-clear! painter 0.04 0.04 0.09)
    (gfx-set-antialias! painter #t)

    ;; Half-wavelength grid (faint vertical lines at λ/2 intervals, k=1 reference)
    (let ((hlf (/ 1.0 (* 2.0 n-disp)))
          (cnt (inexact->exact (round (* 2.0 n-disp)))))
      (gfx-set-pen-color! painter 0.14 0.14 0.24 1.0)
      (gfx-set-pen-width! painter 0.7)
      (let loop ((i 0))
        (when (<= i cnt)
          (let ((sx (p->sx (* i hlf))))
            (gfx-draw-line! painter sx (* cy 0.22) sx (* cy 1.78)))
          (loop (+ i 1)))))

    ;; Central x-axis
    (gfx-set-pen-color! painter 0.38 0.38 0.44 1.0)
    (gfx-set-pen-width! painter 1.2)
    (gfx-draw-line! painter xL cy xR cy)

    ;; E field curve — two-pass (wide glow + thin core)
    (let ((cv (poly-segs n-curve
                (lambda (i)
                  (let* ((f  (/ (exact->inexact i) (- n-curve 1.0)))
                         (ey (field-at f tv)))
                    (cons (p->sx f)
                          (- cy (* ey Es))))))))
      (gfx-draw-lines! painter cv  1.0 0.22 0.22 0.18  8.0)
      (gfx-draw-lines! painter cv  1.0 0.44 0.44 0.94  1.8))

    ;; B field curve (oblique projection) — two-pass
    (let ((cv (poly-segs n-curve
                (lambda (i)
                  (let* ((f  (/ (exact->inexact i) (- n-curve 1.0)))
                         (bz (field-at f tv)))
                    (cons (+ (p->sx f) (* bz Bs bz-dx))
                          (+ cy        (* bz Bs bz-dy))))))))
      (gfx-draw-lines! painter cv  0.18 0.38 1.0 0.18  8.0)
      (gfx-draw-lines! painter cv  0.36 0.56 1.0 0.94  1.8))

    ;; Field arrows at n-arr evenly-spaced positions
    (let loop ((i 0))
      (when (< i n-arr)
        (let* ((f    (/ (+ (exact->inexact i) 0.5) (exact->inexact n-arr)))
               (sx   (p->sx f))
               (fv   (field-at f tv))
               (epx  (* fv Es))     ; E y-displacement (pixels)
               (bpx  (* fv Bs)))    ; B magnitude (pixels)
          (when (> (abs epx) 5.0)
            (draw-arrow! painter sx cy sx (- cy epx)
                         1.0 0.38 0.38 0.90 1.8))
          (when (> (abs bpx) 5.0)
            (draw-arrow! painter sx cy
                         (+ sx (* bpx bz-dx))
                         (+ cy (* bpx bz-dy))
                         0.32 0.56 1.0 0.90 1.8)))
        (loop (+ i 1))))

    ;; Axis dots
    (gfx-set-color! painter 0.46 0.46 0.50 0.7)
    (let loop ((i 0))
      (when (<= i n-arr)
        (gfx-fill-circle! painter (p->sx (/ (exact->inexact i) (exact->inexact n-arr))) cy 2.2)
        (loop (+ i 1))))

    ;; Coordinate frame (top-left)
    (let* ((ox 38.0) (oy 92.0) (al 28.0))
      (draw-arrow! painter ox oy (+ ox al) oy  0.6 0.6 0.6 0.9 1.5)
      (gfx-set-color! painter 0.60 0.60 0.60 0.85)
      (gfx-set-font!  painter "sans-serif" 10)
      (gfx-draw-text! painter (+ ox al 4) (+ oy 4) "x")

      (draw-arrow! painter ox oy ox (- oy al)  1.0 0.32 0.32 0.9 1.5)
      (gfx-set-color! painter 1.0 0.32 0.32 0.9)
      (gfx-draw-text! painter (- ox 14) (- oy al 2) "E")

      (draw-arrow! painter ox oy
                   (+ ox (* al bz-dx)) (+ oy (* al bz-dy))
                   0.32 0.56 1.0 0.9 1.5)
      (gfx-set-color! painter 0.32 0.56 1.0 0.9)
      (gfx-draw-text! painter (+ ox (* al bz-dx) 4)
                              (+ oy (* al bz-dy) 2) "B"))

    ;; Info panel (top-right)
    (let* ((vp  (/ *omega* (max 0.001 *k*)))
           (px  (- w 212.0))
           (py  10.0))
      (gfx-set-color! painter 0.0 0.0 0.0 0.58)
      (gfx-fill-rect! painter px py 204 84)
      (gfx-set-color! painter 0.75 0.88 1.0 0.95)
      (gfx-set-font!  painter "monospace" 11)
      (gfx-draw-text! painter (+ px 8) (+ py 18)
                      (string-append "vφ = ω/k = " (number->string vp)))
      (gfx-set-color! painter 0.58 1.0 0.58 0.90)
      (gfx-draw-text! painter (+ px 8) (+ py 36) "E · B = 0   (orthogonal)")
      (gfx-set-color! painter 1.0 0.88 0.38 0.90)
      (gfx-draw-text! painter (+ px 8) (+ py 54) "S = E × B   (Poynting →)")
      (gfx-set-color! painter 0.78 0.78 0.78 0.80)
      (gfx-draw-text! painter (+ px 8) (+ py 72)
                      (string-append "A=" (number->string *amp*)
                                     "  ω=" (number->string *omega*)
                                     "  k=" (number->string *k*))))

    ;; Propagation direction arrow (bottom-centre)
    (let ((bx (- (/ w 2.0) 65.0))
          (by (- h 24.0)))
      (draw-arrow! painter bx by (+ bx 130.0) by  1.0 0.88 0.35 0.72 2.2)
      (gfx-set-color! painter 1.0 0.88 0.35 0.72)
      (gfx-set-font!  painter "sans-serif" 11)
      (gfx-draw-text! painter (+ bx 136.0) (+ by 4.0) "propagation"))

    ;; Paused indicator
    (when *paused*
      (gfx-set-color! painter 1.0 0.75 0.20 0.92)
      (gfx-set-font!  painter "sans-serif" 16 #t)
      (gfx-draw-text! painter (- (/ w 2.0) 32.0) 28.0 "PAUSED"))))

;;; ═══ Window ══════════════════════════════════════════════════════════════════

(define *win*    (make-window "Maxwell Plane Wave Explorer" 1060 680))
(define *canvas* (window-canvas *win*))
(define *sb*     (window-sidebar *win*))

;;; ═══ Sidebar ═════════════════════════════════════════════════════════════════

;; Wave controls
(box-add! *sb* (make-label "── Wave controls ──"))
(box-add! *sb* (make-separator))

(box-add! *sb* (make-label "Angular frequency  ω"))
(box-add! *sb*
  (make-slider "ω" 0.2 5.0 0.05 *omega*
    (lambda (v) (set! *omega* v) (canvas-redraw! *canvas*))))

(box-add! *sb* (make-separator))
(box-add! *sb* (make-label "Wavenumber  k"))
(box-add! *sb*
  (make-slider "k" 0.2 5.0 0.05 *k*
    (lambda (v) (set! *k* v) (canvas-redraw! *canvas*))))

(box-add! *sb* (make-separator))
(box-add! *sb* (make-label "Amplitude  A"))
(box-add! *sb*
  (make-slider "A" 0.1 1.5 0.05 *amp*
    (lambda (v) (set! *amp* v) (canvas-redraw! *canvas*))))

(box-add! *sb* (make-separator))

;; Maxwell equations (symbolic, verified at k=ω=1)
(box-add! *sb* (make-label "── Maxwell equations (k=ω=1) ──"))
(box-add! *sb* (make-separator))
(box-add! *sb* (make-label "E_y = B_z = cos(x − t)"))
(box-add! *sb* (make-separator))
(box-add! *sb* (make-label (string-append "∂E_y/∂x = " (sym->infix dEdx))))
(box-add! *sb* (make-label (string-append "∂B_z/∂t = " (sym->infix dBdt))))
(box-add! *sb* (make-label
  (string-append "Faraday:  Σ = " (residual->str faraday-res))))
(box-add! *sb* (make-separator))
(box-add! *sb* (make-label (string-append "∂B_z/∂x = " (sym->infix dBdx))))
(box-add! *sb* (make-label (string-append "∂E_y/∂t = " (sym->infix dEdt))))
(box-add! *sb* (make-label
  (string-append "Ampere:   Σ = " (residual->str ampere-res))))
(box-add! *sb* (make-separator))

;; Buttons
(box-add! *sb*
  (make-button "Pause / Resume  [Space]"
    (lambda () (set! *paused* (not *paused*)))))
(box-add! *sb*
  (make-button "Reset time  [r]"
    (lambda () (set! *time* 0.0))))
(box-add! *sb*
  (make-button "Quit  [q]" quit-event-loop))

;;; ═══ Canvas, keyboard, timer ══════════════════════════════════════════════════

(canvas-on-draw! *canvas* draw)

(window-on-key! *win*
  (lambda (key mods)
    (cond ((equal? key "space")  (set! *paused* (not *paused*)))
          ((equal? key "r")      (set! *time* 0.0))
          ((equal? key "q")      (quit-event-loop))
          ((equal? key "Escape") (quit-event-loop)))))

(define *timer*
  (make-timer 16
    (lambda ()
      (unless *paused*
        (set! *time* (+ *time* 0.016)))
      (canvas-redraw! *canvas*))))

(timer-start! *timer*)

(statusbar-set-text! (window-status-bar *win*)
  "Space = pause · r = reset time · ω/k/A sliders · q = quit")

(window-on-close! *win* quit-event-loop)
(window-show! *win*)
(run-event-loop)
