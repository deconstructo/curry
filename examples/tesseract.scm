;;; tesseract.scm — Animated 4D hypercube (tesseract) demo.
;;; Version: 1.1
;;;
;;; Pipeline per frame:
;;;   4D vertex
;;;     → rotate in XW plane  (built-in rotate-4d-xw)
;;;     → rotate in YZ plane  (implemented here)
;;;     → rotate in ZW plane  (implemented here, optional second 4D rotation)
;;;     → project-4d  → 3D point
;;;     → perspective project → 2D screen point
;;;
;;; Edges are coloured by the average W-coordinate of their two endpoints
;;; (blue = inner cell at w=-1, red = outer cell at w=+1).
;;;
;;; Controls (sidebar sliders):
;;;   XW speed   — rotation rate in the XW plane  (the "classic" 4D spin)
;;;   YZ speed   — rotation rate in the YZ plane  (tumbles the 3D shadow)
;;;   ZW speed   — rotation rate in the ZW plane  (second 4D spin, creates
;;;                the characteristic inside-out tesseract effect)
;;;   4D dist    — perspective distance for 4D→3D projection
;;;   3D dist    — perspective distance for 3D→2D projection
;;;
;;; Run: ./build/curry examples/tesseract.scm
;;; 
;;; Enjoy!
;;; 

(import (curry qt6))
(import (scheme base))
(import (scheme inexact))

;;; ── Tesseract geometry ────────────────────────────────────────────────────

;; 16 vertices: bit k of index i → coordinate k is +1.0, otherwise -1.0
(define *verts*
  (let ((v (make-vector 16)))
    (do ((i 0 (+ i 1)))
        ((= i 16) v)
      (vector-set! v i
        (vector (if (zero? (bitwise-and i 1)) -1.0  1.0)
                (if (zero? (bitwise-and i 2)) -1.0  1.0)
                (if (zero? (bitwise-and i 4)) -1.0  1.0)
                (if (zero? (bitwise-and i 8)) -1.0  1.0))))))

;; 32 edges: pairs (i . j) with i < j that differ in exactly one bit
(define *edges*
  (let ((acc '()))
    (do ((i 0 (+ i 1)))
        ((= i 16))
      (do ((b 0 (+ b 1)))
          ((= b 4))
        (let ((j (bitwise-xor i (arithmetic-shift 1 b))))
          (when (> j i)
            (set! acc (cons (cons i j) acc))))))
    (list->vector (reverse acc))))

;;; ── Rotation helpers ──────────────────────────────────────────────────────

;; rotate-4d-xw is provided by (curry qt6).
;; Implement YZ and ZW rotations for the two extra degrees of freedom.

(define (rotate-4d-yz angle p)
  (let ((y (vector-ref p 1))
        (z (vector-ref p 2))
        (c (cos angle))
        (s (sin angle)))
    (vector (vector-ref p 0)
            (- (* c y) (* s z))
            (+ (* s y) (* c z))
            (vector-ref p 3))))

(define (rotate-4d-zw angle p)
  (let ((z (vector-ref p 2))
        (w (vector-ref p 3))
        (c (cos angle))
        (s (sin angle)))
    (vector (vector-ref p 0)
            (vector-ref p 1)
            (- (* c z) (* s w))
            (+ (* s z) (* c w)))))

;;; ── Projection ────────────────────────────────────────────────────────────

;; Identity 3×3 matrix — passed to vec3-project-batch when the 3-D points
;; are already in their final orientation (4-D pipeline handled rotation).
(define *I3* (vector 1.0 0.0 0.0  0.0 1.0 0.0  0.0 0.0 1.0))

;;; ── Animation state ───────────────────────────────────────────────────────

(define *angle-xw*  0.0)
(define *angle-yz*  0.0)
(define *angle-zw*  0.0)

(define *speed-xw*  0.007)   ; ~0.4 rad/s at 60 fps
(define *speed-yz*  0.011)
(define *speed-zw*  0.0)     ; off by default — enable via slider for the
                              ; classic "inside-out" effect

(define *d4*  4.0)            ; 4D perspective distance (must stay > 2 — vertices reach w=±2)
(define *d3*  4.0)            ; 3D perspective distance

;;; ── Per-frame draw ────────────────────────────────────────────────────────

(define (draw-frame painter w h)
  (gfx-clear! painter 0.04 0.04 0.10)
  (gfx-set-antialias! painter #t)

  (let* ((cx    (/ w 2.0))
         (cy    (/ h 2.0))
         (scale (* (min w h) 0.22))
         (proj  (make-4d-projector *d4*))

         ;; Run the 4-D pipeline, collecting 3-D results into flat vectors
         (p3x (make-vector 16 0.0))
         (p3y (make-vector 16 0.0))
         (p3z (make-vector 16 0.0))
         (_   (do ((i 0 (+ i 1))) ((= i 16))
                (let* ((v4 (vector-ref *verts* i))
                       (r1 (rotate-4d-xw v4 *angle-xw*))
                       (r2 (rotate-4d-yz *angle-yz* r1))
                       (r3 (rotate-4d-zw *angle-zw* r2))
                       (p3 (project-4d   proj r3)))
                  (vector-set! p3x i (vector-ref p3 0))
                  (vector-set! p3y i (vector-ref p3 1))
                  (vector-set! p3z i (vector-ref p3 2)))))
         ;; Batch 3-D → 2-D in C (identity rotation — 4-D pipeline owns orientation)
         (res (vec3-project-batch p3x p3y p3z *I3* cx cy scale *d3*))
         (sx  (vector-ref res 0))
         (sy  (vector-ref res 1)))

    ;; Draw edges — colour by mean W coordinate (blue=inner, red=outer)
    (do ((i 0 (+ i 1)))
        ((= i (vector-length *edges*)))
      (let* ((edge (vector-ref *edges* i))
             (ia   (car edge))
             (ib   (cdr edge))
             (wa   (vector-ref (vector-ref *verts* ia) 3))
             (wb   (vector-ref (vector-ref *verts* ib) 3))
             (t    (* 0.5 (+ 1.0 (* 0.5 (+ wa wb)))))
             (r    t)
             (g    (* 0.55 (- 1.0 (abs (- t 0.5)))))
             (b    (- 1.0 t)))
        (gfx-set-pen-color! painter r g b 0.88)
        (gfx-set-pen-width! painter 1.6)
        (gfx-draw-line! painter (vector-ref sx ia) (vector-ref sy ia)
                                (vector-ref sx ib) (vector-ref sy ib))))

    ;; Draw vertices — slightly larger dot, same colour scheme
    (do ((i 0 (+ i 1)))
        ((= i 16))
      (let* ((wi (vector-ref (vector-ref *verts* i) 3))
             (t  (* 0.5 (+ 1.0 wi)))
             (r  t)
             (g  (* 0.55 (- 1.0 (abs (- t 0.5)))))
             (b  (- 1.0 t)))
        (gfx-set-color! painter r g b 1.0)
        (gfx-fill-circle! painter (vector-ref sx i) (vector-ref sy i) 3.5)))))

;;; ── Step ──────────────────────────────────────────────────────────────────

(define (step!)
  (set! *angle-xw* (+ *angle-xw* *speed-xw*))
  (set! *angle-yz* (+ *angle-yz* *speed-yz*))
  (set! *angle-zw* (+ *angle-zw* *speed-zw*)))

;;; ── Window and controls ───────────────────────────────────────────────────

(define win     (make-window "Tesseract — 4D Hypercube" 960 720))
(define canvas  (window-canvas win))
(define sidebar (window-sidebar win))

;; Helper: add a labelled section header
(define (add-heading! text)
  (box-add! sidebar (make-label text)))

;; Rotation speed sliders  (value in units of 0.001 rad/frame)
(add-heading! "Rotation speeds")

(box-add! sidebar
  (make-slider "XW speed" -40 40 1 7
    (lambda (v) (set! *speed-xw* (* v 0.001)))))

(box-add! sidebar
  (make-slider "YZ speed" -40 40 1 11
    (lambda (v) (set! *speed-yz* (* v 0.001)))))

(box-add! sidebar
  (make-slider "ZW speed" -40 40 1 0
    (lambda (v) (set! *speed-zw* (* v 0.001)))))

(box-add! sidebar (make-separator))

;; Projection distance sliders  (value in units of 0.1)
(add-heading! "Perspective")

(box-add! sidebar
  (make-slider "4D dist" 21 80 1 40        ; min 2.1 keeps fov > max-w (2.0)
    (lambda (v) (set! *d4* (* v 0.1)))))

(box-add! sidebar
  (make-slider "3D dist" 10 80 1 40
    (lambda (v) (set! *d3* (* v 0.1)))))

(box-add! sidebar (make-separator))

(box-add! sidebar
  (make-button "Reset angles"
    (lambda ()
      (set! *angle-xw* 0.0)
      (set! *angle-yz* 0.0)
      (set! *angle-zw* 0.0))))

;; Keyboard shortcuts
(window-on-key! win
  (lambda (key mods)
    (cond
      ((or (equal? key "q") (equal? key "Escape"))
       (quit-event-loop)))))

;; 60 fps animation timer
(define anim-timer
  (make-timer 16
    (lambda ()
      (step!)
      (canvas-redraw! canvas))))

(window-on-realize! win
  (lambda ()
    (canvas-on-draw! canvas draw-frame)
    (timer-start! anim-timer)))

(window-on-close! win
  (lambda ()
    (timer-stop! anim-timer)
    (quit-event-loop)))

(window-show! win)
(run-event-loop)
