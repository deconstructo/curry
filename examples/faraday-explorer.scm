;;; faraday-explorer.scm  — v1.0
;;;
;;; Interactive workbook for Faraday's Law of Electromagnetic Induction:
;;;
;;;   ∇ × E = −∂B/∂t
;;;
;;; A cylindrical solenoid region of oscillating B_z drives a circulating
;;; induced electric field E.  The exact analytic solution is used:
;;;
;;;   Inside  (r < R):  E_φ = −(r/2) ∂B/∂t
;;;   Outside (r ≥ R):  E_φ = −(R²/2r) ∂B/∂t
;;;
;;; Key teaching points:
;;;   • E is ZERO when B is at its peak — only the RATE of change matters
;;;   • E is MAXIMUM when B is changing fastest (crossing zero)
;;;   • The ¼-period button lets students step through these phases
;;;   • The integration loop shows EMF = −dΦ/dt live
;;;   • The CAS panel verifies the plane-wave form symbolically
;;;
;;; Controls:
;;;   [Space]         pause / resume
;;;   [→ ¼ period]   step one quarter period forward
;;;   [r]             reset time to 0
;;;   [q / Escape]    quit
;;;
;;; Run:  ./build/curry examples/faraday-explorer.scm

(import (curry qt6))
(import (scheme base))
(import (scheme inexact))
(import (scheme write))

;;; ═══ Symbolic CAS — plane-wave verification ══════════════════════════════════

;; For E_y = cos(x−t), B_z = cos(x−t) with c = 1:
;;   Faraday (z-component):  ∂E_y/∂x − ∂E_x/∂y = −∂B_z/∂t
;;   E_x = 0, so this reduces to ∂E_y/∂x = −∂B_z/∂t

(symbolic x t)

(define dEy-dx      (simplify (∂ (cos (- x t)) x)))   ; −sin(x−t)
(define neg-dBz-dt  (simplify (- (∂ (cos (- x t)) t)))) ; −sin(x−t) also
(define faraday-cas (simplify (- dEy-dx neg-dBz-dt)))  ; 0

;;; ═══ Constants ═══════════════════════════════════════════════════════════════

(define pi 3.14159265358979323846)

;;; ═══ Mutable world state ══════════════════════════════════════════════════════

(define *B0*        1.0)   ; peak B field (T)
(define *omega*     1.0)   ; angular frequency (rad/s)
(define *R*         0.9)   ; solenoid radius (world units)
(define *r-loop*    1.5)   ; integration loop radius (world units)
(define *time*      0.0)   ; simulation time (s)
(define *paused*    #f)
(define *show-B*    #t)
(define *show-E*    #t)
(define *show-loop* #t)

;;; ═══ Field equations ══════════════════════════════════════════════════════════

(define (Bz   tv) (* *B0* (cos (* *omega* tv))))
(define (dBdt tv) (* (- *B0*) *omega* (sin (* *omega* tv))))

;; Induced E at world point (wx, wy) given ∂B/∂t = db and solenoid radius R.
;; Returns (Ex . Ey) in Cartesian components.
(define (E-at wx wy db R)
  (let* ((r2  (+ (* wx wx) (* wy wy)))
         (r   (sqrt r2)))
    (if (< r 0.05)
        (cons 0.0 0.0)
        (let* ((ephi (if (< r R)
                         (* -0.5 r  db)          ; inside
                         (* -0.5 R R (/ db r)))) ; outside
               (ex   (* (- (/ wy r)) ephi))       ; ê_φ = (−y/r, x/r)
               (ey   (* (/ wx r)     ephi)))
          (cons ex ey)))))

;;; ═══ Number formatting ════════════════════════════════════════════════════════

(define (fmt2 v)   ; 2 decimal places
  (number->string (/ (round (* v 100.0)) 100.0)))

(define (fmt3 v)   ; 3 decimal places
  (number->string (/ (round (* v 1000.0)) 1000.0)))

;;; ═══ Drawing helpers ══════════════════════════════════════════════════════════

;; Arrow from (x1,y1) to (x2,y2) with filled head scaled to shaft length.
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

;;; ═══ Draw callback ═══════════════════════════════════════════════════════════

;; E-field arrow grid: nx × ny over ±xw × ±yw world units
(define nx 20) (define ny 16)
(define xw 3.1) (define yw 2.5)

(define (draw painter w h)
  (let* ((tv   *time*)
         (bz   (Bz tv))
         (db   (dBdt tv))
         (cx   (/ w 2.0))
         (cy   (/ h 2.0))
         (sc   (/ (min w h) 5.8))  ; pixels per world unit
         (Rpx  (* *R*     sc))
         (Rlpx (* *r-loop* sc))
         (gdx  (/ (* 2.0 xw) (- nx 1)))
         (gdy  (/ (* 2.0 yw) (- ny 1)))
         ;; max arrow length = 40% of grid cell, in world units
         (amax (* (min gdx gdy) 0.40)))

    (gfx-clear! painter 0.06 0.06 0.10)
    (gfx-set-antialias! painter #t)

    ;; ── Faint grid ─────────────────────────────────────────────────────────
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

    ;; ── Axes ──────────────────────────────────────────────────────────────
    (gfx-set-pen-color! painter 0.28 0.28 0.35 0.9)
    (gfx-set-pen-width! painter 1.0)
    (gfx-draw-line! painter (- cx (* xw sc)) cy (+ cx (* xw sc)) cy)
    (gfx-draw-line! painter cx (- cy (* yw sc)) cx (+ cy (* yw sc)))
    (gfx-set-color! painter 0.32 0.32 0.40 0.75)
    (gfx-set-font!  painter "sans-serif" 9)
    (gfx-draw-text! painter (+ cx (* xw sc) 3) (+ cy 4) "x")
    (gfx-draw-text! painter (+ cx 4) (- cy (* yw sc) 8) "y")

    ;; ── B region (solenoid cross-section) ─────────────────────────────────
    (when *show-B*
      (let* ((frac  (min 1.0 (abs bz)))
             ;; Blue = out of page (+), red = into page (−)
             (r-col (if (< bz 0.0) (* 0.85 frac) (* 0.12 frac)))
             (g-col (* 0.14 frac))
             (b-col (if (> bz 0.0) (* 0.85 frac) (* 0.12 frac))))
        (gfx-set-color! painter r-col g-col b-col (* 0.42 frac))
        (gfx-fill-circle! painter cx cy Rpx)
        (gfx-set-pen-color! painter (+ r-col 0.2) g-col (+ b-col 0.2)
                            (min 1.0 (* 0.9 (max 0.15 frac))))
        (gfx-set-pen-width! painter 2.0)
        (gfx-draw-circle! painter cx cy Rpx)
        ;; Direction symbol  ⊙ = out of page, ⊗ = into page
        (gfx-set-color! painter (+ r-col 0.35) (+ g-col 0.35) (+ b-col 0.45) 0.92)
        (gfx-set-font!  painter "sans-serif" 20)
        (gfx-draw-text! painter (- cx 10) (+ cy 8)
          (if (>= bz 0.0) "⊙" "⊗"))
        ;; B value label
        (gfx-set-font! painter "monospace" 9)
        (gfx-draw-text! painter (- cx 22) (+ cy 26)
          (string-append "B = " (fmt2 bz) " T"))))

    ;; ── E field arrows (grid) ─────────────────────────────────────────────
    (when *show-E*
      ;; First pass: find maximum E magnitude for normalization
      (let* ((emax 1e-10))
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
        ;; Second pass: draw
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
                  (when (> em (* emax 0.025))
                    (let* ((norm (/ em emax))
                           (len  (* amax norm sc))   ; pixels
                           (ux   (/ ex em))
                           (uy   (- (/ ey em)))       ; flip y for screen
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

    ;; ── Integration loop ──────────────────────────────────────────────────
    (when *show-loop*
      ;; Loop circle
      (gfx-set-pen-color! painter 0.88 0.88 1.0 0.60)
      (gfx-set-pen-width! painter 1.6)
      (gfx-draw-circle! painter cx cy Rlpx)
      ;; Circulation direction arrow on the loop at φ=45°
      ;; CW tangent at world angle φ in screen coords: (sin φ, cos φ)
      ;; dBdt>0 → CW; dBdt<0 → CCW
      (when (> (abs db) (* *B0* *omega* 0.05))
        (let* ((phi  (/ pi 4.0))
               (dir  (if (> db 0.0) 1.0 -1.0))
               (lx   (+ cx (* Rlpx (cos phi))))
               (ly   (- cy (* Rlpx (sin phi))))
               (cwx  (sin phi))    ; CW screen tangent x
               (cwy  (cos phi))    ; CW screen tangent y (y-down)
               (alen 20.0))
          (arrow! painter
                  (- lx (* cwx dir alen 0.5))
                  (- ly (* cwy dir alen 0.5))
                  (+ lx (* cwx dir alen 0.5))
                  (+ ly (* cwy dir alen 0.5))
                  0.88 0.88 1.0 0.80 2.0)))
      ;; EMF label  (Φ = B_z · π · min(R,r_loop)²)
      (let* ((reff  (min *r-loop* *R*))
             (flux  (* bz pi reff reff))
             (emf   (* (- pi reff reff) db))
             (rlabel (string-append "EMF = " (fmt2 emf) " V")))
        (gfx-set-color! painter 0.88 0.88 1.0 0.82)
        (gfx-set-font!  painter "monospace" 10)
        (gfx-draw-text! painter (+ cx Rlpx 7) (- cy 6) rlabel))
      ;; Loop radius label
      (gfx-set-color! painter 0.60 0.60 0.80 0.60)
      (gfx-set-font!  painter "sans-serif" 9)
      (gfx-draw-text! painter (+ cx Rlpx 3) (+ cy 5) "rₗ"))

    ;; R label (solenoid radius)
    (gfx-set-color! painter 0.55 0.55 0.65 0.65)
    (gfx-set-font!  painter "sans-serif" 9)
    (gfx-draw-text! painter (+ cx Rpx 4) (- cy 4) "R")

    ;; ── Live values panel (top-left of canvas) ────────────────────────────
    (let* ((px 12.0) (py 12.0) (lh 18) (pw 258))
      (gfx-set-color! painter 0.02 0.02 0.04 0.70)
      (gfx-fill-rect! painter px py pw (* 6.8 lh))
      (gfx-set-font!  painter "monospace" 11)
      ;; Time
      (gfx-set-color! painter 0.75 0.75 0.75 0.95)
      (gfx-draw-text! painter (+ px 8) (+ py lh)
        (string-append "t        = " (fmt2 *time*) " s"))
      ;; B_z  (colour-coded: blue out, red in)
      (let ((c (if (>= bz 0.0)
                   (list 0.35 0.55 1.0)
                   (list 1.0 0.35 0.35))))
        (gfx-set-color! painter (car c) (cadr c) (caddr c) 0.95))
      (gfx-draw-text! painter (+ px 8) (+ py (* 2 lh))
        (string-append "B_z      = " (fmt2 bz) " T"
          (if (>= bz 0.0) "  ⊙ out" "  ⊗ in")))
      ;; ∂B/∂t  (yellow)
      (gfx-set-color! painter 0.96 0.96 0.40 0.95)
      (gfx-draw-text! painter (+ px 8) (+ py (* 3 lh))
        (string-append "∂B/∂t    = " (fmt2 db) " T/s"))
      ;; E_φ at r_loop
      (let* ((ev   (E-at *r-loop* 0.0 db *R*))
             (ephi (cdr ev)))                ; E_y at (r_loop, 0) = E_phi at φ=0
        (gfx-set-color! painter 0.28 0.96 0.40 0.95)
        (gfx-draw-text! painter (+ px 8) (+ py (* 4 lh))
          (string-append "E_φ(rₗ)  = " (fmt3 ephi) " V/m")))
      ;; (∇×E)_z  — inside solenoid = −∂B/∂t
      (gfx-set-color! painter 0.68 0.68 1.0 0.95)
      (gfx-draw-text! painter (+ px 8) (+ py (* 5 lh))
        (string-append "(∇×E)_z  = " (fmt2 (- db)) " s⁻¹"))
      ;; Verification tick
      (gfx-set-color! painter 0.50 0.50 0.55 0.80)
      (gfx-draw-text! painter (+ px 8) (+ py (* 6.3 lh))
        "∇×E = −∂B/∂t   ✓"))

    ;; ── Phase hint (bottom of canvas) ─────────────────────────────────────
    (let* ((hint (cond
                   ((< (abs db) (* *B0* *omega* 0.08))
                    "B at extremum — ∂B/∂t ≈ 0  →  E = 0")
                   ((> (abs bz) (* *B0* 0.92))
                    "")
                   ((> (abs db) (* *B0* *omega* 0.92))
                    "B crossing zero — |∂B/∂t| max  →  |E| max")
                   (else ""))))
      (when (> (string-length hint) 0)
        (gfx-set-color!  painter 1.0 0.90 0.40 0.88)
        (gfx-set-font!   painter "sans-serif" 12 #t)
        (gfx-draw-text!  painter (- (/ w 2.0) 170.0) (- h 18.0) hint)))

    ;; ── Pause indicator ────────────────────────────────────────────────────
    (when *paused*
      (gfx-set-color!  painter 1.0 0.75 0.20 0.92)
      (gfx-set-font!   painter "sans-serif" 15 #t)
      (gfx-draw-text!  painter (- (/ w 2.0) 28.0) 26.0 "PAUSED"))))

;;; ═══ Window ══════════════════════════════════════════════════════════════════

(define *win*    (make-window "Faraday's Law Explorer" 1040 680))
(define *canvas* (window-canvas *win*))
(define *sb*     (window-sidebar *win*))

;;; ═══ Sidebar ═════════════════════════════════════════════════════════════════

;;; Header
(box-add! *sb* (make-label "FARADAY'S LAW"))
(box-add! *sb* (make-label ""))
(box-add! *sb* (make-label "   ∇ × E = −∂B/∂t"))
(box-add! *sb* (make-label ""))

;;; Physical description
(box-add! *sb* (make-label "▸ A CHANGING B field creates"))
(box-add! *sb* (make-label "  a circulating E field."))
(box-add! *sb* (make-label ""))
(box-add! *sb* (make-label "▸ Strength ∝ |∂B/∂t|, not |B|."))
(box-add! *sb* (make-label "  E = 0 when B is at its peak!"))
(box-add! *sb* (make-label ""))
(box-add! *sb* (make-label "▸ Direction obeys Lenz's law:"))
(box-add! *sb* (make-label "  induced E opposes the change."))
(box-add! *sb* (make-separator))

;;; B field controls
(box-add! *sb* (make-label "── B field ──"))
(box-add! *sb* (make-label "Peak  B₀  (T)"))
(box-add! *sb*
  (make-slider "B0" 0.2 2.0 0.05 *B0*
    (lambda (v) (set! *B0* v) (canvas-redraw! *canvas*))))
(box-add! *sb* (make-label "Frequency  ω  (rad/s)"))
(box-add! *sb*
  (make-slider "ω" 0.2 3.0 0.05 *omega*
    (lambda (v) (set! *omega* v) (canvas-redraw! *canvas*))))
(box-add! *sb* (make-label "Solenoid radius  R"))
(box-add! *sb*
  (make-slider "R" 0.3 1.6 0.05 *R*
    (lambda (v) (set! *R* v) (canvas-redraw! *canvas*))))
(box-add! *sb* (make-separator))

;;; Integration loop
(box-add! *sb* (make-label "── Integration loop ──"))
(box-add! *sb* (make-label "Loop radius  rₗ"))
(box-add! *sb*
  (make-slider "rloop" 0.3 2.5 0.05 *r-loop*
    (lambda (v) (set! *r-loop* v) (canvas-redraw! *canvas*))))
(box-add! *sb* (make-label "Try: rₗ inside R, then outside."))
(box-add! *sb* (make-separator))

;;; Display toggles
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

;;; CAS verification
(box-add! *sb* (make-label "── CAS check (c = 1 plane wave) ──"))
(box-add! *sb* (make-label "E_y = cos(x − t)"))
(box-add! *sb* (make-label "B_z = cos(x − t)"))
(box-add! *sb* (make-label ""))
(box-add! *sb* (make-label (string-append "∂E_y/∂x  = " (sym->infix dEy-dx))))
(box-add! *sb* (make-label (string-append "−∂B_z/∂t = " (sym->infix neg-dBz-dt))))
(box-add! *sb* (make-label ""))
(box-add! *sb* (make-label
  (string-append "∂E_y/∂x − (−∂B_z/∂t) = "
    (let ((r faraday-cas))
      (if (and (integer? r) (zero? r)) "0  ✓" (sym->infix r))))))
(box-add! *sb* (make-separator))

;;; Exploration tips
(box-add! *sb* (make-label "── Explore ──"))
(box-add! *sb* (make-label "1. Pause at E=0.  Note B is max."))
(box-add! *sb* (make-label "2. Step ¼ period: E jumps to max,"))
(box-add! *sb* (make-label "   B crosses zero. Why?"))
(box-add! *sb* (make-label "3. Move rₗ inside R: EMF ∝ r²."))
(box-add! *sb* (make-label "4. Double ω: E doubles.  B same."))
(box-add! *sb* (make-separator))

;;; Control buttons
(box-add! *sb*
  (make-button "▶  Pause / Resume  [Space]"
    (lambda () (set! *paused* (not *paused*)))))
(box-add! *sb*
  (make-button "→  +¼ period"
    (lambda ()
      (set! *time* (+ *time* (/ pi (* 2.0 *omega*))))
      (canvas-redraw! *canvas*))))
(box-add! *sb*
  (make-button "↺  Reset time  [r]"
    (lambda () (set! *time* 0.0) (canvas-redraw! *canvas*))))
(box-add! *sb*
  (make-button "✕  Quit  [q]" quit-event-loop))

;;; ═══ Canvas + keyboard + timer ════════════════════════════════════════════════

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

(define *timer*
  (make-timer 16
    (lambda ()
      (unless *paused*
        (set! *time* (+ *time* 0.016)))
      (canvas-redraw! *canvas*))))

(timer-start! *timer*)

(statusbar-set-text! (window-status-bar *win*)
  "Space = pause  ·  → or [+¼] = step quarter period  ·  r = reset  ·  q = quit")

(window-on-close! *win* quit-event-loop)
(window-show! *win*)
(run-event-loop)
