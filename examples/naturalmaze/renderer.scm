;;; renderer.scm — draw callbacks, HUD, minimap
;;; Requires Qt6 to already be imported.

;;; ── GL init (called from realize callback) ───────────────────────────────

(define (init-gl!)
  (set! *main-prog* (make-gl-shader FRAG-SRC))
  (set! *maze-tex*  (make-gl-texture (maze-bytevec) DX (* DZ DW))))

(define (refresh-maze-tex!)
  (gl-texture-update! *maze-tex* (maze-bytevec)))

;;; ── Main 3-D draw pass ───────────────────────────────────────────────────

(define (draw-3d! painter w h)
  (when *main-prog*
    (gl-shader-draw! *main-prog* painter
      `((u_pos      . ,(list *px* *pz*))
        (u_yaw      . ,*yaw*)
        (u_pitch    . ,*pitch*)
        (u_pw       . ,*pw-vis*)
        (u_time     . ,(inexact (current-second)))
        (u_half_fov . ,0.66)
        (u_wall_h   . ,1.0)
        (u_fog_dist . ,12.0)
        (u_maze     . ,*maze-tex*)
        (u_DX       . ,DX)
        (u_DZ       . ,DZ)
        (u_DW       . ,DW)))))

;;; ── Minimap (QPainter overlay) ───────────────────────────────────────────

(define MAP-CELL  9)    ; px per maze cell
(define MAP-PAD   8)
(define MAP-ALPHA 0.72)

(define (draw-minimap! painter w h)
  (let* ((mw   (* DX MAP-CELL))
         (mh   (* DZ MAP-CELL))
         (ox   (- w mw MAP-PAD))
         (oy   MAP-PAD)
         (iw   *pw*))

    ;; Background
    (gfx-set-color! painter 0.0 0.0 0.08 MAP-ALPHA)
    (gfx-fill-rect! painter ox oy mw mh)

    (gfx-set-pen-width! painter 1.0)

    ;; Walls (X boundaries)
    (do ((k 0 (+ k 1))) ((> k DX))
      (do ((iz 0 (+ iz 1))) ((= iz DZ))
        (when (or (= k 0) (= k DX)
                  (not (passage? (- k 1) iz iw B+X)))
          (let ((px (+ ox (* k MAP-CELL)))
                (py (+ oy (* iz MAP-CELL))))
            (gfx-set-pen-color! painter 0.25 0.45 0.55 1.0)
            (gfx-draw-line! painter px py px (+ py MAP-CELL))))))

    ;; Walls (Z boundaries)
    (do ((k 0 (+ k 1))) ((> k DZ))
      (do ((ix 0 (+ ix 1))) ((= ix DX))
        (when (or (= k 0) (= k DZ)
                  (not (passage? ix (- k 1) iw B+Z)))
          (let ((px (+ ox (* ix MAP-CELL)))
                (py (+ oy (* k MAP-CELL))))
            (gfx-set-pen-color! painter 0.25 0.45 0.55 1.0)
            (gfx-draw-line! painter px py (+ px MAP-CELL) py)))))

    ;; W-rift dots
    (do ((ix 0 (+ ix 1))) ((= ix DX))
      (do ((iz 0 (+ iz 1))) ((= iz DZ))
        (when (has-w-passage-scheme? ix iz iw)
          (let ((cx (+ ox (* ix MAP-CELL) (/ MAP-CELL 2)))
                (cy (+ oy (* iz MAP-CELL) (/ MAP-CELL 2)))
                (pulse (* 0.5 (+ 1.0 (sin (* 3.2 (inexact (current-second))))))))
            (gfx-set-color! painter (* 0.5 (+ 0.5 pulse)) 0.1 1.0 0.8)
            (gfx-fill-circle! painter cx cy 2.5)))))

    ;; Player dot + direction arrow
    (let* ((pcx (+ ox (* *px* MAP-CELL)))
           (pcy (+ oy (* *pz* MAP-CELL)))
           (arr 7.0))
      (gfx-set-color! painter 1.0 0.85 0.2 1.0)
      (gfx-fill-circle! painter pcx pcy 3.5)
      (gfx-set-pen-color! painter 1.0 1.0 0.4 0.9)
      (gfx-draw-line! painter pcx pcy
                      (+ pcx (* arr (sin *yaw*)))
                      (+ pcy (* arr (cos *yaw*)))))

    ;; W-slice label
    (gfx-set-color! painter 0.5 0.7 1.0 0.9)
    (gfx-draw-text! painter (+ ox 3.0) (+ oy mh -3.0)
                    (string-append "W:" (number->string *pw*)))))

(define (has-w-passage-scheme? ix iz iw)
  (not (zero? (bitwise-and (vector-ref *cells* (cidx ix iz iw)) 48))))

;;; ── Crosshair ────────────────────────────────────────────────────────────

(define (draw-crosshair! painter w h)
  (let ((cx (/ w 2.0))
        (cy (/ h 2.0))
        (r   8.0)
        (g   2.0))
    (gfx-set-pen-color! painter 1.0 1.0 1.0 0.55)
    (gfx-set-pen-width! painter 1.5)
    (gfx-draw-line! painter (- cx r) cy (- cx g) cy)
    (gfx-draw-line! painter (+ cx g) cy (+ cx r) cy)
    (gfx-draw-line! painter cx (- cy r) cx (- cy g))
    (gfx-draw-line! painter cx (+ cy g) cx (+ cy r))))

;;; ── W-guide overlay ──────────────────────────────────────────────────────
;;; Shown when the player is standing on a cell with a W-passage.

(define (draw-w-guide! painter w h)
  (let* ((ix (inexact->exact (floor *px*)))
         (iz (inexact->exact (floor *pz*))))
    (when (has-w-passage-scheme? ix iz *pw*)
      (let ((alpha (* 0.5 (+ 1.0 (sin (* 2.5 (inexact (current-second))))))))
        (gfx-set-color! painter 0.55 0.15 1.0 (max 0.15 (* 0.7 alpha)))
        (gfx-draw-text! painter (- (/ w 2.0) 70.0) (- h 55.0)
                        (string-append
                          "W-rift  [Q = W+"
                          (if (< *pw* (- DW 1)) " E = W-" "")
                          "]"))))))

;;; ── W dimension compass strip ─────────────────────────────────────────────

(define (draw-w-strip! painter w h)
  (let* ((strip-w 12.0)
         (strip-h (* DW 18.0))
         (ox      (- w 20.0))
         (oy      (- (/ h 2.0) (/ strip-h 2.0))))
    (gfx-set-color! painter 0.05 0.05 0.12 0.6)
    (gfx-fill-rect! painter ox oy strip-w strip-h)
    (do ((iw 0 (+ iw 1))) ((= iw DW))
      (let* ((cy (+ oy (* (- DW 1 iw) 18.0) 9.0))
             (active (= iw *pw*))
             (alpha  (if active 1.0 0.45)))
        (gfx-set-color! painter
                        (if active 0.55 0.2)
                        (if active 0.15 0.1)
                        (if active 1.0  0.4)
                        alpha)
        (gfx-fill-circle! painter (+ ox (/ strip-w 2.0)) cy (if active 5.0 3.0))))))

;;; ── Top-level draw ────────────────────────────────────────────────────────

(define (draw-frame! painter w h)
  (draw-3d!        painter w h)
  (draw-minimap!   painter w h)
  (draw-crosshair! painter w h)
  (draw-w-guide!   painter w h)
  (draw-w-strip!   painter w h))
