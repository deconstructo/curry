;;; gauss-b-explorer.scm — v1.0
;;;
;;; Interactive workbook for Gauss's Law for the Magnetic Field.
;;; Fourth and final in the Maxwell series.
;;;
;;; ════════════════════════════════════════════════════════════════════════════
;;; BACKGROUND
;;; ════════════════════════════════════════════════════════════════════════════
;;;
;;; Maxwell's second equation (often called the "no-monopole" law):
;;;
;;;   ∇ · B  =  0
;;;
;;; Reading it:
;;;   The divergence of B is zero everywhere, always.
;;;   There are no sources or sinks of B field lines.
;;;   Every field line that enters a region must also exit it.
;;;   B field lines form closed loops — they never start or end.
;;;
;;; CONTRAST WITH GAUSS'S LAW FOR E:
;;;   ∇ · E  =  ρ / ε₀  — E field lines start on + charges, end on − charges.
;;;   ∇ · B  =  0        — B field lines have no sources or sinks, ever.
;;;
;;; In plain English:
;;;   "There is no such thing as a magnetic charge (monopole).
;;;    A north pole always comes paired with a south pole.
;;;    Cut a magnet in half: you get two smaller magnets, each with N and S.
;;;    Cut it in half again: two more magnets.  You can never isolate a monopole."
;;;
;;; Integral form:
;;;   ∮_S  B · dA  =  0   (for any closed surface S)
;;;
;;; THE DEMO — TWO-POLE MAGNET CROSS-SECTION:
;;;   This shows a 2D magnetic dipole: a "north pole" (red dot, top) and
;;;   "south pole" (blue dot, bottom) separated by 2d.
;;;   The cyan arrows are the magnetic field B circling between the poles.
;;;   The white circle is a Gaussian surface you can position with the slider.
;;;
;;;   KEY EXPERIMENT:
;;;   1. Default: Gaussian surface encloses BOTH poles → ∮B·dl ≈ 0
;;;      (north and south contributions cancel exactly)
;;;   2. Move gy up to enclose ONLY the north "pole" → ∮B·dl ≠ 0
;;;      THIS IS WHAT A MONOPOLE WOULD LOOK LIKE.
;;;      But no such isolated pole has EVER been observed in nature.
;;;      Gauss's law for B (∇·B = 0) is an experimental fact.
;;;
;;; THE MATHEMATICAL REASON ∇·B = 0:
;;;   In physics, B always arises from currents via Ampere's law, which means
;;;   B can always be written as B = ∇ × A (the vector potential A).
;;;   And for any vector field A:
;;;     ∇ · (∇ × A)  =  0   (identically, by calculus)
;;;   So B = ∇×A automatically satisfies ∇·B = 0.
;;;   The CAS below proves this identity symbolically.
;;;
;;; THE CABRERA EXPERIMENT (1982):
;;;   A Stanford physicist observed a single event consistent with a magnetic
;;;   monopole passing through a superconducting loop.  It was never repeated.
;;;   To this day, no confirmed monopole has been found.  ∇·B = 0 stands.
;;;
;;; For the student guide see: docs/gauss-b-explorer.md
;;; Run: ./build/curry examples/gauss-b-explorer.scm
;;; ════════════════════════════════════════════════════════════════════════════

(import (curry qt6))
(import (scheme base))
(import (scheme inexact))
(import (scheme write))

;;; ════════════════════════════════════════════════════════════════════════════
;;; SYMBOLIC CAS — PROVING ∇·(∇×A) = 0
;;; ════════════════════════════════════════════════════════════════════════════
;;;
;;; Since B = ∇×A always in electromagnetism, ∇·B = 0 follows from the
;;; mathematical identity ∇·(∇×A) = 0 (the divergence of any curl is zero).
;;;
;;; We verify this symbolically for A = (0, xy, xyz):
;;;   B = ∇×A:
;;;     Bx = ∂(xyz)/∂y − ∂(xy)/∂z  =  xz − 0  =  xz
;;;     By = ∂(0)/∂z  − ∂(xyz)/∂x  =   0 − yz  = −yz
;;;     Bz = ∂(xy)/∂x − ∂(0)/∂y   =   y − 0   =   y
;;;
;;;   ∇·B = ∂(xz)/∂x + ∂(−yz)/∂y + ∂(y)/∂z
;;;        =    z     +    (−z)    +    0     =  0  ✓
;;;
;;; Note: ∂(xz)/∂x = z and ∂(−yz)/∂y = −z cancel non-trivially — the CAS
;;; must track both terms and only then discover they sum to zero.

(symbolic x y z)

(define Ay (* x y))
(define Az (* x y z))

(define Bx (simplify (- (∂ Az y) (∂ Ay z))))    ; xz
(define By (simplify (- 0 (∂ Az x))))           ; −yz
(define Bz (simplify (∂ Ay x)))                 ; y

(define div-B
  (simplify (+ (∂ Bx x) (∂ By y) (∂ Bz z))))   ; 0  ✓

;;; ════════════════════════════════════════════════════════════════════════════
;;; STATE
;;; ════════════════════════════════════════════════════════════════════════════

(define pi 3.14159265358979323846)

(define *m*       1.0)   ; pole strength
(define *d*       0.70)  ; half-separation: poles at (0, ±d)
(define *gy*      0.0)   ; Gaussian surface y-offset
(define *r-gauss* 1.0)   ; Gaussian surface radius
(define *show-poles* #t)
(define *show-B*     #t)
(define *show-gauss* #t)

;;; ════════════════════════════════════════════════════════════════════════════
;;; FIELD: 2D MAGNETIC DIPOLE
;;; ════════════════════════════════════════════════════════════════════════════
;;;
;;; We model the field using two 2D "pseudo-monopoles":
;;;   North (+m) at (0, +d): B ∝ r̂/r (outward in 2D)
;;;   South (−m) at (0, −d): B ∝ −r̂/r (inward in 2D)
;;;
;;; The 2D monopole field B = m×r̂/r² has ∇·B = 0 everywhere except at the
;;; pole location (where it is a delta function, representing the monopole).
;;;
;;; 2D Gauss theorem: ∮ B·n̂ dl = ∫∫ ∇·B dA
;;;   = 0          if neither or both poles are inside the loop
;;;   = +2πm       if the north pole is inside (this is what a monopole gives)
;;;   = −2πm       if the south pole is inside
;;;
;;; Real magnets: the field lines must close THROUGH the magnet interior,
;;; meaning you can never have a surface that encloses only one pole of
;;; a real magnet — the return path is always there.  The pseudo-monopole
;;; simulation lets you see what an isolated monopole WOULD look like.

(define (B-at wx wy)
  (let* ((d    *d*)
         (m    *m*)
         ;; Displacement to north pole at (0, +d)
         (dx-n wx) (dy-n (- wy d))
         (r2-n (max 0.0001 (+ (* dx-n dx-n) (* dy-n dy-n))))
         ;; Displacement to south pole at (0, −d)
         (dx-s wx) (dy-s (+ wy d))
         (r2-s (max 0.0001 (+ (* dx-s dx-s) (* dy-s dy-s))))
         ;; B from north (+m): m × (Δx, Δy)/r²
         (bx-n (* m (/ dx-n r2-n)))
         (by-n (* m (/ dy-n r2-n)))
         ;; B from south (−m): −m × (Δx, Δy)/r²
         (bx-s (* (- m) (/ dx-s r2-s)))
         (by-s (* (- m) (/ dy-s r2-s))))
    (cons (+ bx-n bx-s) (+ by-n by-s))))

;;; ════════════════════════════════════════════════════════════════════════════
;;; GAUSSIAN FLUX: ∮ B·n̂ dl AROUND THE WHITE CIRCLE
;;; ════════════════════════════════════════════════════════════════════════════
;;;
;;; Numerically integrates the outward flux of B around a circle at (0, gy)
;;; with radius r-gauss.  By the 2D divergence theorem this equals ∫∫ ∇·B dA:
;;;   = 0      if no poles inside, or both cancel
;;;   = ±2πm   if exactly one pole inside (the monopole signal)

(define (gaussian-flux)
  (let* ((N     360)
         (dt    (/ (* 2.0 pi) N))
         (total 0.0))
    (let loop ((i 0))
      (when (< i N)
        (let* ((theta (* i dt))
               (nx  (cos theta))
               (ny  (sin theta))
               (wx  (* *r-gauss* nx))
               (wy  (+ *gy* (* *r-gauss* ny)))
               (bv  (B-at wx wy))
               (bn  (+ (* (car bv) nx) (* (cdr bv) ny))))
          (set! total (+ total (* bn *r-gauss* dt))))
        (loop (+ i 1))))
    total))

;;; Determine which poles are inside the Gaussian surface.
(define (north-inside?) (< (abs (- *gy* *d*))    *r-gauss*))
(define (south-inside?) (< (abs (+ *gy* *d*))    *r-gauss*))

;;; ════════════════════════════════════════════════════════════════════════════
;;; UTILITIES
;;; ════════════════════════════════════════════════════════════════════════════

(define (fmt2 v) (number->string (/ (round (* v 100.0))  100.0)))
(define (fmt3 v) (number->string (/ (round (* v 1000.0)) 1000.0)))

(define (arrow! p x1 y1 x2 y2 r g b a lw)
  (gfx-set-pen-color! p r g b a)
  (gfx-set-pen-width! p lw)
  (gfx-draw-line! p x1 y1 x2 y2)
  (let* ((dx (- x2 x1)) (dy (- y2 y1))
         (len (sqrt (+ (* dx dx) (* dy dy)))))
    (when (> len 5.0)
      (let* ((ux (/ dx len)) (uy (/ dy len))
             (px (- uy)) (py ux)
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
;;;   2. Pole markers — red N (top), blue S (bottom)
;;;   3. B field arrows — cyan, showing the dipole pattern
;;;   4. Gaussian surface — white circle at (0, gy)
;;;      Colour of ∮B·dl readout: white ≈ 0, red/blue ≠ 0 (monopole signal)
;;;   5. Live values panel
;;;   6. Hint describing which poles are inside the surface

(define nx 20) (define ny 16)
(define xw 3.1) (define yw 2.5)

(define (draw painter w h)
  (let* ((flux (gaussian-flux))
         (n-in (north-inside?))
         (s-in (south-inside?))
         (cx   (/ w 2.0)) (cy (/ h 2.0))
         (sc   (/ (min w h) 5.8))
         (Gpx  (* *r-gauss* sc))
         (Gcy  (- cy (* *gy* sc)))   ; Gaussian centre on screen (y flipped)
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

    ;; ── 3. B field arrows ─────────────────────────────────────────────────
    ;;
    ;; The field lines form the characteristic dipole pattern: arcing from N
    ;; to S in the exterior, with the return path through the "magnet interior".
    ;; Note that the arrows always form closed curves (confirming ∇·B = 0 in
    ;; the exterior) — no arrow ever points outward from a region with net flux.
    (when *show-B*
      (let* ((bmax 1e-10))
        (let lp-i ((i 0)) (when (< i nx)
          (let lp-j ((j 0)) (when (< j ny)
            (let* ((wx (+ (- xw) (* i gdx)))
                   (wy (+ (- yw) (* j gdy)))
                   (bv (B-at wx wy))
                   (bm (sqrt (+ (* (car bv) (car bv)) (* (cdr bv) (cdr bv))))))
              (when (> bm bmax) (set! bmax bm)))
            (lp-j (+ j 1))))
          (lp-i (+ i 1))))
        (let lp-i ((i 0)) (when (< i nx)
          (let lp-j ((j 0)) (when (< j ny)
            (let* ((wx  (+ (- xw) (* i gdx)))
                   (wy  (+ (- yw) (* j gdy)))
                   (bv  (B-at wx wy))
                   (bx  (car bv)) (by (cdr bv))
                   (bm  (sqrt (+ (* bx bx) (* by by)))))
              (when (> bm (* bmax 0.015))
                (let* ((norm (/ bm bmax))
                       (len  (* amax norm sc))
                       (ux   (/ bx bm))
                       (uy   (- (/ by bm)))
                       (sx   (+ cx (* wx sc)))
                       (sy   (- cy (* wy sc)))
                       (x1   (- sx (* ux len 0.5)))
                       (y1   (- sy (* uy len 0.5)))
                       (x2   (+ sx (* ux len 0.5)))
                       (y2   (+ sy (* uy len 0.5)))
                       (alph (min 1.0 (* 0.85 (sqrt norm)))))
                  (arrow! painter x1 y1 x2 y2
                           0.20 0.82 1.00 alph 1.4))))
            (lp-j (+ j 1))))
          (lp-i (+ i 1))))))

    ;; ── 4. Pole markers ──────────────────────────────────────────────────
    (when *show-poles*
      (let* ((ny-screen (- cy (* *d* sc)))   ; north pole on screen
             (sy-screen (+ cy (* *d* sc)))   ; south pole on screen
             (pr        10.0))               ; pole dot radius in pixels
        ;; North (red)
        (gfx-set-color! painter 0.90 0.15 0.15 0.90)
        (gfx-fill-circle! painter cx ny-screen pr)
        (gfx-set-pen-color! painter 1.0 0.40 0.40 1.0)
        (gfx-set-pen-width! painter 1.5)
        (gfx-draw-circle! painter cx ny-screen pr)
        (gfx-set-color! painter 1.0 0.85 0.85 0.95)
        (gfx-set-font!  painter "sans-serif" 11 #t)
        (gfx-draw-text! painter (- cx 5) (- ny-screen 4) "N")
        ;; South (blue)
        (gfx-set-color! painter 0.10 0.30 0.90 0.90)
        (gfx-fill-circle! painter cx sy-screen pr)
        (gfx-set-pen-color! painter 0.35 0.55 1.0 1.0)
        (gfx-set-pen-width! painter 1.5)
        (gfx-draw-circle! painter cx sy-screen pr)
        (gfx-set-color! painter 0.80 0.85 1.0 0.95)
        (gfx-set-font!  painter "sans-serif" 11 #t)
        (gfx-draw-text! painter (- cx 5) (- sy-screen 4) "S")))

    ;; ── 5. Gaussian surface + flux readout ────────────────────────────────
    ;;
    ;; The flux colour codes the physical situation:
    ;;   White:  ∮B·dl ≈ 0 — no monopole enclosed (physical reality)
    ;;   Red:    ∮B·dl > 0 — north "monopole" enclosed (never seen in nature!)
    ;;   Blue:   ∮B·dl < 0 — south "monopole" enclosed (never seen in nature!)
    (when *show-gauss*
      ;; Circle colour
      (let* ((abs-flux (abs flux))
             (max-flux (* 2.0 pi *m*))
             (frac     (min 1.0 (/ abs-flux (* max-flux 0.3)))))
        (if (> abs-flux (* max-flux 0.1))
            (if (> flux 0.0)
                (gfx-set-pen-color! painter (+ 0.60 (* 0.40 frac)) 0.20 0.20 0.90)
                (gfx-set-pen-color! painter 0.20 0.30 (+ 0.60 (* 0.40 frac)) 0.90))
            (gfx-set-pen-color! painter 0.88 0.88 1.0 0.60)))
      (gfx-set-pen-width! painter 1.8)
      (gfx-draw-circle! painter cx Gcy Gpx)
      ;; Flux readout
      (let* ((flux-col
              (if (> (abs flux) (* pi *m* 0.2))
                  (if (> flux 0.0)
                      '(1.0 0.40 0.40 0.95)
                      '(0.40 0.55 1.0 0.95))
                  '(0.88 0.88 1.0 0.82))))
        (gfx-set-color! painter (car flux-col) (cadr flux-col)
                        (caddr flux-col) (cadddr flux-col))
        (gfx-set-font!  painter "monospace" 10)
        (gfx-draw-text! painter (+ cx Gpx 6) (- Gcy 6)
          (string-append "∮B·dl = " (fmt2 flux))))
      (gfx-set-color! painter 0.60 0.60 0.80 0.60)
      (gfx-set-font!  painter "sans-serif" 9)
      (gfx-draw-text! painter (+ cx Gpx 3) (+ Gcy 5) "rg"))

    ;; ── 6. Live values panel ──────────────────────────────────────────────
    (let* ((px 12.0) (py 12.0) (lh 18) (pw 282))
      (gfx-set-color! painter 0.02 0.02 0.04 0.70)
      (gfx-fill-rect! painter px py pw (* 6.8 lh))
      (gfx-set-font!  painter "monospace" 11)
      (gfx-set-color! painter 0.75 0.75 0.75 0.95)
      (gfx-draw-text! painter (+ px 8) (+ py lh)
        (string-append "m (pole str)  = " (fmt2 *m*)))
      (gfx-draw-text! painter (+ px 8) (+ py (* 2 lh))
        (string-append "d (half-sep)  = " (fmt2 *d*)))
      ;; Poles inside
      (let ((both (and n-in s-in)) (neither (not (or n-in s-in))))
        (gfx-set-color! painter
          (if (and n-in (not s-in)) 1.0 0.75)
          (if both 0.85 0.45)
          (if (and s-in (not n-in)) 1.0 0.75) 0.95)
        (gfx-draw-text! painter (+ px 8) (+ py (* 3 lh))
          (cond (both    "poles inside: N + S  (cancels)")
                (n-in    "poles inside: N only  ← monopole!")
                (s-in    "poles inside: S only  ← monopole!")
                (else    "poles inside: none"))))
      ;; Flux
      (let* ((abs-flux (abs flux))
             (max-flux (* 2.0 pi *m*)))
        (if (> abs-flux (* max-flux 0.1))
            (gfx-set-color! painter 1.0 0.50 0.50 0.95)
            (gfx-set-color! painter 0.28 0.96 0.38 0.95))
        (gfx-draw-text! painter (+ px 8) (+ py (* 4 lh))
          (string-append "∮B·dl         = " (fmt2 flux))))
      (gfx-set-color! painter 0.68 0.68 1.00 0.95)
      (gfx-draw-text! painter (+ px 8) (+ py (* 5 lh))
        (string-append "2π·m (ref)    = " (fmt2 (* 2.0 pi *m*))))
      (gfx-set-color! painter 0.50 0.50 0.55 0.80)
      (gfx-draw-text! painter (+ px 8) (+ py (* 6.3 lh))
        "∇·B = 0  (no monopoles)  ✓"))

    ;; ── 7. Hint ───────────────────────────────────────────────────────────
    (let* ((max-flux (* 2.0 pi *m*))
           (hint
            (cond
              ((and n-in s-in)
               "Both poles enclosed — ∮B·dl = 0  (N and S always come together)")
              ((and n-in (not s-in))
               "North 'pole' enclosed — ∮B·dl ≠ 0 — THIS is what a monopole looks like")
              ((and s-in (not n-in))
               "South 'pole' enclosed — ∮B·dl ≠ 0 — Never observed in any real magnet")
              (else
               "Neither pole enclosed — ∮B·dl ≈ 0  (field lines cancel through surface)"))))
      (let ((c (if (and (or n-in s-in) (not (and n-in s-in)))
                   '(1.0 0.55 0.30 0.92)
                   '(1.0 0.90 0.40 0.88))))
        (gfx-set-color!  painter (car c) (cadr c) (caddr c) (cadddr c)))
      (gfx-set-font!   painter "sans-serif" 12 #t)
      (gfx-draw-text!  painter (- (/ w 2.0) 230.0) (- h 18.0) hint))))

;;; ════════════════════════════════════════════════════════════════════════════
;;; WINDOW AND SIDEBAR
;;; ════════════════════════════════════════════════════════════════════════════

(define *win*    (make-window "Gauss's Law for B  (∇·B = 0)" 1040 680))
(define *canvas* (window-canvas *win*))
(define *sb*     (window-sidebar *win*))

(box-add! *sb* (make-label "GAUSS'S LAW FOR B"))
(box-add! *sb* (make-label ""))
(box-add! *sb* (make-label "         ∇ · B  =  0"))
(box-add! *sb* (make-label ""))
(box-add! *sb* (make-label "▸ B field lines form CLOSED LOOPS."))
(box-add! *sb* (make-label "  No sources, no sinks."))
(box-add! *sb* (make-label ""))
(box-add! *sb* (make-label "▸ ∮B·dA = 0 for any surface."))
(box-add! *sb* (make-label "  Even around one pole of a magnet."))
(box-add! *sb* (make-label ""))
(box-add! *sb* (make-label "▸ No magnetic monopole has"))
(box-add! *sb* (make-label "  ever been found."))
(box-add! *sb* (make-separator))

(box-add! *sb* (make-label "── Dipole ──"))
(box-add! *sb* (make-label "Pole strength  m"))
(box-add! *sb*
  (make-slider "m" 0.2 2.0 0.05 *m*
    (lambda (v) (set! *m* v) (canvas-redraw! *canvas*))))
(box-add! *sb* (make-label "Pole separation  2d"))
(box-add! *sb*
  (make-slider "d" 0.2 1.4 0.05 *d*
    (lambda (v) (set! *d* v) (canvas-redraw! *canvas*))))
(box-add! *sb* (make-label "Shrink d → still a dipole."))
(box-add! *sb* (make-label "You can't isolate one pole!"))
(box-add! *sb* (make-separator))

(box-add! *sb* (make-label "── Gaussian surface ──"))
(box-add! *sb* (make-label "Centre y-offset  gy"))
(box-add! *sb*
  (make-slider "gy" -2.0 2.0 0.05 *gy*
    (lambda (v) (set! *gy* v) (canvas-redraw! *canvas*))))
(box-add! *sb* (make-label "Radius  rg"))
(box-add! *sb*
  (make-slider "rg" 0.2 2.5 0.05 *r-gauss*
    (lambda (v) (set! *r-gauss* v) (canvas-redraw! *canvas*))))
(box-add! *sb* (make-label "Move gy to enclose ONE pole"))
(box-add! *sb* (make-label "and see ∮B·dl ≠ 0."))
(box-add! *sb* (make-separator))

(box-add! *sb* (make-label "── Display ──"))
(box-add! *sb*
  (make-toggle "Pole markers  N/S" *show-poles*
    (lambda (on) (set! *show-poles* on) (canvas-redraw! *canvas*))))
(box-add! *sb*
  (make-toggle "B field arrows" *show-B*
    (lambda (on) (set! *show-B* on) (canvas-redraw! *canvas*))))
(box-add! *sb*
  (make-toggle "Gaussian surface" *show-gauss*
    (lambda (on) (set! *show-gauss* on) (canvas-redraw! *canvas*))))
(box-add! *sb* (make-separator))

;;; ── CAS: ∇·(∇×A) = 0 ─────────────────────────────────────────────────────

(box-add! *sb* (make-label "── CAS: ∇·(∇×A) = 0 ──"))
(box-add! *sb* (make-label "A = (0, xy, xyz)"))
(box-add! *sb* (make-label "B = ∇×A:"))
(box-add! *sb* (make-label (string-append "  Bx = " (sym->infix Bx))))
(box-add! *sb* (make-label (string-append "  By = " (sym->infix By))))
(box-add! *sb* (make-label (string-append "  Bz = " (sym->infix Bz))))
(box-add! *sb* (make-label ""))
(box-add! *sb* (make-label
  (string-append "∇·B = "
    (let ((r div-B))
      (if (and (integer? r) (zero? r)) "0  ✓  (for all A)" (sym->infix r))))))
(box-add! *sb* (make-label "z and −z cancel: non-trivial!"))
(box-add! *sb* (make-separator))

(box-add! *sb* (make-label "── Guided exercises ──"))
(box-add! *sb* (make-label "1. Set gy=0, rg=1. Both poles"))
(box-add! *sb* (make-label "   inside. ∮B·dl = ?"))
(box-add! *sb* (make-label ""))
(box-add! *sb* (make-label "2. Set gy=d, rg=0.4. North only."))
(box-add! *sb* (make-label "   ∮B·dl ≈ 2πm? (Check ref row.)"))
(box-add! *sb* (make-label ""))
(box-add! *sb* (make-label "3. Shrink d to 0.2. Does the"))
(box-add! *sb* (make-label "   'monopole flux' change?"))
(box-add! *sb* (make-label ""))
(box-add! *sb* (make-label "4. Compare ∮E·dA from the"))
(box-add! *sb* (make-label "   Gauss-E demo with ∮B·dl here."))
(box-add! *sb* (make-label "   What is different, and why?"))
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
  "Drag gy to enclose one pole — see what a monopole would look like  ·  q = quit")

(window-on-close! *win* quit-event-loop)
(window-show! *win*)
(run-event-loop)
