;;; tesseract_anaglyph.scm — Animated 4D hypercube with anaglyph 3D rendering.
;;;
;;; Wear red-cyan glasses and the tesseract floats in real 3D space.
;;;
;;; How anaglyph works here:
;;;   Two perspective passes per frame, each with a slightly different eye
;;;   position (offset in screen-X by ±eye_sep/2).  The left-eye pass draws
;;;   in the red channel only; the right-eye pass draws in cyan (G+B) only.
;;;   Additive blending merges both passes so where they overlap you see white
;;;   (screen depth) and where they diverge you see colour separation (3D pop).
;;;   Edge brightness is encoded as luminance so neither eye is darker than the
;;;   other.
;;;
;;; Pipeline per frame (run twice, once per eye):
;;;   4D vertex
;;;     → rotate-4d-xw  (built-in)
;;;     → rotate-4d-yz  (local)
;;;     → rotate-4d-zw  (local)
;;;     → project-4d    → 3D point
;;;     → project-3d-to-screen with eye offset  → 2D screen point
;;;
;;; Controls (sidebar):
;;;   XW / YZ / ZW speed  — 4D rotation rates
;;;   4D dist / 3D dist   — perspective distances
;;;   Eye separation       — inter-ocular distance (0 = flat, higher = more 3D)
;;;   Mode toggle          — Flat (normal) / Anaglyph
;;;
;;; Run:  ./build/curry examples/tesseract_anaglyph.scm
;;;
;;; Enjoy!  (Red-cyan glasses recommended.)

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

;; 3D → 2D perspective with optional eye X offset for stereo.
;; eye-x is the horizontal camera position (negative = left eye, positive = right).
;; Returns (screen-x . screen-y).
(define (project-3d-to-screen p3 cx cy scale d3 eye-x)
  (let* ((x  (vector-ref p3 0))
         (y  (vector-ref p3 1))
         (z  (vector-ref p3 2))
         (w  (/ d3 (+ d3 z)))
         ;; Parallel stereo: camera at eye-x sees the world shifted by -eye-x
         (sx (+ cx (* scale (- x eye-x) w)))
         (sy (+ cy (* scale (- y)       w))))
    (cons sx sy)))

;;; ── Edge-colour helpers ───────────────────────────────────────────────────

;; Map w-mean in [-1,1] to (r g b) — cyan-to-magenta gradient, same as
;; the original tesseract demo so the flat mode looks identical.
(define (edge-color t)
  (let* ((r  t)
         (g  (* 0.55 (- 1.0 (abs (- t 0.5)))))
         (b  (- 1.0 t)))
    (values r g b)))

;; Perceptual luminance (BT.601 weights)
(define (luma r g b)
  (+ (* 0.299 r) (* 0.587 g) (* 0.114 b)))

;;; ── Animation state ───────────────────────────────────────────────────────

(define *angle-xw* 0.0)
(define *angle-yz* 0.0)
(define *angle-zw* 0.0)

(define *speed-xw* 0.007)
(define *speed-yz* 0.011)
(define *speed-zw* 0.0)

(define *d4*      4.0)   ; 4D perspective distance
(define *d3*      4.0)   ; 3D perspective distance
(define *eye-sep* 0.35)  ; inter-ocular separation (world units)
(define *anaglyph* #t)   ; #t = anaglyph, #f = flat (normal) mode

;;; ── Per-eye projection ────────────────────────────────────────────────────

;; Project all 16 vertices to 2D for a given eye X offset.
(define (project-all-verts cx cy scale proj eye-x)
  (let ((out (make-vector 16)))
    (do ((i 0 (+ i 1)))
        ((= i 16) out)
      (let* ((v4 (vector-ref *verts* i))
             (r1 (rotate-4d-xw v4 *angle-xw*))
             (r2 (rotate-4d-yz *angle-yz* r1))
             (r3 (rotate-4d-zw *angle-zw* r2))
             (p3 (project-4d  proj r3)))
        (vector-set! out i
          (project-3d-to-screen p3 cx cy scale *d3* eye-x))))))

;;; ── Draw one eye pass ─────────────────────────────────────────────────────

;; Draw edges and vertices using pts2d.
;; color-fn : (r g b) → (r' g' b')  — channel filter for this eye.
(define (draw-pass painter pts2d color-fn)
  ;; Edges
  (do ((i 0 (+ i 1)))
      ((= i (vector-length *edges*)))
    (let* ((edge (vector-ref *edges* i))
           (ia   (car edge))
           (ib   (cdr edge))
           (wa   (vector-ref (vector-ref *verts* ia) 3))
           (wb   (vector-ref (vector-ref *verts* ib) 3))
           (t    (* 0.5 (+ 1.0 (* 0.5 (+ wa wb)))))
           (pa   (vector-ref pts2d ia))
           (pb   (vector-ref pts2d ib)))
      (call-with-values
        (lambda () (edge-color t))
        (lambda (r g b)
          (call-with-values
            (lambda () (color-fn r g b))
            (lambda (r* g* b*)
              (gfx-set-pen-color! painter r* g* b* 0.88)
              (gfx-set-pen-width! painter 1.6)
              (gfx-draw-line! painter (car pa) (cdr pa) (car pb) (cdr pb))))))))
  ;; Vertices
  (do ((i 0 (+ i 1)))
      ((= i 16))
    (let* ((wi (vector-ref (vector-ref *verts* i) 3))
           (t  (* 0.5 (+ 1.0 wi)))
           (p  (vector-ref pts2d i)))
      (call-with-values
        (lambda () (edge-color t))
        (lambda (r g b)
          (call-with-values
            (lambda () (color-fn r g b))
            (lambda (r* g* b*)
              (gfx-set-color! painter r* g* b* 1.0)
              (gfx-fill-circle! painter (car p) (cdr p) 3.5))))))))

;;; ── Per-frame draw ────────────────────────────────────────────────────────

(define (draw-frame painter w h)
  (gfx-clear! painter 0.0 0.0 0.0)   ; black background
  (gfx-set-antialias! painter #t)

  (let* ((cx    (/ w 2.0))
         (cy    (/ h 2.0))
         (scale (* (min w h) 0.22))
         (proj  (make-4d-projector *d4*)))

    (if *anaglyph*

      ;; ── Anaglyph mode: two passes, additive blend ──────────────────────
      (let* ((half   (/ *eye-sep* 2.0))
             (left   (project-all-verts cx cy scale proj (- half)))
             (right  (project-all-verts cx cy scale proj    half)))
        (gfx-set-blend! painter 'add)
        ;; Left eye — red channel only
        (draw-pass painter left
          (lambda (r g b)
            (let ((L (luma r g b)))
              (values L 0.0 0.0))))
        ;; Right eye — cyan channel only
        (draw-pass painter right
          (lambda (r g b)
            (let ((L (luma r g b)))
              (values 0.0 L L))))
        (gfx-set-blend! painter 'src))

      ;; ── Flat mode: single pass, original colours ───────────────────────
      (let ((pts (project-all-verts cx cy scale proj 0.0)))
        (draw-pass painter pts
          (lambda (r g b) (values r g b)))))))

;;; ── Step ──────────────────────────────────────────────────────────────────

(define (step!)
  (set! *angle-xw* (+ *angle-xw* *speed-xw*))
  (set! *angle-yz* (+ *angle-yz* *speed-yz*))
  (set! *angle-zw* (+ *angle-zw* *speed-zw*)))

;;; ── Window and controls ───────────────────────────────────────────────────

(define win     (make-window "Tesseract — Anaglyph 4D Hypercube" 1020 720))
(define canvas  (window-canvas win))
(define sidebar (window-sidebar win))

(define (add-heading! text)
  (box-add! sidebar (make-label text)))

;; Mode toggle
(add-heading! "Render mode")
(box-add! sidebar
  (make-radio-group '("Anaglyph (red-cyan)" "Flat (normal)") 0
    (lambda (i)
      (set! *anaglyph* (zero? i)))))

(box-add! sidebar (make-separator))

;; Anaglyph controls
(add-heading! "Stereo")
(box-add! sidebar
  (make-slider "Eye separation" 0 100 1 35
    (lambda (v) (set! *eye-sep* (* v 0.01)))))

(box-add! sidebar (make-separator))

;; Rotation speed sliders (value in units of 0.001 rad/frame)
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

;; Projection distance sliders
(add-heading! "Perspective")
(box-add! sidebar
  (make-slider "4D dist" 21 80 1 40
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
      ((equal? key "a")
       (set! *anaglyph* (not *anaglyph*)))
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
