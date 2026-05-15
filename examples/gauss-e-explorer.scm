;;; gauss-e-explorer.scm — v1.0
;;;
;;; Interactive workbook for Gauss's Law for the Electric Field.
;;; Third in the Maxwell series; companion to faraday-explorer.scm and ampere-explorer.scm.
;;;
;;; ════════════════════════════════════════════════════════════════════════════
;;; BACKGROUND
;;; ════════════════════════════════════════════════════════════════════════════
;;;
;;; Maxwell's first equation:
;;;
;;;   ∇ · E  =  ρ / ε₀
;;;
;;;   ∇ · E   — the "divergence" of the electric field E.
;;;               Divergence measures whether field lines spread out from a point
;;;               (positive divergence = source) or converge into it (negative =
;;;               sink).  Think of it as the net outward flow per unit volume.
;;;               If div = 0, as many field lines leave a region as enter it.
;;;
;;;   ρ / ε₀  — the charge density ρ divided by ε₀.
;;;               ρ > 0 (positive charge) → positive divergence → E spreads outward.
;;;               ρ < 0 (negative charge) → negative divergence → E converges inward.
;;;               ρ = 0 (no charge)       → div E = 0 → field lines don't start or end.
;;;
;;; In plain English:
;;;   "Electric field lines start on positive charges and end on negative charges.
;;;    Everywhere else, div E = 0 — the field just passes through."
;;;
;;; INTEGRAL FORM (the version you can measure directly):
;;;
;;;   ∮_S  E · dA  =  Q_enclosed / ε₀
;;;
;;;   The total electric flux through any closed surface S equals the total charge
;;;   inside it, divided by ε₀.  This is Gauss's Law in integral form.
;;;
;;;   KEY INSIGHT: the flux only depends on the ENCLOSED CHARGE, not on the shape
;;;   of the surface, not on where the charge is inside, not on charges outside.
;;;
;;; THIS DEMO — UNIFORMLY CHARGED SPHERE:
;;;   The red/blue disk is the cross-section of a sphere of radius R with uniform
;;;   charge density ρ (total charge Q = 4πR³ρ/3).
;;;   The white circle is a Gaussian surface of radius r_gauss.
;;;
;;;   Field solution (from Gauss's law + spherical symmetry):
;;;     Inside  (r < R):  E_r = ρ·r / (3ε₀)     [grows linearly — more charge enclosed]
;;;     Outside (r ≥ R):  E_r = ρR³ / (3ε₀r²)   [falls as 1/r² — total Q fixed]
;;;
;;;   The outside field is identical to the field of a point charge Q at the origin.
;;;   This is Gauss's law's most practical result: spherical charge distribution = point charge.
;;;
;;; REAL-WORLD CONNECTIONS:
;;;   • Faraday cage: inside a hollow conductor, E = 0 (all charge on surface,
;;;     div E = 0 inside, so ∮E·dA = 0 for any interior surface, so E = 0).
;;;   • Capacitor: uniform E between plates (∮E·dA = σA/ε₀ through a pillbox surface).
;;;   • Proton and electron: equal magnitude charge → same ∮E·dA around each.
;;;
;;; For the full student guide see: docs/gauss-e-explorer.md
;;; Run: ./build/curry examples/gauss-e-explorer.scm
;;; ════════════════════════════════════════════════════════════════════════════

(import (curry qt6))
(import (scheme base))
(import (scheme inexact))
(import (scheme write))

;;; ════════════════════════════════════════════════════════════════════════════
;;; SYMBOLIC CAS — VERIFYING ∇·E = ρ/ε₀
;;; ════════════════════════════════════════════════════════════════════════════
;;;
;;; Inside a uniform sphere with ρ = ε₀ = 1, the field is E = r⃗/3:
;;;   E_x = x/3,  E_y = y/3,  E_z = z/3
;;;
;;; ∇·E = ∂(x/3)/∂x + ∂(y/3)/∂y + ∂(z/3)/∂z = 1/3 + 1/3 + 1/3 = 1 = ρ/ε₀  ✓

(symbolic x y z)

(define div-E-inside
  (simplify (+ (∂ (/ x 3) x) (∂ (/ y 3) y) (∂ (/ z 3) z))))
; = 1 = ρ/ε₀  ✓

;;; ════════════════════════════════════════════════════════════════════════════
;;; STATE
;;; ════════════════════════════════════════════════════════════════════════════

(define pi 3.14159265358979323846)

(define eps0    1.0)   ; permittivity of free space (simulation units)
(define *rho*   1.0)   ; magnitude of charge density (C/m³)
(define *q-sign* 1)    ; +1 = positive charge,  -1 = negative charge
(define *R*     0.9)   ; sphere radius (world units)
(define *r-gauss* 1.5) ; Gaussian surface radius (world units)
(define *show-charge* #t)
(define *show-E*      #t)
(define *show-gauss*  #t)

;;; ════════════════════════════════════════════════════════════════════════════
;;; FIELD EQUATION
;;; ════════════════════════════════════════════════════════════════════════════
;;;
;;; Returns (Ex . Ey) at world point (wx, wy).
;;; rho = signed charge density (ρ·q-sign).
;;; Derivation: ∮E·dA = Q_enclosed/ε₀ on a sphere of radius r.
;;;   Left:  E_r × 4πr²
;;;   Right: ρ × (4π/3)×min(r,R)³ / ε₀
;;; Solving for E_r gives the two-region formula below.

(define (E-at wx wy rho R)
  (let* ((r2  (+ (* wx wx) (* wy wy)))
         (r   (sqrt r2)))
    (if (< r 0.05)
        (cons 0.0 0.0)
        (let* ((er (if (< r R)
                       (/ (* rho r) (* 3.0 eps0))           ; inside: ρr/(3ε₀)
                       (/ (* rho R R R) (* 3.0 eps0 r2)))))  ; outside: ρR³/(3ε₀r²)
          (cons (* er (/ wx r))
                (* er (/ wy r)))))))

;;; ════════════════════════════════════════════════════════════════════════════
;;; UTILITIES
;;; ════════════════════════════════════════════════════════════════════════════

(define (fmt2 v) (number->string (/ (round (* v 100.0))  100.0)))
(define (fmt3 v) (number->string (/ (round (* v 1000.0)) 1000.0)))

;;; Arrow helper (same as other Maxwell demos).
(define (arrow! p x1 y1 x2 y2 r g b a lw)
  (gfx-set-pen-color! p r g b a)
  (gfx-set-pen-width! p lw)
  (gfx-draw-line! p x1 y1 x2 y2)
  (let* ((dx (- x2 x1)) (dy (- y2 y1))
         (len (sqrt (+ (* dx dx) (* dy dy)))))
    (when (> len 5.0)
      (let* ((ux (/ dx len)) (uy (/ dy len))
             (px (- uy))     (py ux)
             (hs (min 5.0 (* len 0.26)))
             (hl (min 9.0 (* len 0.44)))
             (bx (- x2 (* ux hl))) (by (- y2 (* uy hl))))
        (gfx-set-color! p r g b a)
        (gfx-fill-polygon! p
          (list (cons x2 y2)
                (cons (+ bx (* px hs)) (+ by (* py hs)))
                (cons (- bx (* px hs)) (- by (* py hs)))))))))

;;; ════════════════════════════════════════════════════════════════════════════
;;; DRAW CALLBACK
;;; ════════════════════════════════════════════════════════════════════════════
;;;
;;; Visual elements:
;;;   1. Grid + axes
;;;   2. Charge region — red (positive) or blue (negative) disk
;;;   3. E field arrows — green, radial (outward for +, inward for -)
;;;      Inside:  |E| ∝ r  (arrows grow toward boundary)
;;;      Outside: |E| ∝ 1/r²  (arrows shrink rapidly)
;;;   4. Gaussian surface — white circle; ∮E·dA = Q_enclosed/ε₀ readout
;;;   5. Live values panel
;;;   6. Saturation hint when Gaussian surface crosses sphere boundary

(define nx 20) (define ny 16)
(define xw 3.1) (define yw 2.5)

(define (draw painter w h)
  (let* ((rho  (* *rho* *q-sign*))
         (cx   (/ w 2.0)) (cy (/ h 2.0))
         (sc   (/ (min w h) 5.8))
         (Rpx  (* *R* sc))
         (Gpx  (* *r-gauss* sc))
         (gdx  (/ (* 2.0 xw) (- nx 1)))
         (gdy  (/ (* 2.0 yw) (- ny 1)))
         (amax (* (min gdx gdy) 0.40)))

    (gfx-clear! painter 0.06 0.06 0.10)
    (gfx-set-antialias! painter #t)

    ;; ── 1. Grid ───────────────────────────────────────────────────────────
    (gfx-set-pen-color! painter 0.13 0.13 0.20 1.0)
    (gfx-set-pen-width! painter 0.5)
    (let lp ((i -4)) (when (<= i 4)
      (gfx-draw-line! painter (+ cx (* i sc)) (- cy (* yw sc))
                              (+ cx (* i sc)) (+ cy (* yw sc)))
      (lp (+ i 1))))
    (let lp ((j -3)) (when (<= j 3)
      (gfx-draw-line! painter (- cx (* xw sc)) (+ cy (* j sc))
                              (+ cx (* xw sc)) (+ cy (* j sc)))
      (lp (+ j 1))))

    ;; ── 2. Axes ───────────────────────────────────────────────────────────
    (gfx-set-pen-color! painter 0.28 0.28 0.35 0.9)
    (gfx-set-pen-width! painter 1.0)
    (gfx-draw-line! painter (- cx (* xw sc)) cy (+ cx (* xw sc)) cy)
    (gfx-draw-line! painter cx (- cy (* yw sc)) cx (+ cy (* yw sc)))
    (gfx-set-color! painter 0.32 0.32 0.40 0.75)
    (gfx-set-font!  painter "sans-serif" 9)
    (gfx-draw-text! painter (+ cx (* xw sc) 3) (+ cy 4) "x")
    (gfx-draw-text! painter (+ cx 4) (- cy (* yw sc) 8) "y")

    ;; ── 3. Charge region ─────────────────────────────────────────────────
    ;;
    ;; Red = positive charge (E radiates outward).
    ;; Blue = negative charge (E converges inward).
    ;; The colour reminds you of the sign convention:
    ;;   positive charge is the SOURCE of E (divergence > 0)
    ;;   negative charge is the SINK of E (divergence < 0)
    (when *show-charge*
      (let* ((pos (> rho 0.0))
             (r-col (if pos 0.85 0.10))
             (g-col 0.08)
             (b-col (if pos 0.10 0.82)))
        (gfx-set-color! painter r-col g-col b-col 0.38)
        (gfx-fill-circle! painter cx cy Rpx)
        (gfx-set-pen-color! painter (+ r-col 0.10) g-col (+ b-col 0.10) 0.85)
        (gfx-set-pen-width! painter 2.0)
        (gfx-draw-circle! painter cx cy Rpx)
        (gfx-set-color! painter (+ r-col 0.20) (+ g-col 0.20) (+ b-col 0.25) 0.90)
        (gfx-set-font!  painter "sans-serif" 18)
        (gfx-draw-text! painter (- cx 8) (+ cy 7) (if pos "+" "−"))
        (gfx-set-font!  painter "monospace" 9)
        (gfx-draw-text! painter (- cx 32) (+ cy 24)
          (string-append "ρ = " (fmt2 rho) " C/m³"))))

    ;; ── 4. E field arrows (green, radial) ────────────────────────────────
    ;;
    ;; For positive charge: arrows point outward from the sphere.
    ;; For negative charge: arrows point inward toward the sphere.
    ;;
    ;; Radial pattern:
    ;;   Inside (r < R):  |E| ∝ r — arrows grow toward the surface.
    ;;   Outside (r > R): |E| ∝ 1/r² — arrows shrink quickly.
    ;; This is identical in structure to the B arrow patterns in the Ampere demo,
    ;; but now the arrows are purely radial, not azimuthal.
    (when *show-E*
      (let* ((emax 1e-10))
        (let lp-i ((i 0)) (when (< i nx)
          (let lp-j ((j 0)) (when (< j ny)
            (let* ((wx (+ (- xw) (* i gdx)))
                   (wy (+ (- yw) (* j gdy)))
                   (ev (E-at wx wy rho *R*))
                   (em (sqrt (+ (* (car ev) (car ev)) (* (cdr ev) (cdr ev))))))
              (when (> em emax) (set! emax em)))
            (lp-j (+ j 1))))
          (lp-i (+ i 1))))
        (let lp-i ((i 0)) (when (< i nx)
          (let lp-j ((j 0)) (when (< j ny)
            (let* ((wx  (+ (- xw) (* i gdx)))
                   (wy  (+ (- yw) (* j gdy)))
                   (ev  (E-at wx wy rho *R*))
                   (ex  (car ev)) (ey (cdr ev))
                   (em  (sqrt (+ (* ex ex) (* ey ey)))))
              (when (> em (* emax 0.025))
                (let* ((norm (/ em emax))
                       (len  (* amax norm sc))
                       (ux   (/ ex em))
                       (uy   (- (/ ey em)))   ; screen y flipped
                       (sx   (+ cx (* wx sc)))
                       (sy   (- cy (* wy sc)))
                       (x1   (- sx (* ux len 0.5)))
                       (y1   (- sy (* uy len 0.5)))
                       (x2   (+ sx (* ux len 0.5)))
                       (y2   (+ sy (* uy len 0.5)))
                       (alph (min 1.0 (* 0.92 (sqrt norm)))))
                  (arrow! painter x1 y1 x2 y2
                           0.28 0.96 0.38 alph 1.4))))
            (lp-j (+ j 1))))
          (lp-i (+ i 1))))))

    ;; ── 5. Gaussian surface ───────────────────────────────────────────────
    ;;
    ;; White circle at r_gauss.  The outward flux through it equals Q_enclosed/ε₀.
    ;;
    ;; ∮ E·dA = E_r(r_gauss) × 4π × r_gauss²
    ;;        = Q_enclosed / ε₀
    ;;
    ;; With r_eff = min(r_gauss, R):
    ;;   Q_enclosed = ρ × (4π/3) × r_eff³
    ;;   ∮ E·dA     = ρ × (4π/3) × r_eff³ / ε₀
    ;;
    ;; TRY: drag r_gauss slider.  The flux grows as r_gauss³ while inside the sphere.
    ;; Once r_gauss > R, the flux saturates — you've enclosed all the charge.
    (when *show-gauss*
      (gfx-set-pen-color! painter 0.88 0.88 1.0 0.60)
      (gfx-set-pen-width! painter 1.8)
      (gfx-draw-circle! painter cx cy Gpx)
      (let* ((reff (min *r-gauss* *R*))
             (flux (/ (* (/ (* 4.0 pi) 3.0) reff reff reff rho) eps0)))
        (gfx-set-color! painter 0.88 0.88 1.0 0.82)
        (gfx-set-font!  painter "monospace" 10)
        (gfx-draw-text! painter (+ cx Gpx 6) (- cy 6)
          (string-append "∮E·dA = " (fmt2 flux))))
      (gfx-set-color! painter 0.60 0.60 0.80 0.60)
      (gfx-set-font!  painter "sans-serif" 9)
      (gfx-draw-text! painter (+ cx Gpx 3) (+ cy 5) "rg"))

    ;; Sphere radius label
    (gfx-set-color! painter 0.55 0.55 0.65 0.65)
    (gfx-set-font!  painter "sans-serif" 9)
    (gfx-draw-text! painter (+ cx Rpx 4) (- cy 4) "R")

    ;; ── 6. Live values panel ──────────────────────────────────────────────
    (let* ((px 12.0) (py 12.0) (lh 18) (pw 278)
           (reff     (min *r-gauss* *R*))
           (q-enc    (/ (* (/ (* 4.0 pi) 3.0) reff reff reff rho) 1.0))
           (flux     (/ q-enc eps0))
           (er-at-rg (cdr (E-at *r-gauss* 0.0 rho *R*))))
      (gfx-set-color! painter 0.02 0.02 0.04 0.70)
      (gfx-fill-rect! painter px py pw (* 6.8 lh))
      (gfx-set-font!  painter "monospace" 11)
      ;; ρ — orange (the cause)
      (let ((c (if (>= rho 0.0) '(0.95 0.40 0.15) '(0.30 0.50 1.0))))
        (gfx-set-color! painter (car c) (cadr c) (caddr c) 0.95))
      (gfx-draw-text! painter (+ px 8) (+ py lh)
        (string-append "ρ        = " (fmt2 rho) " C/m³"
          (if (>= rho 0.0) "  (+)" "  (-)")))
      ;; Q_enclosed — yellow
      (gfx-set-color! painter 0.96 0.96 0.40 0.95)
      (gfx-draw-text! painter (+ px 8) (+ py (* 2 lh))
        (string-append "Q_enc    = " (fmt2 q-enc) " C"))
      ;; ∮E·dA — green (the measured flux)
      (gfx-set-color! painter 0.28 0.96 0.38 0.95)
      (gfx-draw-text! painter (+ px 8) (+ py (* 3 lh))
        (string-append "∮E·dA    = " (fmt2 flux) " V·m"))
      ;; E_r at the surface
      (gfx-set-color! painter 0.75 0.75 0.75 0.95)
      (gfx-draw-text! painter (+ px 8) (+ py (* 4 lh))
        (string-append "E_r(rg)  = " (fmt3 er-at-rg) " V/m"))
      ;; ∇·E inside
      (gfx-set-color! painter 0.68 0.68 1.00 0.95)
      (gfx-draw-text! painter (+ px 8) (+ py (* 5 lh))
        (string-append "(∇·E)    = " (fmt2 (/ rho eps0)) " C/m³·ε₀"))
      (gfx-set-color! painter 0.50 0.50 0.55 0.80)
      (gfx-draw-text! painter (+ px 8) (+ py (* 6.3 lh))
        "∇·E = ρ/ε₀  ✓"))

    ;; ── 7. Hint ───────────────────────────────────────────────────────────
    (let* ((hint (cond
                   ((< *r-gauss* (* *R* 0.98))
                    "Gaussian surface INSIDE sphere — ∮E·dA ∝ r³ (growing)")
                   ((> *r-gauss* (* *R* 1.02))
                    "Gaussian surface OUTSIDE sphere — ∮E·dA fixed = Q_total/ε₀")
                   (else "Gaussian surface at sphere boundary"))))
      (gfx-set-color!  painter 1.0 0.90 0.40 0.88)
      (gfx-set-font!   painter "sans-serif" 12 #t)
      (gfx-draw-text!  painter (- (/ w 2.0) 210.0) (- h 18.0) hint))))

;;; ════════════════════════════════════════════════════════════════════════════
;;; WINDOW AND SIDEBAR
;;; ════════════════════════════════════════════════════════════════════════════

(define *win*    (make-window "Gauss's Law for E  (∇·E = ρ/ε₀)" 1040 680))
(define *canvas* (window-canvas *win*))
(define *sb*     (window-sidebar *win*))

(box-add! *sb* (make-label "GAUSS'S LAW FOR E"))
(box-add! *sb* (make-label ""))
(box-add! *sb* (make-label "    ∇ · E  =  ρ / ε₀"))
(box-add! *sb* (make-label ""))
(box-add! *sb* (make-label "▸ E field lines START on +"))
(box-add! *sb* (make-label "  charges and END on − charges."))
(box-add! *sb* (make-label ""))
(box-add! *sb* (make-label "▸ ∮E·dA = Q_enclosed/ε₀"))
(box-add! *sb* (make-label "  Flux depends ONLY on charge"))
(box-add! *sb* (make-label "  inside — not on surface shape."))
(box-add! *sb* (make-label ""))
(box-add! *sb* (make-label "▸ Outside the sphere the field"))
(box-add! *sb* (make-label "  looks like a point charge."))
(box-add! *sb* (make-separator))

;; Charge sign toggle
(box-add! *sb* (make-label "── Charge ──"))
(box-add! *sb*
  (make-toggle "Positive charge  (+)"
    (= *q-sign* 1)
    (lambda (on)
      (set! *q-sign* (if on 1 -1))
      (canvas-redraw! *canvas*))))
(box-add! *sb* (make-label "(off = negative charge)"))
(box-add! *sb* (make-label ""))
(box-add! *sb* (make-label "Charge density  ρ  (C/m³)"))
(box-add! *sb*
  (make-slider "rho" 0.2 2.0 0.05 *rho*
    (lambda (v) (set! *rho* v) (canvas-redraw! *canvas*))))
(box-add! *sb* (make-label "Sphere radius  R"))
(box-add! *sb*
  (make-slider "R" 0.3 1.6 0.05 *R*
    (lambda (v) (set! *R* v) (canvas-redraw! *canvas*))))
(box-add! *sb* (make-separator))

(box-add! *sb* (make-label "── Gaussian surface ──"))
(box-add! *sb* (make-label "Radius  rg"))
(box-add! *sb*
  (make-slider "rg" 0.2 2.8 0.05 *r-gauss*
    (lambda (v) (set! *r-gauss* v) (canvas-redraw! *canvas*))))
(box-add! *sb* (make-label "Drag rg across R — flux"))
(box-add! *sb* (make-label "saturates once rg > R."))
(box-add! *sb* (make-separator))

(box-add! *sb* (make-label "── Display ──"))
(box-add! *sb*
  (make-toggle "Charge region" *show-charge*
    (lambda (on) (set! *show-charge* on) (canvas-redraw! *canvas*))))
(box-add! *sb*
  (make-toggle "E field arrows" *show-E*
    (lambda (on) (set! *show-E* on) (canvas-redraw! *canvas*))))
(box-add! *sb*
  (make-toggle "Gaussian surface" *show-gauss*
    (lambda (on) (set! *show-gauss* on) (canvas-redraw! *canvas*))))
(box-add! *sb* (make-separator))

(box-add! *sb* (make-label "── CAS verification ──"))
(box-add! *sb* (make-label "Uniform sphere, ρ=ε₀=1:"))
(box-add! *sb* (make-label "E = (x/3, y/3, z/3)"))
(box-add! *sb* (make-label ""))
(box-add! *sb* (make-label
  (string-append "∇·E (inside) = "
    (let ((r div-E-inside))
      (if (and (integer? r) (= r 1)) "1 = ρ/ε₀  ✓" (sym->infix r))))))
(box-add! *sb* (make-label "∇·E (outside) = 0  ✓"))
(box-add! *sb* (make-label "(field lines pass through)"))
(box-add! *sb* (make-separator))

(box-add! *sb* (make-label "── Guided exercises ──"))
(box-add! *sb* (make-label "1. Drag rg from 0.3 to 2.8."))
(box-add! *sb* (make-label "   When does ∮E·dA stop growing?"))
(box-add! *sb* (make-label ""))
(box-add! *sb* (make-label "2. Toggle charge sign. Do the"))
(box-add! *sb* (make-label "   arrows flip? Does |flux| change?"))
(box-add! *sb* (make-label ""))
(box-add! *sb* (make-label "3. Double R with rg fixed outside."))
(box-add! *sb* (make-label "   Q_enc grows as R³ — why?"))
(box-add! *sb* (make-label ""))
(box-add! *sb* (make-label "4. Double ρ vs double R."))
(box-add! *sb* (make-label "   Which has more effect on E_r"))
(box-add! *sb* (make-label "   at the boundary r = R?"))
(box-add! *sb* (make-separator))

(box-add! *sb*
  (make-button "✕  Quit  [q]" quit-event-loop))

;;; ════════════════════════════════════════════════════════════════════════════
;;; EVENT LOOP
;;; ════════════════════════════════════════════════════════════════════════════

(canvas-on-draw! *canvas* draw)

(window-on-key! *win*
  (lambda (key mods)
    (cond
      ((equal? key "q")      (quit-event-loop))
      ((equal? key "Escape") (quit-event-loop)))))

(statusbar-set-text! (window-status-bar *win*)
  "Drag sliders to explore  ·  q = quit")

(window-on-close! *win* quit-event-loop)
(window-show! *win*)
(run-event-loop)
