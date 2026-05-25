#!/usr/bin/env curry
;;; maze4d.scm — Wander a 4D maze as a 3D creature.
;;;
;;; The maze has passages in ±x, ±z within each w-slice, plus ±w connections
;;; between slices.  At any moment you see a first-person wireframe view of
;;; the 3D cross-section at your current w.  Press Q/E to step through w —
;;; the maze dissolves and reforms around you.
;;;
;;; Goal: reach cell (DX-1, DZ-1) on w-slice (DW-1).  Yellow star on minimap.
;;;
;;; Controls:
;;;   W / ↑    move forward        Q   step −w (red flash if blocked)
;;;   S / ↓    move back           E   step +w (red flash if blocked)
;;;   A / ←    turn left           Esc quit
;;;   D / →    turn right

(import (curry qt6))

;;; ── Parameters ────────────────────────────────────────────────────────────

(define DX 8)  (define DZ 8)  (define DW 4)

(define WALL-H      1.0)
(define EYE-H       0.5)
(define NEAR-CLIP   0.15)
(define MOVE-SPEED  0.12)
(define TURN-SPEED  0.07)
(define EYE-SEP     0.10)   ; anaglyph inter-ocular distance (world units)
(define MAP-CELL    12)     ; minimap pixels per cell
(define MAP-PAD      8)     ; minimap padding from edge

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

;;; ── Camera & projection ───────────────────────────────────────────────────

; World → camera space.  Forward = (sin yaw, 0, cos yaw).
; Returns #(cx cy cz).
(define (world->cam wx wy wz)
  (let* ((dx (- wx *px*)) (dy (- wy EYE-H)) (dz (- wz *pz*))
         (sy (sin *yaw*)) (cy (cos *yaw*)))
    (vector (- (* dx cy) (* dz sy))
            dy
            (+ (* dx sy) (* dz cy)))))

; Clip line c1→c2 (each a #(cx cy cz)) against near plane.
; Returns #(ax ay az bx by bz) or #f if fully behind.
(define (clip-near c1 c2)
  (let ((az (vector-ref c1 2)) (bz (vector-ref c2 2)))
    (cond
      ((and (<= az NEAR-CLIP) (<= bz NEAR-CLIP)) #f)
      ((and (> az NEAR-CLIP) (> bz NEAR-CLIP))
       (vector (vector-ref c1 0) (vector-ref c1 1) az
               (vector-ref c2 0) (vector-ref c2 1) bz))
      (else
       (let* ((t  (/ (- NEAR-CLIP az) (- bz az)))
              (ax (vector-ref c1 0)) (ay (vector-ref c1 1))
              (bx (vector-ref c2 0)) (by (vector-ref c2 1))
              (ix (+ ax (* t (- bx ax))))
              (iy (+ ay (* t (- by ay)))))
         (if (<= az NEAR-CLIP)
             (vector ix iy NEAR-CLIP bx by bz)
             (vector ax ay az ix iy NEAR-CLIP)))))))

;;; ── Wireframe drawing ─────────────────────────────────────────────────────

(define (draw-line-3d! painter x1 y1 z1 x2 y2 z2 sw sh fov eye-x)
  (let* ((clipped (clip-near (world->cam x1 y1 z1)
                              (world->cam x2 y2 z2))))
    (when clipped
      (let* ((ax (vector-ref clipped 0)) (ay (vector-ref clipped 1)) (az (vector-ref clipped 2))
             (bx (vector-ref clipped 3)) (by (vector-ref clipped 4)) (bz (vector-ref clipped 5))
             (hw (/ sw 2.0)) (hh (/ sh 2.0)))
        (gfx-draw-line! painter
          (+ hw (* fov (/ (+ ax eye-x) az)))
          (- hh (* fov (/ ay az)))
          (+ hw (* fov (/ (+ bx eye-x) bz)))
          (- hh (* fov (/ by bz))))))))

; Draw a wall face: base runs from (x0,z0) to (x1,z1) at floor level, rises WALL-H.
(define (draw-wall! painter x0 z0 x1 z1 sw sh fov eye-x)
  (draw-line-3d! painter x0 0.0    z0 x1 0.0    z1 sw sh fov eye-x)
  (draw-line-3d! painter x0 WALL-H z0 x1 WALL-H z1 sw sh fov eye-x)
  (draw-line-3d! painter x0 0.0    z0 x0 WALL-H z0 sw sh fov eye-x)
  (draw-line-3d! painter x1 0.0    z1 x1 WALL-H z1 sw sh fov eye-x))

; Draw all walls for the current w-slice.
(define (draw-maze-pass! painter sw sh fov eye-x)
  (let ((iw *pw*))
    ; Walls perpendicular to x-axis, at x = k  (k = 0..DX)
    (do ((k 0 (+ k 1))) ((> k DX))
      (do ((iz 0 (+ iz 1))) ((= iz DZ))
        (when (or (= k 0) (= k DX)
                  (not (passage? (- k 1) iz iw B+X)))
          (draw-wall! painter
            (exact->inexact k) (exact->inexact iz)
            (exact->inexact k) (exact->inexact (+ iz 1))
            sw sh fov eye-x))))
    ; Walls perpendicular to z-axis, at z = k  (k = 0..DZ)
    (do ((k 0 (+ k 1))) ((> k DZ))
      (do ((ix 0 (+ ix 1))) ((= ix DX))
        (when (or (= k 0) (= k DZ)
                  (not (passage? ix (- k 1) iw B+Z)))
          (draw-wall! painter
            (exact->inexact ix)       (exact->inexact k)
            (exact->inexact (+ ix 1)) (exact->inexact k)
            sw sh fov eye-x))))))

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
  (gfx-clear! painter 0.02 0.0 0.05)
  (gfx-set-antialias! painter #t)

  (let* ((fov      (/ sw 2.0))
         (half-sep (/ EYE-SEP 2.0)))

    (gfx-set-blend!     painter 'add)
    (gfx-set-pen-width! painter 1.5)

    ; Left eye — red
    (gfx-set-pen-color! painter 1.0 0.0 0.0 0.9)
    (draw-maze-pass! painter sw sh fov (- half-sep))

    ; Right eye — cyan
    (gfx-set-pen-color! painter 0.0 1.0 1.0 0.9)
    (draw-maze-pass! painter sw sh fov half-sep)

    (gfx-set-blend! painter 'src))

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

(define win    (make-window "4D Maze" 960 640))
(define canvas (window-canvas win))

(canvas-on-draw!  canvas draw-frame!)
(window-on-key!   win    handle-key!)
(window-on-close! win    (lambda () (quit-event-loop)))

; 60 fps redraw — movement happens in key handler via OS key-repeat
(define timer (make-timer 16 (lambda () (canvas-redraw! canvas))))

(window-on-realize! win (lambda () (timer-start! timer)))

(window-show! win)
(run-event-loop)
