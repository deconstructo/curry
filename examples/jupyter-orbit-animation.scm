;;; jupyter-orbit-animation.scm — an in-place-animated planet orbit,
;;; rendered entirely inside a Jupyter notebook cell.
;;; Version: 1.0
;;;
;;; This is NOT a standalone script — (jupyter-display-file) only exists
;;; inside curry_jupyter (see docs/reference/jupyter-kernel.md), not the
;;; plain `curry` REPL/CLI. Paste the forms below into a single cell of a
;;; notebook running the "Curry Scheme" kernel and run it.
;;;
;;; It uses the two-argument form of (jupyter-display-file path id): the
;;; first call for a given id publishes a normal image; every later call
;;; with the same id updates that same output in place instead of
;;; stacking a new image underneath it, so the cell's output area shows
;;; one image animating rather than N stacked pictures. This is the same
;;; display_data / update_display_data mechanism Python's
;;; `IPython.display.display(..., display_id=)` / `update_display(...)`
;;; use to back matplotlib's notebook animation support.
;;;
;;; Requires: (curry plplot) built (-DBUILD_MODULE_PLPLOT=ON) and a
;;; kernel built after the jupyter-display-file id-argument change.

(import (curry plplot))
(import (srfi 18))

(define pi 3.14159265358979323846)

;;; ── orbital parameters ──────────────────────────────────────────────
;;; Circular orbits at different radii/periods — not physically simulated
;;; gravity (see examples/solar-system-qt6.scm for real N-body physics),
;;; just enough kinematics to see motion: θ(t) = 2π t / period.

(define planets
  ;; (name  radius  period  color  trail-length)
  (list (vector "Mercury" 0.39  0.24 9  40)
        (vector "Venus"   0.72  0.62 11 40)
        (vector "Earth"   1.00  1.00 3  50)
        (vector "Mars"    1.52  1.88 1  50)))

(define trails (make-vector (length planets) '()))

(define (planet-pos p t)
  (let* ((r (vector-ref p 1))
         (period (vector-ref p 2))
         (theta (* 2.0 pi (/ t period))))
    (cons (* r (cos theta)) (* r (sin theta)))))

(define (render-frame! t frame-path)
  (plot-device "pngcairo")
  (plot-output frame-path)
  (plot-init)
  (plot-env -1.8 1.8 -1.8 1.8)
  (plot-labels "x (AU)" "y (AU)" "Inner solar system")

  ;; sun
  (plot-color 2)
  (plot-points (list 0.0) (list 0.0) 9)

  ;; each planet: update trail, draw trail then current position
  (let loop ((ps planets) (i 0))
    (unless (null? ps)
      (let* ((p (car ps))
             (pos (planet-pos p t))
             (trail-len (vector-ref p 4))
             (new-trail (cons pos (vector-ref trails i))))
        (when (> (length new-trail) trail-len)
          (set! new-trail (list-head new-trail trail-len)))
        (vector-set! trails i new-trail)

        (plot-color (vector-ref p 3))
        (when (> (length new-trail) 1)
          (plot-line (map car new-trail) (map cdr new-trail)))
        (plot-points (list (car pos)) (list (cdr pos)) 17))
      (loop (cdr ps) (+ i 1))))

  (plot-end))

;;; ── animate: ~120 frames, updating one notebook output in place ────

(define frame-path "/tmp/curry-orbit-frame.png")

(let loop ((frame 0))
  (when (< frame 240)
    (render-frame! (* frame 0.01) frame-path)
    (jupyter-display-file frame-path 'orbit-animation)
    (thread-sleep! 0.03)
    (loop (+ frame 1))))

(display "done") (newline)
