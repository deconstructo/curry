;;; ampere-explorer.scm — v1.0
;;;
;;; Interactive workbook for Ampere's Law (Maxwell's version).
;;; Companion lesson to examples/faraday-explorer.scm.
;;;
;;; ════════════════════════════════════════════════════════════════════════════
;;; BACKGROUND — READ THIS FIRST
;;; ════════════════════════════════════════════════════════════════════════════
;;;
;;; In the Faraday demo you saw:
;;;
;;;   ∇ × E = −∂B/∂t
;;;   "A changing B creates a circulating E field."
;;;
;;; Maxwell's completed Ampere's Law says:
;;;
;;;   ∇ × B = μ₀J + μ₀ε₀ ∂E/∂t
;;;
;;; Reading it piece by piece:
;;;
;;;   ∇ × B       — the curl of the magnetic field B.
;;;                  Curl ≠ 0 means the field circulates — B goes around in loops.
;;;
;;;   μ₀J         — μ₀ times the conduction current density J.
;;;                  J = amps per square metre of actual moving charge.
;;;                  This was Ampere's original (pre-Maxwell) law.
;;;
;;;   μ₀ε₀ ∂E/∂t  — Maxwell's addition: μ₀ε₀ times the rate of change of E.
;;;                  This term makes no charges move — it is purely from a
;;;                  changing electric field.  Maxwell called it the
;;;                  "displacement current" by analogy with J.
;;;
;;; WHY MAXWELL HAD TO ADD THE ∂E/∂t TERM:
;;;   Imagine charging a parallel-plate capacitor.  Current flows in the wire
;;;   on each side (J ≠ 0), but in the gap between the plates there is no
;;;   charge flow (J = 0).  Without the ∂E/∂t term, Ampere's Law gives
;;;   different answers depending on which surface you wrap around the wire —
;;;   a mathematical contradiction.  Maxwell's addition resolves it: the
;;;   growing E in the gap acts exactly like a current for the purpose of
;;;   generating B, even though no charge crosses the gap.
;;;
;;; THE DEEP SYMMETRY WITH FARADAY:
;;;   Faraday:  changing B  →  circulating E   (∇×E = −∂B/∂t)
;;;   Ampere:   changing E  →  circulating B   (∇×B =  μ₀ε₀ ∂E/∂t, in free space)
;;;
;;;   This mutual induction is why electromagnetic waves exist: changing E
;;;   produces B, which produces E, which produces B ... propagating at
;;;   c = 1/√(μ₀ε₀).  Maxwell predicted light was an EM wave from this.
;;;
;;; TWO MODES IN THIS DEMO:
;;;
;;;   DISPLACEMENT CURRENT (default):
;;;     E oscillates in a cylindrical region.  ∂E/∂t drives circulating B.
;;;     Direct mirror of Faraday.  B is 90° out of phase with E — B is maximum
;;;     when E crosses zero, and zero when E is at its peak.
;;;
;;;   CONDUCTION CURRENT:
;;;     AC current density J oscillates in a wire cross-section.  J directly
;;;     drives B.  B is IN PHASE with J — no 90° lag.
;;;     This is the classical Ampere's Law that predates Maxwell.
;;;
;;; THE KEY CONTRAST (the most important thing to take away from this demo):
;;;   Displacement mode: B is 90° behind E    — same phase relationship as Faraday
;;;   Conduction mode:   B is in phase with J — B follows the current instantly
;;;
;;; For the full student guide see: docs/ampere-explorer.md
;;;
;;; Controls:  [Space] pause · [→] +¼ period · [m] toggle mode · [r] reset · [q] quit
;;; Run:  ./build/curry examples/ampere-explorer.scm
;;; ════════════════════════════════════════════════════════════════════════════

(import (curry qt6))
(import (scheme base))
(import (scheme inexact))
(import (scheme write))

;;; ════════════════════════════════════════════════════════════════════════════
;;; SYMBOLIC CAS — PLANE-WAVE VERIFICATION OF AMPERE'S LAW
;;; ════════════════════════════════════════════════════════════════════════════
;;;
;;; We verify Ampere's Law for the plane wave  E_y = cos(x − t), B_z = cos(x − t)
;;; in natural units (c = 1, so μ₀ε₀ = 1, free space J = 0).
;;;
;;; The y-component of  ∇ × B = μ₀ε₀ ∂E/∂t  simplifies to:
;;;   −∂B_z/∂x  =  ∂E_y/∂t    (with μ₀ε₀ = 1)
;;;
;;; Left side:  −∂[cos(x−t)]/∂x  =  −(−sin(x−t))  =  sin(x−t)
;;; Right side:  ∂[cos(x−t)]/∂t  =  −sin(x−t)·(−1)  =  sin(x−t)  ✓
;;;
;;; The residual is zero: the law holds for every (x, t), not just one point.
;;; Compare with the Faraday CAS check in faraday-explorer.scm — same wave,
;;; same verification strategy, but now checking the Ampere component.

(symbolic x t)

(define neg-dBz-dx (simplify (- (∂ (cos (- x t)) x))))  ; sin(x−t)
(define dEy-dt     (simplify (∂ (cos (- x t)) t)))       ; sin(x−t)
(define ampere-cas (simplify (- neg-dBz-dx dEy-dt)))     ; 0  ✓

;;; ════════════════════════════════════════════════════════════════════════════
;;; CONSTANTS AND WORLD STATE
;;; ════════════════════════════════════════════════════════════════════════════

(define pi 3.14159265358979323846)

;; In SI: μ₀ = 4π×10⁻⁷ H/m, ε₀ = 8.85×10⁻¹² F/m, c = 1/√(μ₀ε₀) = 3×10⁸ m/s.
;; We set both to 1 so B and E have comparable magnitudes for a readable display.
(define mu0  1.0)
(define eps0 1.0)

(define *E0*        1.0)   ; peak E field, displacement current mode (V/m)
(define *J0*        1.0)   ; peak current density, conduction mode (A/m²)
(define *omega*     1.0)   ; angular frequency (rad/s)
(define *R*         0.9)   ; source region radius (world units)
(define *r-loop*    1.5)   ; Amperian loop radius (world units)
(define *time*      0.0)   ; elapsed simulation time (s)
(define *paused*    #f)
(define *disp-mode* #t)    ; #t = displacement current, #f = conduction current
(define *show-src*  #t)    ; show source field region (E or J)
(define *show-B*    #t)    ; show induced B arrows
(define *show-loop* #t)    ; show integration loop

;;; ════════════════════════════════════════════════════════════════════════════
;;; FIELD EQUATIONS
;;; ════════════════════════════════════════════════════════════════════════════

;; The "source field" displayed in the central region:
;;   displacement mode: E_z(t) = E₀ cos(ωt)
;;   conduction mode:   J_z(t) = J₀ cos(ωt)
(define (source-field tv)
  (if *disp-mode*
      (* *E0* (cos (* *omega* tv)))
      (* *J0* (cos (* *omega* tv)))))

;; The effective current density that drives B (the right-hand side of Ampere's law):
;;
;;   Displacement mode:  J_eff = ε₀ ∂E/∂t = −ε₀ E₀ ω sin(ωt)
;;   Conduction mode:    J_eff = J_z       =  J₀ cos(ωt)
;;
;; THE CRUCIAL PHASE DIFFERENCE:
;;   In displacement mode, J_eff ∝ −sin(ωt) while E ∝ cos(ωt) — they are
;;   90° out of phase.  B (which follows J_eff) is maximum when E = 0, and
;;   zero when E is at its peak.  This is the SAME 90° lag as in Faraday's law.
;;
;;   In conduction mode, J_eff = J ∝ cos(ωt) — B is IN PHASE with J.
;;   Larger current immediately creates stronger B with no phase shift.
(define (jeff tv)
  (if *disp-mode*
      (* (- eps0) *E0* *omega* (sin (* *omega* tv)))
      (* *J0* (cos (* *omega* tv)))))

;;; ════════════════════════════════════════════════════════════════════════════
;;; DERIVING B-at: THE EXACT INDUCED MAGNETIC FIELD
;;; ════════════════════════════════════════════════════════════════════════════
;;;
;;; The geometry is identical to the Faraday solenoid (cylindrical region of
;;; radius R, uniform source inside, nothing outside), so the derivation uses
;;; the same symmetry + integral-form strategy.
;;;
;;; Ampere's Law in integral form (from ∇×B = μ₀J_total via Stokes' theorem):
;;;
;;;   ∮_C  B · dl  =  μ₀ ∫∫_S  J_total · dA
;;;
;;; SYMMETRY ARGUMENT:
;;;   The source is rotationally symmetric, so B can only be azimuthal and
;;;   can only depend on r.  Picking a circular loop of radius r:
;;;     ∮ B · dl  =  B_φ · 2πr
;;;
;;; CASE 1 — loop inside (r < R):
;;;   I_enclosed = J_eff · πr²
;;;   B_φ · 2πr  = μ₀ J_eff · πr²
;;;   B_φ        = (μ₀/2) r J_eff     ← grows linearly with r (more area, more current)
;;;
;;; CASE 2 — loop outside (r ≥ R):
;;;   I_enclosed = J_eff · πR²  (fixed — loop already captures all the source current)
;;;   B_φ · 2πr  = μ₀ J_eff · πR²
;;;   B_φ        = (μ₀/2) (R²/r) J_eff  ← falls off as 1/r (same EMF, longer path)
;;;
;;; CONTINUITY at r = R: (μ₀/2)R = (μ₀/2)(R²/R) ✓
;;;
;;; DIRECTION — right-hand rule:
;;;   J_eff > 0 (effective current out of page) → B circulates counterclockwise.
;;;   Azimuthal unit vector ê_φ = (−y/r, x/r) gives CCW circulation.
;;;   So: B_x = B_φ · (−y/r),  B_y = B_φ · (x/r)
;;;
;;;   Note: this is the OPPOSITE sense from Faraday's law (Lenz) where
;;;   the induced E was clockwise for positive ∂B/∂t.  There is no minus
;;;   sign in Ampere's law — J directly drives B in the same-hand sense.

(define (B-at wx wy je R)
  ;; Returns (Bx . By) — Cartesian components of induced B at world point (wx, wy).
  ;; je = J_eff (effective current density driving B).
  (let* ((r2  (+ (* wx wx) (* wy wy)))
         (r   (sqrt r2)))
    (if (< r 0.05)
        (cons 0.0 0.0)
        (let* ((bphi (if (< r R)
                         (* 0.5 mu0 r je)              ; inside:  (μ₀/2) r J_eff
                         (* 0.5 mu0 R R (/ je r))))    ; outside: (μ₀/2)(R²/r) J_eff
               (bx   (* (- (/ wy r)) bphi))            ; B_x = (−y/r) B_φ
               (by   (* (/ wx r)     bphi)))            ; B_y = ( x/r) B_φ
          (cons bx by)))))

;;; ════════════════════════════════════════════════════════════════════════════
;;; UTILITIES
;;; ════════════════════════════════════════════════════════════════════════════

(define (fmt2 v) (number->string (/ (round (* v 100.0))  100.0)))
(define (fmt3 v) (number->string (/ (round (* v 1000.0)) 1000.0)))

;;; ════════════════════════════════════════════════════════════════════════════
;;; DRAWING HELPER — ARROW WITH PROPORTIONAL HEAD
;;; ════════════════════════════════════════════════════════════════════════════

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
;;; DRAW CALLBACK — CALLED EVERY FRAME
;;; ════════════════════════════════════════════════════════════════════════════
;;;
;;; Visual elements:
;;;   1. Grid + axes
;;;   2. Source region — orange (out of page) / purple (into page)
;;;      Displacement mode: the E_z field inside the cylinder
;;;      Conduction mode:   the J_z current density inside the wire
;;;   3. B field arrows — CYAN, circulating around the source region
;;;      Inside (r < R):  arrows grow longer outward (B ∝ r)
;;;      Outside (r > R): arrows shorten outward      (B ∝ 1/r)
;;;   4. Integration loop — white circle; ∮ B·dl = μ₀ I_enclosed
;;;   5. Live values panel — shows the chain: source → J_eff → B → curl
;;;   6. Phase hint — flags the two pedagogically important moments

(define nx 20) (define ny 16)
(define xw 3.1) (define yw 2.5)

(define (draw painter w h)
  (let* ((tv   *time*)
         (sf   (source-field tv))   ; E_z or J_z
         (je   (jeff tv))           ; effective current density (driver of B)
         (cx   (/ w 2.0))
         (cy   (/ h 2.0))
         (sc   (/ (min w h) 5.8))
         (Rpx  (* *R*      sc))
         (Rlpx (* *r-loop* sc))
         (gdx  (/ (* 2.0 xw) (- nx 1)))
         (gdy  (/ (* 2.0 yw) (- ny 1)))
         (amax (* (min gdx gdy) 0.40)))

    (gfx-clear! painter 0.06 0.06 0.10)
    (gfx-set-antialias! painter #t)

    ;; ── 1. Grid ───────────────────────────────────────────────────────────
    (gfx-set-pen-color! painter 0.13 0.13 0.20 1.0)
    (gfx-set-pen-width! painter 0.5)
    (let lp ((i -4))
      (when (<= i 4)
        (gfx-draw-line! painter (+ cx (* i sc)) (- cy (* yw sc))
                                (+ cx (* i sc)) (+ cy (* yw sc)))
        (lp (+ i 1))))
    (let lp ((j -3))
      (when (<= j 3)
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

    ;; ── 3. Source field region ────────────────────────────────────────────
    ;;
    ;; Orange/yellow = source field pointing OUT of the page (+z direction).
    ;; Purple/violet = source field pointing INTO the page (−z direction).
    ;;
    ;; Displacement mode: the orange disk is the electric field E_z.
    ;;   COMPARE WITH FARADAY: that demo used blue/red for the magnetic field B.
    ;;   The physics is symmetric — here we're looking at the "other half".
    ;;
    ;; Conduction mode: the orange disk is the current density J_z.
    ;;   This is the classical picture of a wire cross-section carrying AC current.
    ;;
    ;; In both modes the brightness scales with the field magnitude, so the
    ;; disk fades when the source is crossing zero.
    (when *show-src*
      (let* ((frac  (min 1.0 (abs sf)))
             (out   (> sf 0.0))
             (r-col (if out (* 0.90 frac) (* 0.40 frac)))
             (g-col (if out (* 0.48 frac) (* 0.06 frac)))
             (b-col (if out (* 0.05 frac) (* 0.88 frac))))
        (gfx-set-color! painter r-col g-col b-col (* 0.45 frac))
        (gfx-fill-circle! painter cx cy Rpx)
        (gfx-set-pen-color! painter
          (min 1.0 (+ r-col 0.15)) (min 1.0 (+ g-col 0.15)) (min 1.0 (+ b-col 0.15))
          (min 1.0 (* 0.9 (max 0.15 frac))))
        (gfx-set-pen-width! painter 2.0)
        (gfx-draw-circle! painter cx cy Rpx)
        (gfx-set-color! painter
          (min 1.0 (+ r-col 0.28)) (min 1.0 (+ g-col 0.28)) (min 1.0 (+ b-col 0.32)) 0.92)
        (gfx-set-font!  painter "sans-serif" 20)
        (gfx-draw-text! painter (- cx 10) (+ cy 8) (if out "⊙" "⊗"))
        (gfx-set-font!  painter "monospace" 9)
        (gfx-draw-text! painter (- cx 30) (+ cy 26)
          (if *disp-mode*
              (string-append "E = " (fmt2 sf) " V/m")
              (string-append "J = " (fmt2 sf) " A/m²")))))

    ;; ── 4. Induced B field arrows ─────────────────────────────────────────
    ;;
    ;; CYAN arrows (to distinguish from Faraday's green E arrows).
    ;; Length and opacity scale with |B| relative to the frame maximum.
    ;;
    ;; Direction: right-hand rule from J_eff.
    ;;   J_eff > 0 (out of page) → B circulates counterclockwise.
    ;;   J_eff < 0 (into page)   → B circulates clockwise.
    ;; No minus sign here — unlike Faraday/Lenz, Ampere has no opposition rule.
    ;;
    ;; Radial pattern:
    ;;   Inside (r < R):  |B| ∝ r — arrows get longer toward the boundary.
    ;;   Outside (r > R): |B| ∝ 1/r — arrows shorten as you move away.
    ;;   Same pattern as the E arrows in the Faraday demo.
    (when *show-B*
      (let* ((bmax 1e-10))
        (let lp-i ((i 0))
          (when (< i nx)
            (let lp-j ((j 0))
              (when (< j ny)
                (let* ((wx (+ (- xw) (* i gdx)))
                       (wy (+ (- yw) (* j gdy)))
                       (bv (B-at wx wy je *R*))
                       (bm (sqrt (+ (* (car bv) (car bv)) (* (cdr bv) (cdr bv))))))
                  (when (> bm bmax) (set! bmax bm)))
                (lp-j (+ j 1))))
            (lp-i (+ i 1))))
        (let lp-i ((i 0))
          (when (< i nx)
            (let lp-j ((j 0))
              (when (< j ny)
                (let* ((wx  (+ (- xw) (* i gdx)))
                       (wy  (+ (- yw) (* j gdy)))
                       (bv  (B-at wx wy je *R*))
                       (bx  (car bv))
                       (by  (cdr bv))
                       (bm  (sqrt (+ (* bx bx) (* by by)))))
                  (when (> bm (* bmax 0.025))
                    (let* ((norm (/ bm bmax))
                           (len  (* amax norm sc))
                           (ux   (/ bx bm))
                           (uy   (- (/ by bm)))   ; screen y flipped
                           (sx   (+ cx (* wx sc)))
                           (sy   (- cy (* wy sc)))
                           (x1   (- sx (* ux len 0.5)))
                           (y1   (- sy (* uy len 0.5)))
                           (x2   (+ sx (* ux len 0.5)))
                           (y2   (+ sy (* uy len 0.5)))
                           (alph (min 1.0 (* 0.92 (sqrt norm)))))
                      (arrow! painter x1 y1 x2 y2
                               0.20 0.82 1.00 alph 1.4))))
                (lp-j (+ j 1))))
            (lp-i (+ i 1))))))

    ;; ── 5. Integration loop (Ampere's integral form) ──────────────────────
    ;;
    ;; The white circle at r_loop visualises:
    ;;   ∮_C  B · dl  =  μ₀ I_enclosed
    ;;
    ;; Where I_enclosed = J_eff × π × r_eff², and r_eff = min(r_loop, R).
    ;;
    ;; By symmetry: ∮ B·dl = B_φ(r_loop) × 2π × r_loop.
    ;; You can verify: B_φ × 2πr = μ₀ × J_eff × π × r_eff² in both regions. ✓
    ;;
    ;; TRY: drag r_loop past R — the ∮B·dl readout saturates once you've
    ;; enclosed all the source current.  Same saturation experiment as Faraday.
    ;;
    ;; The small arrow on the loop shows B's circulation direction
    ;; (counterclockwise for J_eff > 0 by right-hand rule).
    (when *show-loop*
      (gfx-set-pen-color! painter 0.88 0.88 1.0 0.60)
      (gfx-set-pen-width! painter 1.6)
      (gfx-draw-circle! painter cx cy Rlpx)
      ;; Circulation arrow — only when J_eff is large enough to be meaningful.
      (let* ((je-scale (if *disp-mode*
                           (* eps0 *E0* *omega*)
                           *J0*)))
        (when (> (abs je) (* je-scale 0.05))
          (let* ((phi  (/ pi 4.0))
                 ;; Ampere (no minus sign): J_eff > 0 → B goes CCW in world.
                 ;; CCW in world = CCW on screen (y-flip preserves CCW).
                 ;; Using (sin φ, cos φ) as the CW screen tangent:
                 ;;   dir = -1 → arrow goes in (-CW) = CCW direction.
                 ;;   dir = +1 → arrow goes in CW direction.
                 (dir  (if (> je 0.0) -1.0 1.0))
                 (lx   (+ cx (* Rlpx (cos phi))))
                 (ly   (- cy (* Rlpx (sin phi))))
                 (tx   (sin phi))
                 (ty   (cos phi))
                 (alen 20.0))
            (arrow! painter
                    (- lx (* tx dir alen 0.5)) (- ly (* ty dir alen 0.5))
                    (+ lx (* tx dir alen 0.5)) (+ ly (* ty dir alen 0.5))
                    0.88 0.88 1.0 0.80 2.0))))
      ;; ∮ B·dl readout.
      (let* ((reff (min *r-loop* *R*))
             (circ (* mu0 pi reff reff je)))
        (gfx-set-color! painter 0.88 0.88 1.0 0.82)
        (gfx-set-font!  painter "monospace" 10)
        (gfx-draw-text! painter (+ cx Rlpx 7) (- cy 6)
          (string-append "∮B·dl = " (fmt2 circ))))
      (gfx-set-color! painter 0.60 0.60 0.80 0.60)
      (gfx-set-font!  painter "sans-serif" 9)
      (gfx-draw-text! painter (+ cx Rlpx 3) (+ cy 5) "rₗ"))

    ;; Source region radius label.
    (gfx-set-color! painter 0.55 0.55 0.65 0.65)
    (gfx-set-font!  painter "sans-serif" 9)
    (gfx-draw-text! painter (+ cx Rpx 4) (- cy 4) "R")

    ;; ── 6. Live values panel ──────────────────────────────────────────────
    ;;
    ;; Colour coding of the five rows:
    ;;   White:   time
    ;;   Orange:  the source field (E or J) — what you're changing
    ;;   Yellow:  J_eff — the actual driver of B (∂E/∂t in displacement mode)
    ;;   Cyan:    B_φ at the loop — the induced/driven field
    ;;   Lavender: (∇×B)_z — should equal μ₀ J_eff (Ampere's law verification)
    ;;
    ;; In displacement mode, rows 2 and 3 differ — E and ∂E/∂t are 90° apart.
    ;; In conduction mode, rows 2 and 3 are equal — J directly equals J_eff.
    ;; Watching the two yellow/orange rows in displacement mode makes the
    ;; 90° phase lag obvious.
    (let* ((px 12.0) (py 12.0) (lh 18) (pw 278))
      (gfx-set-color! painter 0.02 0.02 0.04 0.70)
      (gfx-fill-rect! painter px py pw (* 6.8 lh))
      (gfx-set-font!  painter "monospace" 11)
      ;; Row 1: time
      (gfx-set-color! painter 0.75 0.75 0.75 0.95)
      (gfx-draw-text! painter (+ px 8) (+ py lh)
        (string-append "t        = " (fmt2 *time*) " s"))
      ;; Row 2: source field (orange = out, purple = in)
      (let ((c (if (>= sf 0.0) '(0.95 0.58 0.15) '(0.62 0.18 0.90))))
        (gfx-set-color! painter (car c) (cadr c) (caddr c) 0.95))
      (gfx-draw-text! painter (+ px 8) (+ py (* 2 lh))
        (if *disp-mode*
            (string-append "E_z      = " (fmt2 sf) " V/m"
              (if (>= sf 0.0) "  ⊙" "  ⊗"))
            (string-append "J_z      = " (fmt2 sf) " A/m²"
              (if (>= sf 0.0) "  ⊙" "  ⊗"))))
      ;; Row 3: J_eff — the direct driver of B (yellow; watch it vs row 2)
      (gfx-set-color! painter 0.96 0.96 0.40 0.95)
      (gfx-draw-text! painter (+ px 8) (+ py (* 3 lh))
        (if *disp-mode*
            (string-append "ε₀∂E/∂t  = " (fmt2 je) " A/m²")
            (string-append "J_eff    = " (fmt2 je) " A/m²")))
      ;; Row 4: B_φ at the loop (cyan)
      (let* ((bphi (cdr (B-at *r-loop* 0.0 je *R*))))
        (gfx-set-color! painter 0.20 0.85 1.00 0.95)
        (gfx-draw-text! painter (+ px 8) (+ py (* 4 lh))
          (string-append "B_φ(rₗ)  = " (fmt3 bphi) " T")))
      ;; Row 5: (∇×B)_z inside = μ₀ J_eff — should match row 3 scaled by μ₀
      (gfx-set-color! painter 0.68 0.68 1.00 0.95)
      (gfx-draw-text! painter (+ px 8) (+ py (* 5 lh))
        (string-append "(∇×B)_z  = " (fmt2 (* mu0 je)) " T/m"))
      ;; Row 6: law confirmation
      (gfx-set-color! painter 0.50 0.50 0.55 0.80)
      (gfx-draw-text! painter (+ px 8) (+ py (* 6.3 lh))
        (if *disp-mode*
            "∇×B = μ₀ε₀ ∂E/∂t  ✓"
            "∇×B = μ₀J  ✓")))

    ;; ── 7. Phase hint ─────────────────────────────────────────────────────
    ;;
    ;; Flags the two key moments each half-period.
    ;;
    ;; Displacement mode — same 90° pattern as Faraday (but B/E instead of E/B):
    ;;   E at extremum → ∂E/∂t = 0 → B = 0
    ;;   E crossing zero → ∂E/∂t maximum → B maximum
    ;;
    ;; Conduction mode — no phase lag:
    ;;   J at maximum → B at maximum   (B follows J instantly)
    ;;   J at zero    → B at zero
    (let* ((je-max (if *disp-mode* (* eps0 *E0* *omega*) *J0*))
           (hint
            (if *disp-mode*
                (cond
                  ((< (abs je) (* je-max 0.08))
                   "E at extremum — ∂E/∂t ≈ 0  →  B = 0")
                  ((> (abs je) (* je-max 0.92))
                   "E crossing zero — |∂E/∂t| max  →  |B| maximum")
                  (else ""))
                (cond
                  ((> (abs je) (* je-max 0.92))
                   "J maximum  →  B maximum  (B in phase with J, no lag!)")
                  ((< (abs je) (* je-max 0.08))
                   "J ≈ 0  →  B ≈ 0  (B tracks J with no 90° delay)")
                  (else "")))))
      (when (> (string-length hint) 0)
        (gfx-set-color!  painter 1.0 0.90 0.40 0.88)
        (gfx-set-font!   painter "sans-serif" 12 #t)
        (gfx-draw-text!  painter (- (/ w 2.0) 210.0) (- h 18.0) hint)))

    ;; Mode indicator — top right
    (gfx-set-color!  painter 0.60 0.60 0.70 0.80)
    (gfx-set-font!   painter "sans-serif" 10)
    (gfx-draw-text!  painter (- w 170.0) 18.0
      (if *disp-mode* "MODE: displacement (∂E/∂t)" "MODE: conduction (J)"))

    (when *paused*
      (gfx-set-color!  painter 1.0 0.75 0.20 0.92)
      (gfx-set-font!   painter "sans-serif" 15 #t)
      (gfx-draw-text!  painter (- (/ w 2.0) 28.0) 26.0 "PAUSED"))))

;;; ════════════════════════════════════════════════════════════════════════════
;;; WINDOW AND SIDEBAR
;;; ════════════════════════════════════════════════════════════════════════════

(define *win*    (make-window "Ampere's Law Explorer" 1040 680))
(define *canvas* (window-canvas *win*))
(define *sb*     (window-sidebar *win*))

;;; ── Heading ──────────────────────────────────────────────────────────────

(box-add! *sb* (make-label "AMPERE'S LAW"))
(box-add! *sb* (make-label ""))
(box-add! *sb* (make-label "  ∇×B = μ₀J + μ₀ε₀ ∂E/∂t"))
(box-add! *sb* (make-label ""))
(box-add! *sb* (make-label "▸ Currents AND changing E"))
(box-add! *sb* (make-label "  both create circulating B."))
(box-add! *sb* (make-label ""))
(box-add! *sb* (make-label "▸ Displacement: B 90° behind E"))
(box-add! *sb* (make-label "  (mirror image of Faraday)."))
(box-add! *sb* (make-label ""))
(box-add! *sb* (make-label "▸ Conduction: B in phase with J"))
(box-add! *sb* (make-label "  — no lag, no minus sign."))
(box-add! *sb* (make-separator))

;;; ── Mode toggle ──────────────────────────────────────────────────────────

(box-add! *sb* (make-label "── Mode ──"))
;; The displacement current (∂E/∂t) term is what Maxwell added.
;; Turn it off to see the classical pre-Maxwell Ampere's law (conduction only).
(box-add! *sb*
  (make-toggle "Displacement current  ∂E/∂t"
    *disp-mode*
    (lambda (on) (set! *disp-mode* on) (canvas-redraw! *canvas*))))
(box-add! *sb* (make-label "(off → conduction current J)"))
(box-add! *sb* (make-separator))

;;; ── Source parameters ────────────────────────────────────────────────────

(box-add! *sb* (make-label "── Source field ──"))
;; E₀: peak E field amplitude (displacement mode).
;; Note: what matters for B is ∂E/∂t = −E₀ω sin(ωt), not E itself.
;; Doubling E₀ doubles ∂E/∂t and therefore doubles B.
(box-add! *sb* (make-label "Peak  E₀  (V/m)  [disp.]"))
(box-add! *sb*
  (make-slider "E0" 0.2 2.0 0.05 *E0*
    (lambda (v) (set! *E0* v) (canvas-redraw! *canvas*))))
;; J₀: peak current density (conduction mode).
(box-add! *sb* (make-label "Peak  J₀  (A/m²)  [cond.]"))
(box-add! *sb*
  (make-slider "J0" 0.2 2.0 0.05 *J0*
    (lambda (v) (set! *J0* v) (canvas-redraw! *canvas*))))
;; ω: angular frequency.
;; In displacement mode: ∂E/∂t = −E₀ω sin(ωt), so higher ω means stronger B
;; even with the same peak E.  Try doubling ω and watch the cyan arrows.
(box-add! *sb* (make-label "Frequency  ω  (rad/s)"))
(box-add! *sb*
  (make-slider "ω" 0.2 3.0 0.05 *omega*
    (lambda (v) (set! *omega* v) (canvas-redraw! *canvas*))))
;; R: source region radius.
(box-add! *sb* (make-label "Source radius  R"))
(box-add! *sb*
  (make-slider "R" 0.3 1.6 0.05 *R*
    (lambda (v) (set! *R* v) (canvas-redraw! *canvas*))))
(box-add! *sb* (make-separator))

;;; ── Integration loop ─────────────────────────────────────────────────────

(box-add! *sb* (make-label "── Integration loop ──"))
;; ∮ B·dl = μ₀ I_enclosed.
;; Drag rₗ across R to see the circulation value saturate once the loop
;; encloses all of the source current.  Identical experiment to the Faraday demo.
(box-add! *sb* (make-label "Loop radius  rₗ"))
(box-add! *sb*
  (make-slider "rloop" 0.3 2.5 0.05 *r-loop*
    (lambda (v) (set! *r-loop* v) (canvas-redraw! *canvas*))))
(box-add! *sb* (make-label "∮B·dl saturates when rₗ ≥ R."))
(box-add! *sb* (make-separator))

;;; ── Display toggles ──────────────────────────────────────────────────────

(box-add! *sb* (make-label "── Display ──"))
(box-add! *sb*
  (make-toggle "Source field region" *show-src*
    (lambda (on) (set! *show-src* on) (canvas-redraw! *canvas*))))
(box-add! *sb*
  (make-toggle "B field arrows" *show-B*
    (lambda (on) (set! *show-B* on) (canvas-redraw! *canvas*))))
(box-add! *sb*
  (make-toggle "Integration loop" *show-loop*
    (lambda (on) (set! *show-loop* on) (canvas-redraw! *canvas*))))
(box-add! *sb* (make-separator))

;;; ── CAS verification ─────────────────────────────────────────────────────
;;;
;;; Mirrors the Faraday CAS check exactly, but verifying Ampere's law.
;;; For the plane wave E_y = cos(x−t), B_z = cos(x−t) (c=1, J=0):
;;;   −∂B_z/∂x = sin(x−t)
;;;   ∂E_y/∂t  = sin(x−t)
;;; The residual is 0 — the law holds for all (x, t).

(box-add! *sb* (make-label "── CAS verification ──"))
(box-add! *sb* (make-label "Plane wave: E_y=B_z=cos(x−t)"))
(box-add! *sb* (make-label ""))
(box-add! *sb* (make-label (string-append "−∂B_z/∂x = " (sym->infix neg-dBz-dx))))
(box-add! *sb* (make-label (string-append "∂E_y/∂t  = " (sym->infix dEy-dt))))
(box-add! *sb* (make-label ""))
(box-add! *sb* (make-label
  (string-append "Residual = "
    (let ((r ampere-cas))
      (if (and (integer? r) (zero? r)) "0   ✓  (all x,t)" (sym->infix r))))))
(box-add! *sb* (make-separator))

;;; ── Guided exercises ─────────────────────────────────────────────────────

(box-add! *sb* (make-label "── Guided exercises ──"))
(box-add! *sb* (make-label "1. Displacement mode: pause when"))
(box-add! *sb* (make-label "   B = 0. What is E_z? ∂E/∂t?"))
(box-add! *sb* (make-label ""))
(box-add! *sb* (make-label "2. Press [m] for conduction mode."))
(box-add! *sb* (make-label "   Pause when B is max. Is J"))
(box-add! *sb* (make-label "   also max, or near zero?"))
(box-add! *sb* (make-label ""))
(box-add! *sb* (make-label "3. Drag rₗ past R in both modes."))
(box-add! *sb* (make-label "   What value does ∮B·dl reach?"))
(box-add! *sb* (make-label ""))
(box-add! *sb* (make-label "4. Displacement mode: double ω."))
(box-add! *sb* (make-label "   Why do the B arrows grow?"))
(box-add! *sb* (make-label "   E₀ unchanged — what changed?"))
(box-add! *sb* (make-separator))

;;; ── Control buttons ──────────────────────────────────────────────────────

(box-add! *sb*
  (make-button "▶  Pause / Resume  [Space]"
    (lambda () (set! *paused* (not *paused*)))))
(box-add! *sb*
  (make-button "→  +¼ period  [→ key]"
    (lambda ()
      (set! *time* (+ *time* (/ pi (* 2.0 *omega*))))
      (canvas-redraw! *canvas*))))
(box-add! *sb*
  (make-button "⇄  Toggle mode  [m]"
    (lambda ()
      (set! *disp-mode* (not *disp-mode*))
      (canvas-redraw! *canvas*))))
(box-add! *sb*
  (make-button "↺  Reset time  [r]"
    (lambda () (set! *time* 0.0) (canvas-redraw! *canvas*))))
(box-add! *sb*
  (make-button "✕  Quit  [q]" quit-event-loop))

;;; ════════════════════════════════════════════════════════════════════════════
;;; EVENT LOOP
;;; ════════════════════════════════════════════════════════════════════════════

(canvas-on-draw! *canvas* draw)

(window-on-key! *win*
  (lambda (key mods)
    (cond
      ((equal? key "space")  (set! *paused* (not *paused*)))
      ((equal? key "r")      (set! *time* 0.0) (canvas-redraw! *canvas*))
      ((equal? key "m")      (set! *disp-mode* (not *disp-mode*))
                             (canvas-redraw! *canvas*))
      ((equal? key "Right")
       (set! *time* (+ *time* (/ pi (* 2.0 *omega*))))
       (canvas-redraw! *canvas*))
      ((equal? key "q")      (quit-event-loop))
      ((equal? key "Escape") (quit-event-loop)))))

(define *timer*
  (make-timer 16
    (lambda ()
      (unless *paused* (set! *time* (+ *time* 0.016)))
      (canvas-redraw! *canvas*))))

(timer-start! *timer*)

(statusbar-set-text! (window-status-bar *win*)
  "Space = pause  ·  → = step ¼ period  ·  m = toggle mode  ·  r = reset  ·  q = quit")

(window-on-close! *win* quit-event-loop)
(window-show! *win*)
(run-event-loop)
