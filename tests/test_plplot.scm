;;; test_plplot.scm — tests for the (curry plplot) module
;;;
;;; Uses the "svg" device (file-backed, no display needed) so it runs in CI.
;;; Each section exercises a group of bindings; output files land in /tmp.
;;;
;;; Run: CURRY_MODULE_PATH=build/mods ./build/curry tests/test_plplot.scm

(import (curry plplot))

(define pass 0)
(define fail 0)

(define (check label result expected)
  (if (equal? result expected)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write result)
             (display " expected ") (write expected)
             (newline)
             (set! fail (+ fail 1)))))

(define (check-pred label pred result)
  (if (pred result)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " unexpected value: ") (write result)
             (newline)
             (set! fail (+ fail 1)))))

(define (raises? thunk)
  (guard (exn (#t #t))
    (thunk)
    #f))

;;; Helper: generate a list of n evenly-spaced floats in [lo, hi]
(define (linspace lo hi n)
  (let ((step (/ (- hi lo) (- n 1))))
    (let loop ((i 0) (acc '()))
      (if (= i n)
          (reverse acc)
          (loop (+ i 1)
                (cons (+ lo (* i step)) acc))))))

;;; Helper: apply a function element-wise to a list
(define (map-list f lst)
  (map f lst))

;;; ── plot-version ─────────────────────────────────────────────────────────

(define ver (plot-version))
(check-pred "plot-version returns string"   string? ver)
(check-pred "plot-version non-empty"        (lambda (s) (> (string-length s) 0)) ver)
(display "PLplot version: ") (display ver) (newline)

;;; ── 2D line plot ─────────────────────────────────────────────────────────

(define out-line "/tmp/curry-plplot-line.svg")

(plot-device "svg")
(plot-output out-line)
(plot-init)

(plot-env 0.0 1.0 -1.0 1.0)
(plot-labels "x" "sin(x)" "Sine wave")
(plot-color 3)   ; green

(define xs (linspace 0.0 (* 2.0 3.14159265358979) 100))
(define ys (map-list sin xs))

(plot-line xs ys)
(plot-end)

(check-pred "SVG line file created"
            (lambda (path)
              (guard (exn (#t #f))
                (let* ((p (open-input-file path))
                       (c (read-char p)))
                  (close-input-port p)
                  (not (eof-object? c)))))
            out-line)

;;; ── 2D scatter plot (plot-points) ────────────────────────────────────────

(define out-scatter "/tmp/curry-plplot-scatter.svg")

(plot-device "svg")
(plot-output out-scatter)
(plot-init)

(define px '(0.1 0.3 0.5 0.7 0.9))
(define py '(0.2 0.8 0.4 0.9 0.3))

(plot-env 0.0 1.0 0.0 1.0)
(plot-labels "x" "y" "Scatter")
(plot-color 2)   ; red
(plot-points px py 4)  ; symbol 4 = diamond
(plot-end)

(check-pred "SVG scatter file created"
            (lambda (path)
              (guard (exn (#t #f))
                (let* ((p (open-input-file path))
                       (c (read-char p)))
                  (close-input-port p)
                  (not (eof-object? c)))))
            out-scatter)

;;; ── Histogram ────────────────────────────────────────────────────────────

(define out-hist "/tmp/curry-plplot-hist.svg")

(plot-device "svg")
(plot-output out-hist)
(plot-init)

;;; 50 "data" values — a rough hand-built normal distribution sample
(define hist-data
  '(-2.1 -1.8 -1.5 -1.3 -1.1 -1.0 -0.9 -0.8 -0.7 -0.6
    -0.5 -0.4 -0.3 -0.2 -0.1  0.0  0.1  0.2  0.3  0.4
     0.5  0.6  0.7  0.8  0.9  1.0  1.1  1.2  1.4  1.7
    -0.3 -0.1  0.0  0.0  0.1  0.1  0.2  0.2  0.3  0.4
     0.5  0.6  0.7  0.8  1.0  1.2 -0.5 -0.2  0.3  0.9))

;;; plot-histogram args: data  datamin  datamax  nbins  flags(0)
(plot-histogram hist-data -3.0 3.0 20)
(plot-end)

(check-pred "SVG histogram file created"
            (lambda (path)
              (guard (exn (#t #f))
                (let* ((p (open-input-file path))
                       (c (read-char p)))
                  (close-input-port p)
                  (not (eof-object? c)))))
            out-hist)

;;; ── Error bars ───────────────────────────────────────────────────────────

(define out-err "/tmp/curry-plplot-errbar.svg")

(plot-device "svg")
(plot-output out-err)
(plot-init)

(define eb-x    '(1.0 2.0 3.0 4.0 5.0))
(define eb-ymid '(1.1 1.9 3.2 3.8 5.1))
(define eb-ylo  '(0.8 1.6 2.8 3.4 4.7))
(define eb-yhi  '(1.4 2.2 3.6 4.2 5.5))

(plot-env 0.0 6.0 0.0 6.5)
(plot-labels "x" "y" "Error bars")
(plot-color 9)   ; blue
(plot-line eb-x eb-ymid)
(plot-color 1)   ; black
(plot-error-y eb-x eb-ylo eb-yhi)
(plot-end)

(check-pred "SVG error-bar file created"
            (lambda (path)
              (guard (exn (#t #f))
                (let* ((p (open-input-file path))
                       (c (read-char p)))
                  (close-input-port p)
                  (not (eof-object? c)))))
            out-err)

;;; ── Subplot layout ───────────────────────────────────────────────────────

(define out-sub "/tmp/curry-plplot-subplot.svg")

(plot-device "svg")
(plot-output out-sub)
(plot-init)

(plot-subplot 1 2)   ; 1 row, 2 columns

;;; Left panel — sine
(plot-advance)
(plot-env 0.0 (* 2.0 3.14159265358979) -1.0 1.0)
(plot-labels "x" "sin(x)" "Left")
(plot-color 3)
(plot-line xs ys)

;;; Right panel — cosine
(plot-advance)
(define ycos (map-list cos xs))
(plot-env 0.0 (* 2.0 3.14159265358979) -1.0 1.0)
(plot-labels "x" "cos(x)" "Right")
(plot-color 4)
(plot-line xs ycos)

(plot-end)

(check-pred "SVG subplot file created"
            (lambda (path)
              (guard (exn (#t #f))
                (let* ((p (open-input-file path))
                       (c (read-char p)))
                  (close-input-port p)
                  (not (eof-object? c)))))
            out-sub)

;;; ── Text annotation (plot-text, plot-mtex) ───────────────────────────────

(define out-txt "/tmp/curry-plplot-text.svg")

(plot-device "svg")
(plot-output out-txt)
(plot-init)

(plot-env 0.0 1.0 0.0 1.0)
(plot-labels "x" "y" "Annotations")
(plot-color 1)
(plot-text 0.5 0.5 "centre label")
(plot-text 0.1 0.8 "top-left")
;;; plot-mtex: side  disp  pos  just  text
(plot-mtex "t" 1.0 0.5 0.5 "title via mtex")
(plot-end)

(check-pred "SVG text file created"
            (lambda (path)
              (guard (exn (#t #f))
                (let* ((p (open-input-file path))
                       (c (read-char p)))
                  (close-input-port p)
                  (not (eof-object? c)))))
            out-txt)

;;; ── Color / style helpers ────────────────────────────────────────────────

;;; Exercise color, rgb-color, width, bg-color, font-size without crashing.
(define out-style "/tmp/curry-plplot-style.svg")

(plot-device "svg")
(plot-output out-style)
(plot-background-color 20 20 40)   ; dark background
(plot-init)

(plot-env 0.0 1.0 0.0 1.0)

(plot-color-rgb 255 80 0)          ; custom orange via slot 15
(plot-width 3.0)
(plot-font-size 1.5)
(plot-line '(0.0 1.0) '(0.0 1.0))

(plot-color 5)                     ; switch back to palette slot
(plot-width 1.0)
(plot-font-size 1.0)
(plot-line '(0.0 1.0) '(1.0 0.0))

(plot-end)

(check-pred "SVG style file created"
            (lambda (path)
              (guard (exn (#t #f))
                (let* ((p (open-input-file path))
                       (c (read-char p)))
                  (close-input-port p)
                  (not (eof-object? c)))))
            out-style)

;;; ── 3D line ──────────────────────────────────────────────────────────────

(define out-3d "/tmp/curry-plplot-3d.svg")

(plot-device "svg")
(plot-output out-3d)
(plot-init)

(define helix-t (linspace 0.0 (* 4.0 3.14159265358979) 60))
(define helix-x (map-list cos helix-t))
(define helix-y (map-list sin helix-t))
(define helix-z helix-t)

;;; plot-3d-init: x1 x2 y1 y2 z1 z2 alt az
(plot-3d-init -1.0 1.0 -1.0 1.0 0.0 (* 4.0 3.14159265358979) 30.0 45.0)
(plot-3d-box "X" "Y" "Z")
(plot-color 2)
(plot-3d-line helix-x helix-y helix-z)
(plot-end)

(check-pred "SVG 3D-line file created"
            (lambda (path)
              (guard (exn (#t #f))
                (let* ((p (open-input-file path))
                       (c (read-char p)))
                  (close-input-port p)
                  (not (eof-object? c)))))
            out-3d)

;;; ── plot-page-dimensions ─────────────────────────────────────────────────

(plot-device "svg")
(plot-output "/tmp/curry-plplot-dims.svg")
(plot-init)
(define dims (plot-page-dimensions))
(check-pred "page-dimensions is pair" pair? dims)
(check-pred "page width is integer"   integer? (car dims))
(check-pred "page height is integer"  integer? (cdr dims))
(plot-end)

;;; ── Error cases ──────────────────────────────────────────────────────────

;;; Mismatched x/y lengths must raise an error.
(plot-device "svg")
(plot-output "/tmp/curry-plplot-err.svg")
(plot-init)
(check "plot-line length mismatch raises"
       (raises? (lambda ()
                  (plot-env 0.0 1.0 0.0 1.0)
                  (plot-line '(0.0 0.5 1.0) '(0.0 1.0))))
       #t)
(plot-end)

;;; ── Lissajous figure (a=3, b=2, δ=π/4) ─────────────────────────────────
;;; A classic parametric beauty — also stress-tests plot-line with floats.

(define out-liss "/tmp/curry-plplot-lissajous.svg")
(define pi 3.14159265358979)

(plot-device "svg")
(plot-output out-liss)
(plot-init)

(define liss-t (linspace 0.0 (* 2.0 pi) 500))
(define liss-x (map (lambda (t) (sin (* 3.0 t)))             liss-t))
(define liss-y (map (lambda (t) (sin (+ (* 2.0 t) (/ pi 4)))) liss-t))

(plot-env -1.1 1.1 -1.1 1.1)
(plot-labels "sin(3t)" "sin(2t + π/4)" "Lissajous  a=3 b=2 δ=π/4")
(plot-color 9)   ; blue
(plot-line liss-x liss-y)
(plot-end)

(check-pred "SVG Lissajous file created"
            (lambda (path)
              (guard (exn (#t #f))
                (let* ((p (open-input-file path))
                       (c (read-char p)))
                  (close-input-port p)
                  (not (eof-object? c)))))
            out-liss)

;;; ── Butterfly curve (Temple H. Fay, 1989) ───────────────────────────────
;;; x = sin(t)(e^cos(t) − 2cos(4t) − sin^5(t/12))

(define out-butterfly "/tmp/curry-plplot-butterfly.svg")

(plot-device "svg")
(plot-output out-butterfly)
(plot-init)

(define bt (linspace 0.0 (* 24.0 pi) 2000))
(define (butterfly-r t)
  (- (exp (cos t)) (* 2.0 (cos (* 4.0 t))) (expt (sin (/ t 12.0)) 5)))
(define bx (map (lambda (t) (* (sin t) (butterfly-r t))) bt))
(define by (map (lambda (t) (* (cos t) (butterfly-r t))) bt))

(plot-env -3.0 3.0 -3.5 2.5)
(plot-labels "x" "y" "Butterfly curve  (Temple H. Fay, 1989)")
(plot-color-rgb 220 80 180)   ; magenta-ish
(plot-line bx by)
(plot-end)

(check-pred "SVG butterfly file created"
            (lambda (path)
              (guard (exn (#t #f))
                (let* ((p (open-input-file path))
                       (c (read-char p)))
                  (close-input-port p)
                  (not (eof-object? c)))))
            out-butterfly)

;;; ── Log-scale plot (plot-env-log) ────────────────────────────────────────
;;; Exponential decay on a semi-log axis — straight line is the tell.

(define out-log "/tmp/curry-plplot-logscale.svg")

(plot-device "svg")
(plot-output out-log)
(plot-init)

(define log-x (linspace 0.1 5.0 80))
(define log-y (map (lambda (x) (exp (- x))) log-x))

;;; plot-env-log: xmin xmax ymin ymax axis  (just is hardcoded 0 in the C binding)
;;; axis=20 → log y axis only (semi-log)
(plot-env-log 0.0 5.5 1e-3 2.0 20)
(plot-labels "x" "e^{-x}  (log scale)" "Exponential decay — semi-log")
(plot-color 2)
(plot-line log-x log-y)
(plot-end)

(check-pred "SVG log-scale file created"
            (lambda (path)
              (guard (exn (#t #f))
                (let* ((p (open-input-file path))
                       (c (read-char p)))
                  (close-input-port p)
                  (not (eof-object? c)))))
            out-log)

;;; ── X-direction error bars (plot-error-x) ───────────────────────────────

(define out-xerr "/tmp/curry-plplot-xerrbar.svg")

(plot-device "svg")
(plot-output out-xerr)
(plot-init)

(define xe-y    '(1.0 2.0 3.0 4.0 5.0))
(define xe-xmid '(1.2 2.1 2.9 4.0 5.2))
(define xe-xlo  '(0.9 1.7 2.5 3.6 4.8))
(define xe-xhi  '(1.5 2.5 3.3 4.4 5.6))

(plot-env 0.0 6.5 0.0 6.0)
(plot-labels "x" "y" "X error bars")
(plot-color 4)
(plot-points xe-xmid xe-y 4)
(plot-color 1)
(plot-error-x xe-xlo xe-xhi xe-y)
(plot-end)

(check-pred "SVG x-errbar file created"
            (lambda (path)
              (guard (exn (#t #f))
                (let* ((p (open-input-file path))
                       (c (read-char p)))
                  (close-input-port p)
                  (not (eof-object? c)))))
            out-xerr)

;;; ── Custom axis box (plot-box) ───────────────────────────────────────────

(define out-box "/tmp/curry-plplot-box.svg")

(plot-device "svg")
(plot-output out-box)
(plot-init)

;;; Draw axes manually instead of using plot-env
(plot-env 0.0 (* 2.0 pi) -1.5 1.5)
(plot-box "bcnst" "bcnstv")   ; ticks + labels both axes
(plot-color 3)
(plot-line (linspace 0.0 (* 2.0 pi) 100)
           (map sin (linspace 0.0 (* 2.0 pi) 100)))
(plot-color 4)
(plot-line (linspace 0.0 (* 2.0 pi) 100)
           (map cos (linspace 0.0 (* 2.0 pi) 100)))
(plot-end)

(check-pred "SVG custom-box file created"
            (lambda (path)
              (guard (exn (#t #f))
                (let* ((p (open-input-file path))
                       (c (read-char p)))
                  (close-input-port p)
                  (not (eof-object? c)))))
            out-box)

;;; ── 3D sinc surface (plot-3d-surface) ────────────────────────────────────
;;; sinc(r) = sin(r)/r — the classic demo for 3-D surface plots.

(define out-surf "/tmp/curry-plplot-surface.svg")

(plot-device "svg")
(plot-output out-surf)
(plot-init)

(define sinc-n 20)
(define sinc-xs (linspace -5.0 5.0 sinc-n))
(define sinc-ys (linspace -5.0 5.0 sinc-n))

(define (sinc r)
  (if (< (abs r) 1e-9) 1.0 (/ (sin r) r)))

;;; Build z as a list of rows (one row per y value, nx values per row)
(define sinc-zs
  (map (lambda (y)
         (map (lambda (x)
                (sinc (sqrt (+ (* x x) (* y y)))))
              sinc-xs))
       sinc-ys))

(plot-3d-init -5.0 5.0 -5.0 5.0 -0.3 1.1 35.0 225.0)
(plot-3d-box "X" "Y" "sinc(r)")
(plot-color 9)
(plot-3d-surface sinc-xs sinc-ys sinc-zs)
(plot-end)

(check-pred "SVG 3D-surface file created"
            (lambda (path)
              (guard (exn (#t #f))
                (let* ((p (open-input-file path))
                       (c (read-char p)))
                  (close-input-port p)
                  (not (eof-object? c)))))
            out-surf)

;;; ── 3D mesh (plot-3d-mesh) ───────────────────────────────────────────────
;;; Saddle surface  z = x² − y²

(define out-mesh "/tmp/curry-plplot-mesh.svg")

(plot-device "svg")
(plot-output out-mesh)
(plot-init)

(define mesh-n 15)
(define mesh-xs (linspace -2.0 2.0 mesh-n))
(define mesh-ys (linspace -2.0 2.0 mesh-n))
(define mesh-zs
  (map (lambda (y)
         (map (lambda (x) (- (* x x) (* y y))) mesh-xs))
       mesh-ys))

(plot-3d-init -2.0 2.0 -2.0 2.0 -4.5 4.5 40.0 135.0)
(plot-3d-box "X" "Y" "x²−y²")
(plot-color 2)
(plot-3d-mesh mesh-xs mesh-ys mesh-zs)
(plot-end)

(check-pred "SVG 3D-mesh file created"
            (lambda (path)
              (guard (exn (#t #f))
                (let* ((p (open-input-file path))
                       (c (read-char p)))
                  (close-input-port p)
                  (not (eof-object? c)))))
            out-mesh)

;;; ── plot-clear and plot-flush smoke test ─────────────────────────────────
;;; Just verify neither procedure raises.

(define out-cf "/tmp/curry-plplot-clearflush.svg")

(plot-device "svg")
(plot-output out-cf)
(plot-init)
(plot-env 0.0 1.0 0.0 1.0)
(plot-clear)
(plot-env 0.0 1.0 0.0 1.0)
(plot-labels "x" "y" "After clear")
(plot-color 5)
(plot-line '(0.0 1.0) '(0.0 1.0))
(plot-flush)
(plot-end)

(check-pred "SVG clear/flush file created"
            (lambda (path)
              (guard (exn (#t #f))
                (let* ((p (open-input-file path))
                       (c (read-char p)))
                  (close-input-port p)
                  (not (eof-object? c)))))
            out-cf)

;;; ── Summary ──────────────────────────────────────────────────────────────

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
