#!/usr/bin/env curry
;;; maze4d.scm — Wander a 4D maze as a 3D creature.
;;;
;;; The maze has passages in ±x, ±z within each w-slice, plus ±w connections
;;; between slices.  At any moment you see a first-person view (Doom-style
;;; column raycaster) of the 3D cross-section at your current w.
;;; Press Q/E to step through w — the maze instantly resets around you.
;;;
;;; Goal: reach cell (DX-1, DZ-1) on w-slice (DW-1).  Yellow dot on minimap.
;;;
;;; Controls:
;;;   W / ↑    move forward        Q   step −w
;;;   S / ↓    move back           E   step +w
;;;   A / ←    turn left           Esc quit
;;;   D / →    turn right

(import (curry qt6))

;;; ── Parameters ────────────────────────────────────────────────────────────

(define DX 8)  (define DZ 8)  (define DW 4)

(define WALL-H      1.0)   ; wall height in world units
(define MOVE-SPEED  0.12)
(define TURN-SPEED  0.07)
(define *eye-sep*   0.10)  ; anaglyph inter-ocular distance (world units)
(define HALF-FOV    0.66)  ; camera-plane half-width (≈65° FOV)
(define COLUMN-W    3)     ; screen pixels per raycaster column
(define FOG-DIST    7.0)
(define MAP-CELL    12)
(define MAP-PAD      8)

;;; ── Random (LCG) ──────────────────────────────────────────────────────────

(define *lcg* (inexact->exact (floor (current-second))))

(define (lcg-next!)
  (set! *lcg* (remainder (+ (* *lcg* 1664525) 1013904223) 4294967296))
  *lcg*)

(define (rand-int n) (abs (remainder (lcg-next!) n)))

;;; ── Maze ──────────────────────────────────────────────────────────────────

(define B+X  1) (define B-X  2)
(define B+Z  4) (define B-Z  8)
(define B+W 16) (define B-W 32)

; (my-bit opp-bit delta-x delta-z delta-w)
(define *DIRS*
  (vector (vector B+X B-X  1  0  0)
          (vector B-X B+X -1  0  0)
          (vector B+Z B-Z  0  1  0)
          (vector B-Z B+Z  0 -1  0)
          (vector B+W B-W  0  0  1)
          (vector B-W B+W  0  0 -1)))

(define *cells* (make-vector (* DX DZ DW) 0))
(define *seen*  (make-vector (* DX DZ DW) #f))

(define (cidx ix iz iw) (+ (* iw DX DZ) (* iz DX) ix))

(define (in-bounds? ix iz iw)
  (and (< -1 ix DX) (< -1 iz DZ) (< -1 iw DW)))

(define (open-bit! idx bit)
  (vector-set! *cells* idx (bitwise-or (vector-ref *cells* idx) bit)))

(define (passage? ix iz iw bit)
  (and (in-bounds? ix iz iw)
       (not (zero? (bitwise-and (vector-ref *cells* (cidx ix iz iw)) bit)))))

(define (shuffled-dirs)
  (let ((v (vector-copy *DIRS*)))
    (do ((i 5 (- i 1))) ((= i 0) v)
      (let* ((j (rand-int (+ i 1))) (t (vector-ref v i)))
        (vector-set! v i (vector-ref v j))
        (vector-set! v j t)))))

; Iterative recursive backtracker
(define (generate-maze!)
  (let ((stack (list (vector 0 0 0))))
    (vector-set! *seen* 0 #t)
    (let loop ()
      (when (pair? stack)
        (let* ((top  (car stack))
               (ix   (vector-ref top 0))
               (iz   (vector-ref top 1))
               (iw   (vector-ref top 2))
               (dirs (shuffled-dirs))
               (next #f))
          (let find ((i 0))
            (when (and (< i 6) (not next))
              (let* ((d  (vector-ref dirs i))
                     (nx (+ ix (vector-ref d 2)))
                     (nz (+ iz (vector-ref d 3)))
                     (nw (+ iw (vector-ref d 4))))
                (if (and (in-bounds? nx nz nw)
                         (not (vector-ref *seen* (cidx nx nz nw))))
                    (set! next (vector d nx nz nw))
                    (find (+ i 1))))))
          (if next
              (let* ((d  (vector-ref next 0))
                     (nx (vector-ref next 1))
                     (nz (vector-ref next 2))
                     (nw (vector-ref next 3)))
                (open-bit! (cidx ix iz iw) (vector-ref d 0))
                (open-bit! (cidx nx nz nw) (vector-ref d 1))
                (vector-set! *seen* (cidx nx nz nw) #t)
                (set! stack (cons (vector nx nz nw) stack)))
              (set! stack (cdr stack)))
          (loop))))))

(generate-maze!)

;;; ── Player state ──────────────────────────────────────────────────────────

(define *px*  0.5)
(define *pz*  0.5)
(define *pw*  0)
(define *yaw* 0.0)

(define (player-ix) (inexact->exact (floor *px*)))
(define (player-iz) (inexact->exact (floor *pz*)))

;;; ── Movement ──────────────────────────────────────────────────────────────

; Try each axis independently for wall-sliding behaviour.
(define (try-move! dx dz)
  (let* ((nx  (+ *px* dx))
         (nix (inexact->exact (floor nx)))
         (oix (player-ix)))
    (if (= nix oix)
        (set! *px* nx)
        (when (passage? oix (player-iz) *pw* (if (> nix oix) B+X B-X))
          (set! *px* nx))))
  (let* ((nz  (+ *pz* dz))
         (niz (inexact->exact (floor nz)))
         (oiz (player-iz)))
    (if (= niz oiz)
        (set! *pz* nz)
        (when (passage? (player-ix) oiz *pw* (if (> niz oiz) B+Z B-Z))
          (set! *pz* nz)))))

(define (try-step-w! dw)
  (let ((nw (+ *pw* dw))
        (bit (if (> dw 0) B+W B-W)))
    (and (in-bounds? (player-ix) (player-iz) nw)
         (passage? (player-ix) (player-iz) *pw* bit)
         (begin (set! *pw* nw) #t))))

;;; ── Raycaster ─────────────────────────────────────────────────────────────

; DDA raycaster. Returns (perp-dist x-side?) or #f if no wall within range.
; px/pz: eye position; cam: camera-plane x in [-1, 1].
; Uses the perpendicular-distance formula (Lode's method) to avoid fisheye.
(define (cast-ray px pz cam)
  (let* ((sy  (sin *yaw*)) (cy (cos *yaw*))
         ; Ray direction = view_dir + cam * camera_plane_dir
         ; View dir = (sy, cy), camera plane dir (rightward) = (cy, -sy)
         (rdx (+ sy (* cy cam HALF-FOV)))
         (rdz (- cy (* sy cam HALF-FOV)))
         (ddx (if (= rdx 0.0) 1e30 (abs (/ 1.0 rdx))))
         (ddz (if (= rdz 0.0) 1e30 (abs (/ 1.0 rdz))))
         (ix  (inexact->exact (floor px)))
         (iz  (inexact->exact (floor pz)))
         (sx  (if (> rdx 0) 1 -1))
         (sz  (if (> rdz 0) 1 -1))
         ; Initial side distances: distance along ray to first boundary crossing
         (sdx (if (> rdx 0) (* (- (+ ix 1.0) px) ddx) (* (- px ix) ddx)))
         (sdz (if (> rdz 0) (* (- (+ iz 1.0) pz) ddz) (* (- pz iz) ddz))))
    (let loop ((ix ix) (iz iz) (sdx sdx) (sdz sdz))
      (cond
        ((> (min sdx sdz) 18.0) #f)
        ; X boundary is closer — check for wall in step direction
        ((< sdx sdz)
         (if (not (passage? ix iz *pw* (if (> sx 0) B+X B-X)))
             (list (max 0.01 (- sdx ddx)) #t)   ; perp-dist, x-side
             (loop (+ ix sx) iz (+ sdx ddx) sdz)))
        ; Z boundary is closer
        (else
         (if (not (passage? ix iz *pw* (if (> sz 0) B+Z B-Z)))
             (list (max 0.01 (- sdz ddz)) #f)   ; perp-dist, z-side
             (loop ix (+ iz sz) sdx (+ sdz ddz))))))))

; Render one eye as Doom-style vertical column strips.
; er eg eb = eye colour channel (red eye: 1 0 0 / blue eye: 0 0 1).
; X-walls are full brightness; Z-walls are 70% — distinguishes wall orientation.
(define (draw-doom-pass! painter sw sh px pz er eg eb)
  (let* ((sw-f  (exact->inexact sw))
         (sh-f  (exact->inexact sh))
         (hsh   (/ sh-f 2.0))
         (ncols (quotient sw COLUMN-W)))
    (do ((ci 0 (+ ci 1))) ((= ci ncols))
      (let* ((cx  (exact->inexact (+ (* ci COLUMN-W) (quotient COLUMN-W 2))))
             (cam (- (* 2.0 (/ cx sw-f)) 1.0))
             (hit (cast-ray px pz cam)))
        (when hit
          (let* ((perp    (car hit))
                 (x-side? (cadr hit))
                 (half-h  (/ (* WALL-H hsh) perp))
                 (top     (max 0.0 (- hsh half-h)))
                 (bot     (min sh-f (+ hsh half-h)))
                 (fog     (max 0.0 (- 1.0 (/ perp FOG-DIST))))
                 (b       (* fog (if x-side? 1.0 0.7))))
            (gfx-set-color! painter (* er b) (* eg b) (* eb b) 1.0)
            (gfx-fill-rect! painter (exact->inexact (* ci COLUMN-W))
                            top (exact->inexact COLUMN-W) (- bot top))))))))

;;; ── HUD: minimap ──────────────────────────────────────────────────────────

(define (draw-minimap! painter sw sh)
  (let* ((mw  (* DX MAP-CELL))
         (mh  (* DZ MAP-CELL))
         (ox  (- sw mw MAP-PAD))
         (oy  MAP-PAD)
         (iw  *pw*))
    ; Background
    (gfx-set-color! painter 0.0 0.0 0.15 0.75)
    (gfx-fill-rect! painter ox oy mw mh)
    (gfx-set-pen-color! painter 0.4 0.5 1.0 1.0)
    (gfx-set-pen-width! painter 1.0)
    ; x-aligned walls at x = k
    (do ((k 0 (+ k 1))) ((> k DX))
      (do ((iz 0 (+ iz 1))) ((= iz DZ))
        (when (or (= k 0) (= k DX) (not (passage? (- k 1) iz iw B+X)))
          (let ((px (+ ox (* k MAP-CELL))) (py (+ oy (* iz MAP-CELL))))
            (gfx-draw-line! painter px py px (+ py MAP-CELL))))))
    ; z-aligned walls at z = k
    (do ((k 0 (+ k 1))) ((> k DZ))
      (do ((ix 0 (+ ix 1))) ((= ix DX))
        (when (or (= k 0) (= k DZ) (not (passage? ix (- k 1) iw B+Z)))
          (let ((px (+ ox (* ix MAP-CELL))) (py (+ oy (* k MAP-CELL))))
            (gfx-draw-line! painter px py (+ px MAP-CELL) py)))))
    ; Goal star (only visible on final w-slice)
    (when (= iw (- DW 1))
      (gfx-set-color! painter 1.0 0.9 0.1 1.0)
      (gfx-fill-circle! painter
        (+ ox (* (- DX 0.5) MAP-CELL))
        (+ oy (* (- DZ 0.5) MAP-CELL))
        4.5))
    ; Player dot + direction tick
    (let* ((pcx (+ ox (* *px* MAP-CELL)))
           (pcy (+ oy (* *pz* MAP-CELL))))
      (gfx-set-color! painter 1.0 0.3 0.3 1.0)
      (gfx-fill-circle! painter pcx pcy 4.5)
      (gfx-set-pen-color! painter 1.0 0.6 0.6 1.0)
      (gfx-draw-line! painter pcx pcy
        (+ pcx (* 9.0 (sin *yaw*)))
        (+ pcy (* 9.0 (cos *yaw*)))))))

;;; ── HUD: w-level indicator ────────────────────────────────────────────────

(define (draw-w-indicator! painter)
  (let ((y 14.0) (r 6.0) (gap 18.0) (x0 16.0))
    (do ((i 0 (+ i 1))) ((= i DW))
      (let ((cx (+ x0 (* i gap))))
        (if (= i *pw*)
            (begin (gfx-set-color! painter 0.2 1.0 0.5 1.0)
                   (gfx-fill-circle! painter cx y r))
            (begin (gfx-set-color! painter 0.1 0.3 0.2 0.9)
                   (gfx-fill-circle! painter cx y r)))))))

;;; ── Win check ─────────────────────────────────────────────────────────────

(define (at-goal?)
  (and (= (player-ix) (- DX 1))
       (= (player-iz) (- DZ 1))
       (= *pw* (- DW 1))))

;;; ── Main draw ─────────────────────────────────────────────────────────────

(define (draw-frame! painter sw sh)
  (let* ((sy       (sin *yaw*))
         (cy       (cos *yaw*))
         (half-sep (/ *eye-sep* 2.0))
         ; Eye positions, offset laterally ⊥ to view direction.
         ; Right direction in (x,z) = (cy, -sy).
         (lpx (- *px* (* half-sep cy)))
         (lpz (+ *pz* (* half-sep sy)))
         (rpx (+ *px* (* half-sep cy)))
         (rpz (- *pz* (* half-sep sy)))
         (sw-f (exact->inexact sw))
         (sh-f (exact->inexact sh))
         (hsh  (/ sh-f 2.0)))

    ; Ceiling and floor background drawn once in normal blend
    (gfx-set-blend! painter 'src)
    (gfx-clear! painter 0.0 0.0 0.0)
    (gfx-set-color! painter 0.04 0.03 0.06 1.0)   ; dark ceiling
    (gfx-fill-rect! painter 0.0 0.0 sw-f hsh)
    (gfx-set-color! painter 0.09 0.08 0.06 1.0)   ; dark floor (slightly warmer)
    (gfx-fill-rect! painter 0.0 hsh sw-f hsh)

    ; Walls: additive blend for red/blue anaglyph
    (gfx-set-blend! painter 'add)
    (draw-doom-pass! painter sw sh lpx lpz 1.0 0.0 0.0)   ; left eye  — red
    (draw-doom-pass! painter sw sh rpx rpz 0.0 0.0 1.0)   ; right eye — blue
    (gfx-set-blend! painter 'src))

  ; HUD always in normal blend
  (draw-minimap!     painter sw sh)
  (draw-w-indicator! painter)

  (when (at-goal?)
    (gfx-set-color! painter 1.0 1.0 0.0 0.95)
    (gfx-draw-text! painter (/ sw 2.0) (/ sh 2.0) "YOU ESCAPED THE 4D MAZE")))

;;; ── Input ─────────────────────────────────────────────────────────────────

(define (handle-key! key mods)
  (let* ((sy (sin *yaw*)) (cy (cos *yaw*)))
    (cond
      ((or (equal? key "w") (equal? key "Up"))
       (try-move! (* MOVE-SPEED sy) (* MOVE-SPEED cy)))
      ((or (equal? key "s") (equal? key "Down"))
       (try-move! (- (* MOVE-SPEED sy)) (- (* MOVE-SPEED cy))))
      ((or (equal? key "a") (equal? key "Left"))
       (set! *yaw* (- *yaw* TURN-SPEED)))
      ((or (equal? key "d") (equal? key "Right"))
       (set! *yaw* (+ *yaw* TURN-SPEED)))
      ((equal? key "q") (try-step-w! -1))
      ((equal? key "e") (try-step-w!  1))
      ((or (equal? key "Escape") (equal? key "Q"))
       (quit-event-loop)))))

;;; ── Window ────────────────────────────────────────────────────────────────

(define win     (make-window "4D Maze" 1080 640))
(define canvas  (window-canvas win))
(define sidebar (window-sidebar win))

(box-add! sidebar (make-label "Stereo (red/blue)"))
(box-add! sidebar
  (make-slider "Eye separation" 0 50 1 10
    (lambda (v) (set! *eye-sep* (* v 0.01)))))
(box-add! sidebar (make-separator))
(box-add! sidebar (make-label "Controls"))
(box-add! sidebar (make-label "W/S/↑/↓  move"))
(box-add! sidebar (make-label "A/D/←/→  turn"))
(box-add! sidebar (make-label "Q/E      step ±w"))

(canvas-on-draw!  canvas draw-frame!)
(window-on-key!   win    handle-key!)
(window-on-close! win    (lambda () (quit-event-loop)))

; 60 fps redraw — movement happens in key handler via OS key-repeat
(define timer (make-timer 16 (lambda () (canvas-redraw! canvas))))

(window-on-realize! win (lambda () (timer-start! timer)))

(window-show! win)
(run-event-loop)
