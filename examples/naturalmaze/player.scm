;;; player.scm — player state, movement, input tracking
;;; No Qt6 dependency. Compile with: curry -c player.scm

;;; ── Constants ────────────────────────────────────────────────────────────

(define MOVE-SPEED    3.2)    ; world units / second
(define TURN-SPEED    2.0)    ; radians / second (keyboard turn)
(define MOUSE-SENS-H  0.0025) ; radians / pixel (yaw)
(define MOUSE-SENS-V  0.0020) ; radians / pixel (pitch)
(define PITCH-MAX     1.15)   ; ~66°
(define PLAYER-R      0.28)   ; collision radius
(define W-SPEED       2.0)    ; W-units / second for smooth transition
(define W-VIS-SMOOTH  10.0)   ; visual W lerp rate

;;; ── State ─────────────────────────────────────────────────────────────────

(define *px*     1.5)         ; world X (continuous)
(define *pz*     1.5)         ; world Z (continuous)
(define *pw*     0)           ; actual W-slice (integer)
(define *pw-vis* 0.0)         ; visual W for smooth shader blend
(define *yaw*    0.0)         ; horizontal look angle (radians)
(define *pitch*  0.0)         ; vertical look angle (radians, + = up)

(define *held* (make-hash-table equal?))

(define (key-held? k)  (hash-table-ref *held* k #f))
(define (key-down! k)  (hash-table-set! *held* k #t))
(define (key-up!   k)  (hash-table-delete! *held* k))

;;; ── Collision-aware movement ──────────────────────────────────────────────
;;; Check x and z independently so player slides along walls.

(define (try-move-x! dx)
  (let* ((new-x  (+ *px* dx))
         (cur-ix (inexact->exact (floor *px*)))
         (new-ix (inexact->exact (floor (+ new-x (* (if (< dx 0) -1 1) PLAYER-R)))))
         (cur-iz (inexact->exact (floor *pz*))))
    (when (or (= new-ix cur-ix)
              (and (> new-ix cur-ix) (passage? cur-ix cur-iz *pw* B+X))
              (and (< new-ix cur-ix) (passage? cur-ix cur-iz *pw* B-X)))
      (set! *px* (max PLAYER-R (min (- DX PLAYER-R) new-x))))))

(define (try-move-z! dz)
  (let* ((new-z  (+ *pz* dz))
         (cur-ix (inexact->exact (floor *px*)))
         (cur-iz (inexact->exact (floor *pz*)))
         (new-iz (inexact->exact (floor (+ new-z (* (if (< dz 0) -1 1) PLAYER-R))))))
    (when (or (= new-iz cur-iz)
              (and (> new-iz cur-iz) (passage? cur-ix cur-iz *pw* B+Z))
              (and (< new-iz cur-iz) (passage? cur-ix cur-iz *pw* B-Z)))
      (set! *pz* (max PLAYER-R (min (- DZ PLAYER-R) new-z))))))

;;; ── W-dimension movement ─────────────────────────────────────────────────
;;; Returns #t if the step happened.

(define (try-step-w! dir)
  (let* ((cur-ix (inexact->exact (floor *px*)))
         (cur-iz (inexact->exact (floor *pz*)))
         (bit    (if (> dir 0) B+W B-W))
         (nw     (+ *pw* dir)))
    (when (and (in-bounds? cur-ix cur-iz nw)
               (passage? cur-ix cur-iz *pw* bit))
      (set! *pw* nw)
      #t)))

;;; ── Per-frame tick ───────────────────────────────────────────────────────

(define (tick! dt-ms)
  (let* ((dt  (/ (inexact dt-ms) 1000.0))
         (sy  (sin *yaw*))
         (cy  (cos *yaw*)))

    ;; WASD movement (forward = −z in yaw=0, +x for right)
    (let ((fwd  (if (or (key-held? "w") (key-held? "Up"))    1 0))
          (back (if (or (key-held? "s") (key-held? "Down"))  1 0))
          (left (if (or (key-held? "a") (key-held? "Left"))  1 0))
          (rgt  (if (or (key-held? "d") (key-held? "Right")) 1 0)))
      (let ((move-x (* MOVE-SPEED dt (- (* (- fwd back) sy) (* (- rgt left) cy))))
            (move-z (* MOVE-SPEED dt (+ (* (- fwd back) cy) (* (- rgt left) sy)))))
        (when (not (= move-x 0)) (try-move-x! move-x))
        (when (not (= move-z 0)) (try-move-z! move-z))))


    ;; Smooth W visual lerp
    (let ((diff (- (exact->inexact *pw*) *pw-vis*)))
      (set! *pw-vis* (+ *pw-vis* (* diff (min 1.0 (* dt W-VIS-SMOOTH))))))))
