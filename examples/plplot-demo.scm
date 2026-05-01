;;; plplot-demo.scm — showcase of the (curry plplot) module
;;;
;;; Produces a set of SVG files in /tmp covering 2-D plots, parametric curves,
;;; logarithmic axes, error bars, subplots, and 3-D surfaces.
;;;
;;; Build the module first:
;;;   cmake -B build -DBUILD_MODULE_PLPLOT=ON && cmake --build build -j$(nproc)
;;;
;;; Run:
;;;   CURRY_MODULE_PATH=build/mods ./build/curry examples/plplot-demo.scm

(import (curry plplot))

(define pi 3.14159265358979323846)

;;; ── helpers ─────────────────────────────────────────────────────────────────

;;; n evenly-spaced values in [lo, hi]
(define (linspace lo hi n)
  (let ((step (/ (- hi lo) (- n 1))))
    (let loop ((i 0) (acc '()))
      (if (= i n)
          (reverse acc)
          (loop (+ i 1) (cons (+ lo (* i step)) acc))))))

;;; apply f element-wise
(define (fmap f xs) (map f xs))

;;; open a new SVG output file
(define (open-svg path)
  (plot-device "svg")
  (plot-output path)
  (plot-init))

(define (done path)
  (plot-end)
  (display "wrote ") (display path) (newline))

;;; report the library version once
(display "PLplot version: ") (display (plot-version)) (newline)

;;; ── 1. Sine and cosine on one page ─────────────────────────────────────────

(open-svg "/tmp/plplot-demo-sincos.svg")

(define t200 (linspace 0.0 (* 2.0 pi) 200))

(plot-env 0.0 (* 2.0 pi) -1.2 1.2)
(plot-labels "x (radians)" "amplitude" "Sine and Cosine")

(plot-color 3)                          ; green — sine
(plot-line t200 (fmap sin t200))

(plot-color 9)                          ; blue — cosine
(plot-line t200 (fmap cos t200))

(done "/tmp/plplot-demo-sincos.svg")

;;; ── 2. Scatter plot with symbol markers ─────────────────────────────────────

(open-svg "/tmp/plplot-demo-scatter.svg")

;;; Ten points scattered along y = x + noise (hand-crafted)
(define sc-x '(0.05 0.12 0.20 0.31 0.42 0.55 0.63 0.74 0.85 0.95))
(define sc-y '(0.08 0.10 0.24 0.28 0.45 0.52 0.68 0.71 0.88 0.92))

(plot-env 0.0 1.0 0.0 1.0)
(plot-labels "x" "y" "Scatter  (symbol 4 = diamond)")
(plot-color 2)                          ; red
(plot-points sc-x sc-y 4)              ; PLplot symbol 4 = filled diamond

(done "/tmp/plplot-demo-scatter.svg")

;;; ── 3. Histogram ────────────────────────────────────────────────────────────

(open-svg "/tmp/plplot-demo-histogram.svg")

;;; 60 samples approximating a bell-shaped distribution
(define hist-data
  '(-2.3 -1.9 -1.7 -1.5 -1.4 -1.2 -1.1 -1.0 -0.9 -0.8
    -0.7 -0.6 -0.6 -0.5 -0.4 -0.4 -0.3 -0.3 -0.2 -0.2
    -0.1 -0.1  0.0  0.0  0.0  0.1  0.1  0.2  0.2  0.3
     0.3  0.4  0.4  0.5  0.5  0.6  0.6  0.7  0.8  0.9
     1.0  1.1  1.2  1.3  1.5  1.6  1.8  2.0  2.2 -0.5
    -0.2  0.0  0.1  0.3  0.5  0.7  0.9  1.1  1.3  1.6))

;;; plot-histogram: data  xmin  xmax  nbins
(plot-histogram hist-data -3.0 3.0 18)
(plot-labels "value" "count" "Histogram (18 bins)")

(done "/tmp/plplot-demo-histogram.svg")

;;; ── 4. Y error bars ─────────────────────────────────────────────────────────

(open-svg "/tmp/plplot-demo-errorbars.svg")

(define eb-x    '(1.0 2.0 3.0 4.0 5.0))
(define eb-ymid '(1.1 2.0 2.8 4.1 5.0))
(define eb-ylo  '(0.7 1.6 2.3 3.6 4.5))
(define eb-yhi  '(1.5 2.4 3.3 4.6 5.5))

(plot-env 0.0 6.0 0.0 6.5)
(plot-labels "x" "y ± σ" "Measurements with error bars")

(plot-color 9)                          ; blue — central line
(plot-line  eb-x eb-ymid)
(plot-color 2)                          ; red — error caps
(plot-error-y eb-x eb-ylo eb-yhi)
(plot-color 2)
(plot-points eb-x eb-ymid 17)          ; symbol 17 = filled circle

(done "/tmp/plplot-demo-errorbars.svg")

;;; ── 5. Subplot grid (1×2) ───────────────────────────────────────────────────

(open-svg "/tmp/plplot-demo-subplot.svg")

(plot-subplot 1 2)                      ; 1 row, 2 columns

;;; left panel — sine
(plot-advance)
(plot-env 0.0 (* 2.0 pi) -1.0 1.0)
(plot-labels "x" "sin x" "Sine")
(plot-color 3)
(plot-line t200 (fmap sin t200))

;;; right panel — x·sin(x)
(plot-advance)
(define t2-x (linspace 0.0 (* 2.0 pi) 200))
(plot-env 0.0 (* 2.0 pi) -7.0 7.0)
(plot-labels "x" "x·sin x" "Damped ramp")
(plot-color 4)
(plot-line t2-x (map (lambda (x) (* x (sin x))) t2-x))

(done "/tmp/plplot-demo-subplot.svg")

;;; ── 6. Text annotation ──────────────────────────────────────────────────────

(open-svg "/tmp/plplot-demo-annotation.svg")

(plot-env 0.0 1.0 0.0 1.0)
(plot-labels "x" "y" "Annotations")
(plot-color 1)

(plot-text 0.5 0.5 "centre of the plot")
(plot-text 0.1 0.85 "top-left corner")
(plot-text 0.6 0.15 "bottom-right area")
;;; plot-mtex: side  displacement  position  justification  text
(plot-mtex "t" 1.5 0.5 0.5 "margin title via plot-mtex")

(done "/tmp/plplot-demo-annotation.svg")

;;; ── 7. Lissajous figure (a=3, b=2, δ=π/4) ──────────────────────────────────

(open-svg "/tmp/plplot-demo-lissajous.svg")

(define lis-t (linspace 0.0 (* 2.0 pi) 800))
(define lis-x (map (lambda (t) (sin (* 3.0 t)))              lis-t))
(define lis-y (map (lambda (t) (sin (+ (* 2.0 t) (/ pi 4)))) lis-t))

(plot-env -1.1 1.1 -1.1 1.1)
(plot-labels "sin(3t)" "sin(2t + π/4)" "Lissajous  a=3  b=2  δ=π/4")
(plot-color 4)
(plot-line lis-x lis-y)

(done "/tmp/plplot-demo-lissajous.svg")

;;; ── 8. Butterfly curve (Temple H. Fay, 1989) ────────────────────────────────
;;; r(t) = e^cos(t) − 2·cos(4t) − sin⁵(t/12)

(open-svg "/tmp/plplot-demo-butterfly.svg")

(define bt (linspace 0.0 (* 24.0 pi) 3000))
(define (butterfly-r t)
  (- (exp (cos t)) (* 2.0 (cos (* 4.0 t))) (expt (sin (/ t 12.0)) 5)))
(define bfly-x (map (lambda (t) (* (sin t) (butterfly-r t))) bt))
(define bfly-y (map (lambda (t) (* (cos t) (butterfly-r t))) bt))

(plot-env -3.0 3.0 -3.5 2.5)
(plot-labels "x" "y" "Butterfly curve  (Temple H. Fay, 1989)")
(plot-color-rgb 220 60 200)             ; magenta — custom RGB colour
(plot-line bfly-x bfly-y)

(done "/tmp/plplot-demo-butterfly.svg")

;;; ── 9. Semi-log axes (exponential decay) ────────────────────────────────────

(open-svg "/tmp/plplot-demo-logscale.svg")

(define log-x (linspace 0.05 5.0 100))
(define log-y (map (lambda (x) (exp (- x))) log-x))

;;; plot-env-log: xmin xmax ymin ymax logaxis
;;; logaxis=20 → logarithmic y only (semi-log)
(plot-env-log 0.0 5.5 1e-3 2.0 20)
(plot-labels "x" "e^{-x}" "Exponential decay on semi-log axes")
(plot-color 2)
(plot-line log-x log-y)

(done "/tmp/plplot-demo-logscale.svg")

;;; ── 10. Custom line style and width ─────────────────────────────────────────

(open-svg "/tmp/plplot-demo-style.svg")

(plot-background-color 15 15 30)        ; dark navy background
(plot-init)                             ; re-init so background takes effect
;; Actually background must be set before init — reopen:
(plot-end)
(plot-device "svg")
(plot-output "/tmp/plplot-demo-style.svg")
(plot-background-color 15 15 30)
(plot-init)

(plot-env 0.0 (* 2.0 pi) -1.2 1.2)
(plot-labels "x" "f(x)" "Custom colours and line widths")

(plot-color-rgb 255 80 0)               ; orange — thick sine
(plot-width 3.0)
(plot-line t200 (fmap sin t200))

(plot-color-rgb 0 200 255)              ; cyan — thin cosine
(plot-width 1.0)
(plot-line t200 (fmap cos t200))

(plot-width 1.0)
(plot-end)
(display "wrote /tmp/plplot-demo-style.svg") (newline)

;;; ── 11. 3-D helix ───────────────────────────────────────────────────────────

(open-svg "/tmp/plplot-demo-helix.svg")

(define hel-t (linspace 0.0 (* 4.0 pi) 120))
(define hel-x (fmap cos hel-t))
(define hel-y (fmap sin hel-t))
(define hel-z hel-t)

;;; plot-3d-init: x1 x2  y1 y2  z1 z2  altitude azimuth
(plot-3d-init -1.0 1.0 -1.0 1.0 0.0 (* 4.0 pi) 30.0 60.0)
(plot-3d-box "X" "Y" "Z (t)")
(plot-color 9)
(plot-3d-line hel-x hel-y hel-z)

(done "/tmp/plplot-demo-helix.svg")

;;; ── 12. 3-D sinc surface ────────────────────────────────────────────────────
;;;  sinc(r) = sin(r)/r  — a classic 3-D demo

(open-svg "/tmp/plplot-demo-sinc.svg")

(define sinc-n 25)
(define sinc-xs (linspace -6.0 6.0 sinc-n))
(define sinc-ys (linspace -6.0 6.0 sinc-n))

(define (sinc r)
  (if (< (abs r) 1e-9) 1.0 (/ (sin r) r)))

;;; z is a list of rows (one per y), each row has sinc-n values
(define sinc-zs
  (map (lambda (y)
         (map (lambda (x) (sinc (sqrt (+ (* x x) (* y y))))) sinc-xs))
       sinc-ys))

(plot-3d-init -6.0 6.0 -6.0 6.0 -0.25 1.05 40.0 225.0)
(plot-3d-box "X" "Y" "sinc(r)")
(plot-color 9)
(plot-3d-surface sinc-xs sinc-ys sinc-zs)

(done "/tmp/plplot-demo-sinc.svg")

;;; ── 13. Saddle mesh  z = x² − y² ───────────────────────────────────────────

(open-svg "/tmp/plplot-demo-saddle.svg")

(define sad-n 18)
(define sad-xs (linspace -2.0 2.0 sad-n))
(define sad-ys (linspace -2.0 2.0 sad-n))
(define sad-zs
  (map (lambda (y)
         (map (lambda (x) (- (* x x) (* y y))) sad-xs))
       sad-ys))

(plot-3d-init -2.0 2.0 -2.0 2.0 -5.0 5.0 35.0 135.0)
(plot-3d-box "X" "Y" "x²−y²")
(plot-color 2)
(plot-3d-mesh sad-xs sad-ys sad-zs)

(done "/tmp/plplot-demo-saddle.svg")

;;; ── done ────────────────────────────────────────────────────────────────────

(newline)
(display "All SVG files written to /tmp/plplot-demo-*.svg") (newline)
