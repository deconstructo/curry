;;; faraday-explorer.scm  — v1.1
;;;
;;; Interactive workbook for Faraday's Law of Electromagnetic Induction.
;;;
;;; ════════════════════════════════════════════════════════════════════════════
;;; BACKGROUND — READ THIS FIRST (especially if you're a student!)
;;; ════════════════════════════════════════════════════════════════════════════
;;;
;;; Maxwell's four equations describe all of classical electromagnetism.
;;; Faraday's Law is the third of the four:
;;;
;;;   ∇ × E = −∂B/∂t
;;;
;;; Reading it piece by piece:
;;;
;;;   ∇ × E     — the "curl" of the electric field E.
;;;               Curl measures how much a vector field circulates around
;;;               a point.  If you placed a tiny paddle wheel in the field,
;;;               curl tells you how fast it spins and in which direction.
;;;               Curl = 0 means no circulation; curl ≠ 0 means the field
;;;               goes around in loops near that point.
;;;
;;;   = −∂B/∂t  — equals minus the time-rate-of-change of the magnetic field B.
;;;
;;; In plain English:
;;;   "Wherever a magnetic field is changing in time, an electric field
;;;    curls around it."
;;;   Or: a time-varying B creates a circulating E.
;;;
;;; THE KEY INSIGHT THIS DEMO TRIES TO TEACH:
;;;   It is the RATE OF CHANGE of B (∂B/∂t), not B itself, that drives E.
;;;   Watch the green arrows: they vanish completely when B is at its maximum.
;;;   They are strongest when B is CROSSING ZERO — changing fastest.
;;;   Use the "→ +¼ period" button to step through this.
;;;
;;; THE MINUS SIGN — LENZ'S LAW:
;;;   The minus sign is not bookkeeping.  It encodes one of the most
;;;   important ideas in electromagnetism: the induced E always opposes
;;;   the change that caused it.
;;;   • B increasing out of page → induced E circulates clockwise
;;;     (a current flowing with E would create B pointing INTO the page,
;;;      opposing the increase).
;;;   • B decreasing → induced E circulates counterclockwise.
;;;   This opposition is why motors resist being driven and generators resist
;;;   being turned.  It is the root of inductance.
;;;
;;; THE SETUP — WHAT THIS SIMULATION SHOWS:
;;;   We look at the cross-section of a long cylindrical solenoid.
;;;   The coloured circle is that cross-section; its radius is R.
;;;   Inside the circle, B_z = B₀ cos(ωt) — a uniform field oscillating
;;;   sinusoidally in the z-direction (in/out of the screen).
;;;   Outside the circle, B = 0.
;;;
;;;   Because the geometry has perfect rotational symmetry, the induced E
;;;   can only point in the azimuthal (φ) direction and can only depend on r.
;;;   This lets us find the exact analytic solution using the integral form
;;;   of Faraday's Law (see the derivation at the E-at function below).
;;;
;;; REAL-WORLD CONNECTIONS:
;;;   This is exactly what happens inside a transformer (AC current in the
;;;   primary coil creates changing B; changing B induces E = voltage in the
;;;   secondary coil) and inside an AC generator (rotating magnets create
;;;   time-varying B that drives current in the stator coils).
;;;
;;; For a full student guide with derivations and exercises, see:
;;;   docs/faraday-explorer.md
;;;
;;; Controls:  [Space] pause · [→ / +¼] step quarter period · [r] reset · [q] quit
;;; Run:  ./build/curry examples/faraday-explorer.scm
;;; ════════════════════════════════════════════════════════════════════════════

(import (curry qt6))
(import (scheme base))
(import (scheme inexact))
(import (scheme write))

;;; ════════════════════════════════════════════════════════════════════════════
;;; SYMBOLIC CAS — PLANE-WAVE VERIFICATION
;;; ════════════════════════════════════════════════════════════════════════════
;;;
;;; The simulation uses the cylindrical solenoid solution, but we can also
;;; check Faraday's Law for a completely different field configuration:
;;; a plane electromagnetic wave travelling in the +x direction.
;;;
;;; For the wave  E_y = cos(x − t),  B_z = cos(x − t)  (with c = 1):
;;;
;;;   The z-component of  ∇ × E = −∂B/∂t  reads:
;;;     ∂E_y/∂x  −  ∂E_x/∂y  =  −∂B_z/∂t
;;;   Since E_x = 0, this simplifies to:
;;;     ∂E_y/∂x  =  −∂B_z/∂t
;;;
;;; The Curry symbolic CAS computes both sides and verifies they are equal.
;;; The result `faraday-cas = 0` means the residual (LHS − RHS) is zero — ✓
;;;
;;; This is also shown in the sidebar so you can read the actual expressions.

(symbolic x t)

(define dEy-dx      (simplify (∂ (cos (- x t)) x)))     ; ∂E_y/∂x  = −sin(x−t)
(define neg-dBz-dt  (simplify (- (∂ (cos (- x t)) t)))) ; −∂B_z/∂t = −sin(x−t) also
(define faraday-cas (simplify (- dEy-dx neg-dBz-dt)))    ; residual  = 0  ✓

;;; ════════════════════════════════════════════════════════════════════════════
;;; CONSTANTS AND WORLD STATE
;;; ════════════════════════════════════════════════════════════════════════════

(define pi 3.14159265358979323846)

;; All of these can be changed live via the sidebar sliders.
(define *B0*        1.0)   ; peak B field strength, Tesla
(define *omega*     1.0)   ; angular frequency of oscillation, rad/s
                           ;   period T = 2π/ω;  frequency f = ω/(2π) Hz
(define *R*         0.9)   ; solenoid cross-section radius, world units
(define *r-loop*    1.5)   ; radius of the white integration loop, world units
(define *time*      0.0)   ; elapsed simulation time, seconds
(define *paused*    #f)
(define *show-B*    #t)    ; toggle: show the B-field region
(define *show-E*    #t)    ; toggle: show the induced E-field arrows
(define *show-loop* #t)    ; toggle: show the integration loop + EMF

;;; ════════════════════════════════════════════════════════════════════════════
;;; FIELD EQUATIONS
;;; ════════════════════════════════════════════════════════════════════════════

;; The magnetic field inside the solenoid oscillates as a cosine.
(define (Bz   tv) (* *B0* (cos (* *omega* tv))))

;; Its time-derivative — this is the quantity that drives the induced E field.
;; (derivative of B₀ cos(ωt) with respect to t is −B₀ω sin(ωt))
(define (dBdt tv) (* (- *B0*) *omega* (sin (* *omega* tv))))

;;; ════════════════════════════════════════════════════════════════════════════
;;; DERIVING E-at: THE EXACT INDUCED ELECTRIC FIELD
;;; ════════════════════════════════════════════════════════════════════════════
;;;
;;; We apply Faraday's Law in INTEGRAL form (equivalent to the differential
;;; form by Stokes' theorem, but easier to evaluate for symmetric geometries):
;;;
;;;   ∮_C  E · dl  =  −d/dt ∫∫_S  B · dA
;;;   "EMF around a closed loop = minus the rate of change of flux through it"
;;;
;;; SYMMETRY ARGUMENT (this is the key step):
;;;   The solenoid is infinitely long and rotationally symmetric around its
;;;   axis (the z-axis).  Therefore the induced E must share that symmetry:
;;;     • E can only point in the φ direction (tangential to circles).
;;;     • The magnitude |E| can only depend on r, not on φ or z.
;;;   If we pick a circular Amperian loop of radius r centred on the axis,
;;;   E is constant in magnitude all the way around it.
;;;
;;; LEFT SIDE of Faraday's integral law (the "EMF"):
;;;   ∮ E · dl = E_φ · (circumference) = E_φ · 2πr
;;;
;;; RIGHT SIDE — CASE 1: loop INSIDE the solenoid (r < R):
;;;   The flux through the loop is  Φ = B_z · πr²  (area of loop × field)
;;;   −dΦ/dt = −πr² · ∂B_z/∂t
;;;
;;;   Equating left and right:
;;;     E_φ · 2πr = −πr² · ∂B_z/∂t
;;;               E_φ = −(r/2) · ∂B_z/∂t          ← grows linearly with r
;;;
;;;   Intuition: a larger circle encloses more area, so more flux change,
;;;   so a stronger induced field at the rim.
;;;
;;; RIGHT SIDE — CASE 2: loop OUTSIDE the solenoid (r ≥ R):
;;;   B = 0 outside, so all the flux comes from the solenoid interior:
;;;     Φ = B_z · πR²   (fixed — doesn't grow with loop size!)
;;;   −dΦ/dt = −πR² · ∂B_z/∂t
;;;
;;;   Equating:
;;;     E_φ · 2πr = −πR² · ∂B_z/∂t
;;;               E_φ = −(R²/2r) · ∂B_z/∂t        ← falls off as 1/r
;;;
;;;   Intuition: once the loop is bigger than the solenoid, it already
;;;   captures all the flux, so making it bigger doesn't increase the EMF.
;;;   The same EMF is shared around a longer path, so |E| must fall.
;;;
;;; CONTINUITY CHECK at r = R:
;;;   Inside formula:  E_φ = −(R/2) · ∂B/∂t
;;;   Outside formula: E_φ = −(R²/2R) · ∂B/∂t = −(R/2) · ∂B/∂t  ✓ they match
;;;   (The field is continuous across the solenoid boundary — good!)
;;;
;;; CONVERTING φ-COMPONENT TO CARTESIAN (x, y) COMPONENTS:
;;;   The azimuthal unit vector at a point (x, y) is:
;;;     ê_φ = (−sin φ, cos φ) = (−y/r, x/r)
;;;   So:  E_x = E_φ · (−y/r)
;;;        E_y = E_φ · (x/r)
;;;
;;; LENZ'S LAW CHECK:
;;;   When ∂B_z/∂t > 0 (B increasing out of page):
;;;     E_φ < 0  →  E points in the −ê_φ direction  →  CLOCKWISE circulation.
;;;   A positive charge following E would go clockwise; its motion constitutes
;;;   a clockwise current, which (by the right-hand rule) produces a B field
;;;   pointing INTO the page — opposing the increase.  ✓

(define (E-at wx wy db R)
  ;; Returns (Ex . Ey) — Cartesian components of induced E at world point (wx, wy).
  ;; db = ∂B_z/∂t at the current moment.
  ;; Guard against division by zero at the origin.
  (let* ((r2  (+ (* wx wx) (* wy wy)))
         (r   (sqrt r2)))
    (if (< r 0.05)
        (cons 0.0 0.0)                     ; field undefined at r=0; return zero
        (let* ((ephi (if (< r R)
                         (* -0.5 r  db)           ; inside:  −r/2 · ∂B/∂t
                         (* -0.5 R R (/ db r))))  ; outside: −R²/(2r) · ∂B/∂t
               (ex   (* (- (/ wy r)) ephi))        ; E_x = (−y/r) · E_φ
               (ey   (* (/ wx r)     ephi)))        ; E_y = ( x/r) · E_φ
          (cons ex ey)))))

;;; ════════════════════════════════════════════════════════════════════════════
;;; UTILITY: NUMBER FORMATTING
;;; ════════════════════════════════════════════════════════════════════════════

(define (fmt2 v) (number->string (/ (round (* v 100.0))  100.0)))  ; 2 d.p.
(define (fmt3 v) (number->string (/ (round (* v 1000.0)) 1000.0))) ; 3 d.p.

;;; ════════════════════════════════════════════════════════════════════════════
;;; DRAWING HELPER — ARROW WITH PROPORTIONAL HEAD
;;; ════════════════════════════════════════════════════════════════════════════
;;;
;;; Draws a line from (x1,y1) to (x2,y2) with a filled triangular arrowhead
;;; at the tip.  The head dimensions are proportional to the shaft length so
;;; short arrows still have readable heads.

(define (arrow! p x1 y1 x2 y2 r g b a lw)
  (gfx-set-pen-color! p r g b a)
  (gfx-set-pen-width! p lw)
  (gfx-draw-line! p x1 y1 x2 y2)
  (let* ((dx (- x2 x1)) (dy (- y2 y1))
         (len (sqrt (+ (* dx dx) (* dy dy)))))
    (when (> len 5.0)
      (let* ((ux (/ dx len)) (uy (/ dy len))  ; unit vector along shaft
             (px (- uy))     (py ux)           ; unit vector perpendicular
             (hs (min 5.0 (* len 0.26)))       ; head half-width in pixels
             (hl (min 9.0 (* len 0.44)))       ; head length in pixels
             (bx (- x2 (* ux hl))) (by (- y2 (* uy hl)))) ; base of head
        (gfx-set-color! p r g b a)
        (gfx-fill-polygon! p
          (list (cons x2 y2)                              ; tip
                (cons (+ bx (* px hs)) (+ by (* py hs))) ; left base corner
                (cons (- bx (* px hs)) (- by (* py hs)))))))))  ; right base corner

;;; ════════════════════════════════════════════════════════════════════════════
;;; DRAW CALLBACK — CALLED EVERY FRAME
;;; ════════════════════════════════════════════════════════════════════════════
;;;
;;; Visual elements drawn each frame:
;;;   1. Faint grid — spatial reference
;;;   2. Axes — x and y directions
;;;   3. B region — coloured circle showing the solenoid cross-section
;;;      Blue  (⊙) = B pointing out of the page (+z direction)
;;;      Red   (⊗) = B pointing into the page  (−z direction)
;;;      Brightness ∝ |B_z|; fades to invisible when B crosses zero.
;;;   4. E field arrows — green arrows showing the induced E at each grid point.
;;;      Arrow length ∝ |E| (normalised to the maximum in the frame).
;;;      Opacity also scales with |E|, so near-zero arrows fade out.
;;;   5. Integration loop — white circle at radius r_loop showing
;;;      Faraday's integral form: EMF = ∮ E·dl = −dΦ/dt.
;;;      A small arrow on the circle shows the circulation direction.
;;;   6. Live values panel — top-left readout of all key quantities.
;;;   7. Phase hint — bottom-of-screen text when the field is at a
;;;      pedagogically interesting moment.

;; Grid dimensions: nx columns × ny rows, spanning ±xw × ±yw world units.
;; Increasing nx, ny makes the arrow field denser but slower to draw.
(define nx 20) (define ny 16)
(define xw 3.1) (define yw 2.5)

(define (draw painter w h)
  (let* ((tv   *time*)
         (bz   (Bz tv))     ; current B_z value (T)
         (db   (dBdt tv))   ; current ∂B_z/∂t value (T/s)
         (cx   (/ w 2.0))   ; canvas centre x (pixels)
         (cy   (/ h 2.0))   ; canvas centre y (pixels)
         (sc   (/ (min w h) 5.8))  ; scale: pixels per world unit
         (Rpx  (* *R*      sc))    ; solenoid radius in pixels
         (Rlpx (* *r-loop* sc))    ; loop radius in pixels
         (gdx  (/ (* 2.0 xw) (- nx 1)))   ; grid cell width  (world units)
         (gdy  (/ (* 2.0 yw) (- ny 1)))   ; grid cell height (world units)
         ;; Max arrow display length = 40% of a grid cell.
         ;; Arrows are scaled relative to the maximum E magnitude in the frame,
         ;; so the strongest arrow always reaches this length.
         (amax (* (min gdx gdy) 0.40)))

    (gfx-clear! painter 0.06 0.06 0.10)
    (gfx-set-antialias! painter #t)

    ;; ── 1. Faint spatial grid ──────────────────────────────────────────────
    (gfx-set-pen-color! painter 0.13 0.13 0.20 1.0)
    (gfx-set-pen-width! painter 0.5)
    (let lp ((i -4))
      (when (<= i 4)
        (let ((sx (+ cx (* i sc))))
          (gfx-draw-line! painter sx (- cy (* yw sc)) sx (+ cy (* yw sc))))
        (lp (+ i 1))))
    (let lp ((j -3))
      (when (<= j 3)
        (let ((sy (+ cy (* j sc))))
          (gfx-draw-line! painter (- cx (* xw sc)) sy (+ cx (* xw sc)) sy))
        (lp (+ j 1))))

    ;; ── 2. Axes ────────────────────────────────────────────────────────────
    (gfx-set-pen-color! painter 0.28 0.28 0.35 0.9)
    (gfx-set-pen-width! painter 1.0)
    (gfx-draw-line! painter (- cx (* xw sc)) cy (+ cx (* xw sc)) cy)
    (gfx-draw-line! painter cx (- cy (* yw sc)) cx (+ cy (* yw sc)))
    (gfx-set-color! painter 0.32 0.32 0.40 0.75)
    (gfx-set-font!  painter "sans-serif" 9)
    (gfx-draw-text! painter (+ cx (* xw sc) 3) (+ cy 4) "x")
    (gfx-draw-text! painter (+ cx 4) (- cy (* yw sc) 8) "y")

    ;; ── 3. B field region (solenoid cross-section) ────────────────────────
    ;;
    ;; Colour encodes the current B_z value:
    ;;   Blue  (⊙, "dot in circle") = field pointing OUT of the page.
    ;;   Red   (⊗, "cross in circle") = field pointing INTO the page.
    ;; Brightness scales with |B_z| so it fades when B ≈ 0.
    ;;
    ;; WATCH: the circle fades to grey at t = π/(2ω), 3π/(2ω), ...
    ;; when B_z = 0.  At those same moments the E arrows are at maximum.
    (when *show-B*
      (let* ((frac  (min 1.0 (abs bz)))   ; brightness, 0–1
             (r-col (if (< bz 0.0) (* 0.85 frac) (* 0.12 frac)))
             (g-col (* 0.14 frac))
             (b-col (if (> bz 0.0) (* 0.85 frac) (* 0.12 frac))))
        (gfx-set-color! painter r-col g-col b-col (* 0.42 frac))
        (gfx-fill-circle! painter cx cy Rpx)
        (gfx-set-pen-color! painter (+ r-col 0.2) g-col (+ b-col 0.2)
                            (min 1.0 (* 0.9 (max 0.15 frac))))
        (gfx-set-pen-width! painter 2.0)
        (gfx-draw-circle! painter cx cy Rpx)
        ;; ⊙ = arrow tip coming toward you (out of page)
        ;; ⊗ = arrow tail going away from you (into page)
        (gfx-set-color! painter (+ r-col 0.35) (+ g-col 0.35) (+ b-col 0.45) 0.92)
        (gfx-set-font!  painter "sans-serif" 20)
        (gfx-draw-text! painter (- cx 10) (+ cy 8)
          (if (>= bz 0.0) "⊙" "⊗"))
        (gfx-set-font! painter "monospace" 9)
        (gfx-draw-text! painter (- cx 22) (+ cy 26)
          (string-append "B = " (fmt2 bz) " T"))))

    ;; ── 4. E field arrows (on a regular grid) ─────────────────────────────
    ;;
    ;; Arrows are green and point in the direction of the induced E field.
    ;; Length and opacity both scale with the magnitude |E| relative to the
    ;; maximum on screen — so the relative strengths at different positions
    ;; are always easy to read.
    ;;
    ;; Notice the pattern:
    ;;   • INSIDE the solenoid (r < R):  arrows get longer as you move out.
    ;;     (E ∝ r — more area enclosed, more flux change, stronger field.)
    ;;   • OUTSIDE the solenoid (r > R): arrows get shorter as you move out.
    ;;     (E ∝ 1/r — total flux is fixed, EMF shared over longer path.)
    ;;   • The arrows all point in circles — confirming curl(E) ≠ 0 here.
    (when *show-E*
      (let* ((emax 1e-10))   ; will be updated to the max |E| seen this frame
        ;; First pass: find the maximum E magnitude (for normalisation).
        ;; This keeps arrows a useful size regardless of the ω and B₀ settings.
        (let lp-i ((i 0))
          (when (< i nx)
            (let lp-j ((j 0))
              (when (< j ny)
                (let* ((wx (+ (- xw) (* i gdx)))
                       (wy (+ (- yw) (* j gdy)))
                       (ev (E-at wx wy db *R*))
                       (em (sqrt (+ (* (car ev) (car ev))
                                    (* (cdr ev) (cdr ev))))))
                  (when (> em emax) (set! emax em)))
                (lp-j (+ j 1))))
            (lp-i (+ i 1))))
        ;; Second pass: draw each arrow.
        ;; Note: screen y is flipped relative to world y (screen y increases
        ;; downward), so we negate the y-component of the unit vector.
        (let lp-i ((i 0))
          (when (< i nx)
            (let lp-j ((j 0))
              (when (< j ny)
                (let* ((wx  (+ (- xw) (* i gdx)))
                       (wy  (+ (- yw) (* j gdy)))
                       (ev  (E-at wx wy db *R*))
                       (ex  (car ev))
                       (ey  (cdr ev))
                       (em  (sqrt (+ (* ex ex) (* ey ey)))))
                  ;; Skip arrows below 2.5% of maximum (they'd be invisible noise).
                  (when (> em (* emax 0.025))
                    (let* ((norm (/ em emax))          ; relative strength 0–1
                           (len  (* amax norm sc))     ; display length in pixels
                           (ux   (/ ex em))            ; unit vector, world x
                           (uy   (- (/ ey em)))        ; unit vector, screen y (flipped)
                           (sx   (+ cx (* wx sc)))     ; screen centre x
                           (sy   (- cy (* wy sc)))     ; screen centre y (y flipped)
                           ;; Arrow centred on grid point: half before, half after.
                           (x1   (- sx (* ux len 0.5)))
                           (y1   (- sy (* uy len 0.5)))
                           (x2   (+ sx (* ux len 0.5)))
                           (y2   (+ sy (* uy len 0.5)))
                           ;; Opacity scales with sqrt(norm): weak fields fade but
                           ;; don't vanish completely so students can still see direction.
                           (alph (min 1.0 (* 0.92 (sqrt norm)))))
                      (arrow! painter x1 y1 x2 y2
                               0.28 0.96 0.38 alph 1.4))))
                (lp-j (+ j 1))))
            (lp-i (+ i 1))))))

    ;; ── 5. Integration loop (Faraday's integral form) ─────────────────────
    ;;
    ;; The white circle at radius r_loop visualises the integral form of
    ;; Faraday's Law:
    ;;
    ;;   ∮_C E · dl  =  −d/dt ∫∫_S B · dA   =   EMF
    ;;
    ;; Left side: if we walk around the circle summing up E · dl, we get the
    ;; EMF — the "voltage" that would drive a current around this path.
    ;;
    ;; Right side: the flux Φ = ∫∫ B·dA through the loop:
    ;;   • If r_loop < R:  Φ = B_z · πr_loop²  →  EMF = −πr_loop² · ∂B/∂t
    ;;   • If r_loop ≥ R:  Φ = B_z · πR²       →  EMF = −πR²      · ∂B/∂t
    ;;
    ;; TRY: Move the r_loop slider.  When r_loop < R, the EMF grows as r².
    ;; When r_loop > R, the EMF stops growing — you've already enclosed all the flux.
    ;;
    ;; The small arrow on the loop shows which way a positive charge would be
    ;; pushed — the direction of the induced "current" (confirming Lenz's Law).
    (when *show-loop*
      (gfx-set-pen-color! painter 0.88 0.88 1.0 0.60)
      (gfx-set-pen-width! painter 1.6)
      (gfx-draw-circle! painter cx cy Rlpx)
      ;; Circulation arrow — only draw when E is large enough to see clearly.
      (when (> (abs db) (* *B0* *omega* 0.05))
        (let* ((phi  (/ pi 4.0))      ; place arrow at 45° on the loop
               ;; Lenz's law direction:
               ;;   db > 0 (B increasing) → E circulates clockwise on screen
               ;;   db < 0 (B decreasing) → E circulates counterclockwise
               ;; Clockwise screen tangent at angle φ is (sin φ, cos φ).
               ;; Multiply by dir = ±1 to flip for CCW.
               (dir  (if (> db 0.0) 1.0 -1.0))
               (lx   (+ cx (* Rlpx (cos phi))))
               (ly   (- cy (* Rlpx (sin phi))))
               (cwx  (sin phi))
               (cwy  (cos phi))
               (alen 20.0))
          (arrow! painter
                  (- lx (* cwx dir alen 0.5))
                  (- ly (* cwy dir alen 0.5))
                  (+ lx (* cwx dir alen 0.5))
                  (+ ly (* cwy dir alen 0.5))
                  0.88 0.88 1.0 0.80 2.0)))
      ;; EMF readout.  reff = effective radius (whichever is smaller: loop or solenoid).
      (let* ((reff  (min *r-loop* *R*))
             (emf   (* (- pi reff reff) db)))
        (gfx-set-color! painter 0.88 0.88 1.0 0.82)
        (gfx-set-font!  painter "monospace" 10)
        (gfx-draw-text! painter (+ cx Rlpx 7) (- cy 6)
          (string-append "EMF = " (fmt2 emf) " V")))
      (gfx-set-color! painter 0.60 0.60 0.80 0.60)
      (gfx-set-font!  painter "sans-serif" 9)
      (gfx-draw-text! painter (+ cx Rlpx 3) (+ cy 5) "rₗ"))

    ;; Solenoid radius label
    (gfx-set-color! painter 0.55 0.55 0.65 0.65)
    (gfx-set-font!  painter "sans-serif" 9)
    (gfx-draw-text! painter (+ cx Rpx 4) (- cy 4) "R")

    ;; ── 6. Live values panel ───────────────────────────────────────────────
    ;;
    ;; Shows all five key quantities simultaneously so students can watch
    ;; them change together and build intuition for the relationships.
    ;;
    ;; t:       elapsed time (s)
    ;; B_z:     current magnetic field (T) — colour-coded: blue = out, red = in
    ;; ∂B/∂t:   rate of change of B (T/s) — THIS is what drives E
    ;; E_φ(rₗ): tangential E at the loop radius (V/m) — what the loop "feels"
    ;; (∇×E)_z: the z-component of curl E — should always equal −∂B/∂t
    (let* ((px 12.0) (py 12.0) (lh 18) (pw 258))
      (gfx-set-color! painter 0.02 0.02 0.04 0.70)
      (gfx-fill-rect! painter px py pw (* 6.8 lh))
      (gfx-set-font!  painter "monospace" 11)
      (gfx-set-color! painter 0.75 0.75 0.75 0.95)
      (gfx-draw-text! painter (+ px 8) (+ py lh)
        (string-append "t        = " (fmt2 *time*) " s"))
      (let ((c (if (>= bz 0.0) (list 0.35 0.55 1.0) (list 1.0 0.35 0.35))))
        (gfx-set-color! painter (car c) (cadr c) (caddr c) 0.95))
      (gfx-draw-text! painter (+ px 8) (+ py (* 2 lh))
        (string-append "B_z      = " (fmt2 bz) " T"
          (if (>= bz 0.0) "  ⊙ out" "  ⊗ in")))
      ;; ∂B/∂t is highlighted in yellow because it is the central quantity:
      ;; everything else (E, EMF, curl) follows from it.
      (gfx-set-color! painter 0.96 0.96 0.40 0.95)
      (gfx-draw-text! painter (+ px 8) (+ py (* 3 lh))
        (string-append "∂B/∂t    = " (fmt2 db) " T/s"))
      ;; E_φ at the loop: evaluated at (r_loop, 0) where E_φ = E_y by symmetry.
      (let* ((ephi (cdr (E-at *r-loop* 0.0 db *R*))))
        (gfx-set-color! painter 0.28 0.96 0.40 0.95)
        (gfx-draw-text! painter (+ px 8) (+ py (* 4 lh))
          (string-append "E_φ(rₗ)  = " (fmt3 ephi) " V/m")))
      ;; (∇×E)_z inside the solenoid equals −∂B/∂t everywhere.
      ;; Comparing yellow (∂B/∂t) and purple ((∇×E)_z) confirms the law.
      (gfx-set-color! painter 0.68 0.68 1.0 0.95)
      (gfx-draw-text! painter (+ px 8) (+ py (* 5 lh))
        (string-append "(∇×E)_z  = " (fmt2 (- db)) " s⁻¹"))
      (gfx-set-color! painter 0.50 0.50 0.55 0.80)
      (gfx-draw-text! painter (+ px 8) (+ py (* 6.3 lh))
        "∇×E = −∂B/∂t   ✓"))

    ;; ── 7. Phase hint ──────────────────────────────────────────────────────
    ;;
    ;; At pedagogically significant moments, a banner appears at the bottom
    ;; of the canvas calling attention to the relationship between B and E.
    ;; Use the +¼ period button to step to each of these moments in turn.
    (let* ((hint (cond
                   ((< (abs db) (* *B0* *omega* 0.08))
                    "B at extremum — ∂B/∂t ≈ 0  →  E field = 0")
                   ((> (abs db) (* *B0* *omega* 0.92))
                    "B crossing zero — |∂B/∂t| maximum  →  |E| maximum")
                   (else ""))))
      (when (> (string-length hint) 0)
        (gfx-set-color!  painter 1.0 0.90 0.40 0.88)
        (gfx-set-font!   painter "sans-serif" 12 #t)
        (gfx-draw-text!  painter (- (/ w 2.0) 190.0) (- h 18.0) hint)))

    (when *paused*
      (gfx-set-color!  painter 1.0 0.75 0.20 0.92)
      (gfx-set-font!   painter "sans-serif" 15 #t)
      (gfx-draw-text!  painter (- (/ w 2.0) 28.0) 26.0 "PAUSED"))))

;;; ════════════════════════════════════════════════════════════════════════════
;;; WINDOW AND SIDEBAR
;;; ════════════════════════════════════════════════════════════════════════════

(define *win*    (make-window "Faraday's Law Explorer" 1040 680))
(define *canvas* (window-canvas *win*))
(define *sb*     (window-sidebar *win*))

;;; ── Sidebar: heading and key physics ──────────────────────────────────────

(box-add! *sb* (make-label "FARADAY'S LAW"))
(box-add! *sb* (make-label ""))
(box-add! *sb* (make-label "   ∇ × E = −∂B/∂t"))
(box-add! *sb* (make-label ""))
;; Three sentences that capture the entire content of the law.
(box-add! *sb* (make-label "▸ A CHANGING B creates"))
(box-add! *sb* (make-label "  a circulating E field."))
(box-add! *sb* (make-label ""))
(box-add! *sb* (make-label "▸ Strength ∝ |∂B/∂t|, not |B|."))
(box-add! *sb* (make-label "  E = 0 when B is at its peak!"))
(box-add! *sb* (make-label ""))
(box-add! *sb* (make-label "▸ Lenz's law: induced E opposes"))
(box-add! *sb* (make-label "  the change that caused it."))
(box-add! *sb* (make-separator))

;;; ── Sidebar: B field sliders ──────────────────────────────────────────────

(box-add! *sb* (make-label "── B field ──"))
;; B₀: peak field strength.  Doubling B₀ doubles E proportionally.
(box-add! *sb* (make-label "Peak  B₀  (T)"))
(box-add! *sb*
  (make-slider "B0" 0.2 2.0 0.05 *B0*
    (lambda (v) (set! *B0* v) (canvas-redraw! *canvas*))))
;; ω: angular frequency.  Doubling ω doubles ∂B/∂t (and therefore E)
;; even though B₀ stays the same.  Try this — it's counterintuitive at first.
(box-add! *sb* (make-label "Frequency  ω  (rad/s)"))
(box-add! *sb*
  (make-slider "ω" 0.2 3.0 0.05 *omega*
    (lambda (v) (set! *omega* v) (canvas-redraw! *canvas*))))
;; R: solenoid radius.  Notice how the E field both inside and outside changes.
(box-add! *sb* (make-label "Solenoid radius  R"))
(box-add! *sb*
  (make-slider "R" 0.3 1.6 0.05 *R*
    (lambda (v) (set! *R* v) (canvas-redraw! *canvas*))))
(box-add! *sb* (make-separator))

;;; ── Sidebar: integration loop slider ─────────────────────────────────────

(box-add! *sb* (make-label "── Integration loop ──"))
;; The white circle at radius r_loop lets you apply the integral form:
;;   EMF = −dΦ/dt
;; When r_loop < R: more area → more flux → EMF grows as r_loop².
;; When r_loop > R: all flux already enclosed → EMF stays constant.
(box-add! *sb* (make-label "Loop radius  rₗ"))
(box-add! *sb*
  (make-slider "rloop" 0.3 2.5 0.05 *r-loop*
    (lambda (v) (set! *r-loop* v) (canvas-redraw! *canvas*))))
(box-add! *sb* (make-label "Move rₗ across R to see EMF"))
(box-add! *sb* (make-label "saturate once rₗ > R."))
(box-add! *sb* (make-separator))

;;; ── Sidebar: display toggles ──────────────────────────────────────────────

(box-add! *sb* (make-label "── Display ──"))
(box-add! *sb*
  (make-toggle "B field region" *show-B*
    (lambda (on) (set! *show-B* on) (canvas-redraw! *canvas*))))
(box-add! *sb*
  (make-toggle "E field arrows" *show-E*
    (lambda (on) (set! *show-E* on) (canvas-redraw! *canvas*))))
(box-add! *sb*
  (make-toggle "Integration loop" *show-loop*
    (lambda (on) (set! *show-loop* on) (canvas-redraw! *canvas*))))
(box-add! *sb* (make-separator))

;;; ── Sidebar: CAS symbolic verification ───────────────────────────────────
;;;
;;; The solenoid solution above is numeric.  This section uses the built-in
;;; computer algebra system to verify Faraday's Law symbolically for a
;;; completely different field — the plane electromagnetic wave.
;;;
;;; For E_y = cos(x−t), B_z = cos(x−t) the z-component of ∇×E = −∂B/∂t
;;; reduces to  ∂E_y/∂x = −∂B_z/∂t.  The CAS differentiates both sides and
;;; subtracts.  The result is 0 — meaning the law is satisfied identically,
;;; for every (x, t), not just at one point.

(box-add! *sb* (make-label "── CAS verification ──"))
(box-add! *sb* (make-label "Plane wave: E_y=B_z=cos(x−t)"))
(box-add! *sb* (make-label ""))
(box-add! *sb* (make-label (string-append "∂E_y/∂x  = " (sym->infix dEy-dx))))
(box-add! *sb* (make-label (string-append "−∂B_z/∂t = " (sym->infix neg-dBz-dt))))
(box-add! *sb* (make-label ""))
(box-add! *sb* (make-label
  (string-append "Residual = "
    (let ((r faraday-cas))
      (if (and (integer? r) (zero? r)) "0   ✓  (holds for all x,t)" (sym->infix r))))))
(box-add! *sb* (make-separator))

;;; ── Sidebar: guided exercises ─────────────────────────────────────────────
;;;
;;; These four exercises are designed so that completing them requires the
;;; student to engage with the key physics, not just watch the animation.

(box-add! *sb* (make-label "── Guided exercises ──"))
(box-add! *sb* (make-label "1. Pause when E=0. What is B_z?"))
(box-add! *sb* (make-label "   What is ∂B/∂t? Explain why."))
(box-add! *sb* (make-label ""))
(box-add! *sb* (make-label "2. Step +¼ period. E is now max."))
(box-add! *sb* (make-label "   Is B_z big or small? Why?"))
(box-add! *sb* (make-label ""))
(box-add! *sb* (make-label "3. Drag rₗ from 0.3 to 2.5."))
(box-add! *sb* (make-label "   When does EMF stop growing?"))
(box-add! *sb* (make-label ""))
(box-add! *sb* (make-label "4. Double ω. What happens to E?"))
(box-add! *sb* (make-label "   Now double B₀ instead."))
(box-add! *sb* (make-label "   Same result? Why or why not?"))
(box-add! *sb* (make-separator))

;;; ── Sidebar: control buttons ──────────────────────────────────────────────

;; Pause / Resume: essential for examining any given instant.
(box-add! *sb*
  (make-button "▶  Pause / Resume  [Space]"
    (lambda () (set! *paused* (not *paused*)))))
;; +¼ period: advances exactly one quarter of the current period (T/4 = π/(2ω)).
;; The four phases are: B max → E max CW → B min → E max CCW → back to start.
(box-add! *sb*
  (make-button "→  +¼ period  [→ key]"
    (lambda ()
      (set! *time* (+ *time* (/ pi (* 2.0 *omega*))))
      (canvas-redraw! *canvas*))))
(box-add! *sb*
  (make-button "↺  Reset time  [r]"
    (lambda () (set! *time* 0.0) (canvas-redraw! *canvas*))))
(box-add! *sb*
  (make-button "✕  Quit  [q]" quit-event-loop))

;;; ════════════════════════════════════════════════════════════════════════════
;;; EVENT LOOP SETUP
;;; ════════════════════════════════════════════════════════════════════════════

(canvas-on-draw! *canvas* draw)

(window-on-key! *win*
  (lambda (key mods)
    (cond
      ((equal? key "space")  (set! *paused* (not *paused*)))
      ((equal? key "r")      (set! *time* 0.0) (canvas-redraw! *canvas*))
      ((equal? key "Right")
       (set! *time* (+ *time* (/ pi (* 2.0 *omega*))))
       (canvas-redraw! *canvas*))
      ((equal? key "q")      (quit-event-loop))
      ((equal? key "Escape") (quit-event-loop)))))

;; Timer fires every 16 ms (≈ 60 fps).  On each tick, advance time by 16 ms
;; and request a canvas redraw.  When paused, we still redraw (so the PAUSED
;; banner appears) but do not advance time.
(define *timer*
  (make-timer 16
    (lambda ()
      (unless *paused*
        (set! *time* (+ *time* 0.016)))
      (canvas-redraw! *canvas*))))

(timer-start! *timer*)

(statusbar-set-text! (window-status-bar *win*)
  "Space = pause  ·  → or [+¼] = step one quarter period  ·  r = reset  ·  q = quit")

(window-on-close! *win* quit-event-loop)
(window-show! *win*)
(run-event-loop)
