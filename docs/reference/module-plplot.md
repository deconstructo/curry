# Module: (curry plplot)

Scientific 2D/3D plotting using [PLplot](http://plplot.sourceforge.net/) — a cross-platform plotting library that can render to PNG, PDF, SVG, X11, and Qt windows among other drivers.

## Import

```scheme
(import (curry plplot))
```

## Requirements

**Linux (Debian / Ubuntu)**
```bash
sudo apt install libplplot-dev
```

**macOS (Homebrew)**
```bash
brew install plplot
```

Build with:
```bash
cmake -B build -DBUILD_MODULE_PLPLOT=ON
cmake --build build -j$(sysctl -n hw.logicalcpu)   # macOS
cmake --build build -j$(nproc)                      # Linux
```

### macOS driver notes

The `xwin` driver requires X11/XQuartz, which is not installed by default on macOS. Use file-based or Qt drivers instead:

| Driver | Output | Notes |
|---|---|---|
| `"pngcairo"` | PNG file | Recommended for scripts |
| `"pdfcairo"` | PDF file | Good for print-quality output |
| `"svgcairo"` | SVG file | Scalable vector output |
| `"qtwidget"` | Interactive window | Requires `brew install qt@6` and the qt6 module |

Example — render to PNG on macOS:
```scheme
(plot-device "pngcairo")
(plot-output "plot.png")
(plot-init)
...
(plot-end)
```

## Initialisation and output

### `(plot-init)`

Initialise PLplot. Call once before any drawing.

### `(plot-end)`

Close the current output device and finalise the plot — this is what actually flushes a file-based driver (`pngcairo`/`pdfcairo`/`svgcairo`) to disk.

### `(plot-device driver)`

Set the output driver before `plot-init`. Common drivers: `"pngcairo"`, `"pdfcairo"`, `"svgcairo"`, `"xwin"`, `"qtwidget"`.

### `(plot-output filename)`

Set the output filename (used by file-based drivers like `"pngcairo"`).

### `(plot-font-size scale)`

Set the character/font scale factor (a multiplier on the default size; `1.0` is normal).

### `(plot-advance)`

Advance to a fresh page/sub-page.

## Viewport and window

### `(plot-env xmin xmax ymin ymax)`

Set up a standard viewport, world-coordinate window, and framed/labelled box in one call — the usual first thing to call after `plot-init` for a 2D plot.

### `(plot-env-log xmin xmax ymin ymax log-axis)`

Like `plot-env`, but `log-axis` selects logarithmic scaling: `0` = linear (both axes), `10` = log Y only, `20` = log X only, `30` = log both. (Passed straight through to PLplot's `plenv`.)

## Axes and labels

### `(plot-box xopt yopt)`

Draw axes with tick marks. `xopt`/`yopt` are PLplot axis-option strings (e.g. `"bcnst"` for a framed, labelled, numbered axis with subticks) — see [PLplot's `plbox` docs](https://plplot.sourceforge.net/docbook-manual/plplot-html-5.15.0/plbox.html) for the full option-letter reference.

### `(plot-labels xlabel ylabel title)`

Set the x-axis label, y-axis label, and plot title.

## Line and point plots

### `(plot-line xs ys)`

Draw a line through the points given by equal-length number lists `xs` and `ys`.

### `(plot-points xs ys symbol)`

Draw a point marker (integer PLplot symbol code) at each `(x, y)` from equal-length number lists `xs` and `ys`.

### `(plot-histogram data min max nbins)`

Draw a histogram of `data` (a number list) binned into `nbins` bins between `min` and `max`.

## Error bars

### `(plot-error-y xs ymins ymaxs)`

Draw vertical error bars from `ymins` to `ymaxs` at each x (three equal-length number lists).

### `(plot-error-x xmins xmaxs ys)`

Draw horizontal error bars from `xmins` to `xmaxs` at each y (three equal-length number lists).

## Colour and style

### `(plot-color n)`

Select a colour from the default colour palette (0–15).

### `(plot-color-rgb r g b)`

Define custom colour index 15 as RGB `r g b` (each 0–255) and select it.

### `(plot-width w)`

Set the line width in units of 0.005 mm (integer).

### `(plot-background-color r g b)`

Set the page background colour (RGB, each 0–255).

## 3D plots

### `(plot-3d-init x1 x2 y1 y2 z1 z2 alt az)`

Set up a 3D viewport and coordinate box: x/y/z axis ranges plus altitude and azimuth (viewing angles, in degrees) for the 3D projection. Call once before any 3D drawing.

### `(plot-3d-box xlabel ylabel zlabel)`

Draw the 3D bounding box, axes, and tick labels, with the given x/y/z axis label text (box style/ticks are fixed by the wrapper — unlike `plot-box`, there's no option-string argument here).

### `(plot-3d-line xs ys zs)`

Draw a 3D line through the points given by equal-length number lists `xs`, `ys`, `zs`.

### `(plot-3d-surface xs ys zss)`

Draw a shaded 3D surface. `xs`/`ys` are 1D number lists (axis tick values, lengths *nx*/*ny*); `zss` is a list of *ny* rows, each a list of *nx* numbers.

### `(plot-3d-mesh xs ys zss)`

Draw a 3D wireframe mesh — same argument shape as `plot-3d-surface`, drawn as a mesh (base + top face lines) instead of a shaded surface.

## Layout

### `(plot-subplot nx ny)`

Divide the page into an `nx` × `ny` grid of sub-plots. Call `plot-advance` to move between them.

## Annotations

### `(plot-text x y text)`

Draw `text` at world coordinate `(x, y)`, centred and written along the x-axis direction.

### `(plot-mtex side disp pos just text)`

Write `text` relative to a viewport edge. `side` is one of `"t"`/`"b"`/`"l"`/`"r"` (top/bottom/left/right, PLplot's `plmtex` side codes); `disp` is the displacement from the edge in character heights; `pos` is the position along the edge (0–1); `just` is justification (0=left, 0.5=centre, 1=right along the edge).

## Utilities

### `(plot-clear)`

Clear the current page/subpage.

### `(plot-flush)`

Flush the output buffer (useful for interactive drivers).

### `(plot-version)`

Return the PLplot library version as a string.

### `(plot-page-dimensions)`

Return `(width . height)` (a pair of integers, device pixels) of the current page.

## Example — line plot to PNG

```scheme
(import (scheme base) (srfi 1) (curry plplot))

(plot-device "pngcairo")
(plot-output "output.png")
(plot-init)
(plot-env -3.14 3.14 -1.2 1.2)
(plot-labels "x" "sin(x)" "Sine wave")
(plot-color 1)

(let* ((n   200)
       (xs  (map (lambda (i) (* (- i 100) (/ 3.14159 100.0))) (iota n)))
       (ys  (map sin xs)))
  (plot-line xs ys))

(plot-end)
```

## Example — y = x^2

```scheme
(import (scheme base) (srfi 1) (curry plplot))

(plot-device "pngcairo")
(plot-output "y_x2.png")
(plot-init)
(plot-env -10.0 10.0 0.0 100.0)
(plot-labels "x" "y" "y = x^2")

(let* ((xs (map exact->inexact (iota 41 -10)))
       (ys (map (lambda (x) (* x x)) xs)))
  (plot-line xs ys))

(plot-end)
```

## Example — trajectory phase portrait

```scheme
(import (scheme base) (srfi 1) (curry plplot))

(plot-device "pdfcairo")
(plot-output "phase.pdf")
(plot-init)
(plot-env -2.0 2.0 -2.0 2.0)
(plot-labels "x" "dx/dt" "Phase portrait")

; draw multiple trajectories, one per starting position
(for-each
  (lambda (x0)
    (let loop ((x x0) (v 0.0) (pts-x '()) (pts-y '()) (i 0))
      (if (> i 500)
        (begin
          (plot-color (+ 1 (modulo (inexact->exact (floor (* x0 3))) 7)))
          (plot-line (reverse pts-x) (reverse pts-y)))
        (let ((ax (- (- x) (* 0.1 v))))
          (loop (+ x (* 0.02 v))
                (+ v (* 0.02 ax))
                (cons x pts-x)
                (cons v pts-y)
                (+ i 1))))))
  '(-1.8 -1.2 -0.6 0.0 0.6 1.2 1.8))

(plot-end)
```

## Example — 3D surface

```scheme
(import (scheme base) (srfi 1) (curry plplot))

(plot-device "pngcairo")
(plot-output "surface.png")
(plot-init)
(plot-3d-init -3.0 3.0 -3.0 3.0 -1.0 1.0 45.0 30.0)
(plot-3d-box "x" "y" "z")
(plot-mtex "t" 1.0 0.5 0.5 "z = 0.3 sin(x + y)")

(let* ((xs  (map exact->inexact (iota 20 -3 (/ 6.0 19))))
       (ys  (map exact->inexact (iota 20 -3 (/ 6.0 19))))
       (zss (map (lambda (y) (map (lambda (x) (* 0.3 (sin (+ x y)))) xs)) ys)))
  (plot-3d-surface xs ys zss))

(plot-end)
```

## Using this from the Jupyter kernel

`curry_jupyter` ([docs/reference/jupyter-kernel.md](jupyter-kernel.md)) has a `(jupyter-display-file path)` builtin that publishes a PNG/JPEG/SVG file as an inline image in the notebook. Add one line after `(plot-end)`:

```scheme
(import (scheme base) (srfi 1) (curry plplot))

(plot-device "pngcairo")
(plot-output "y_x2.png")
(plot-init)
(plot-env -10.0 10.0 0.0 100.0)
(plot-labels "x" "y" "y = x^2")
(let* ((xs (map exact->inexact (iota 41 -10)))
       (ys (map (lambda (x) (* x x)) xs)))
  (plot-line xs ys))
(plot-end)

(jupyter-display-file "y_x2.png")
```

(`(srfi 1)` is required for `iota` — confirmed by running this exact cell through the kernel with only `(curry plplot)` imported: `unbound variable: iota`.)

`(jupyter-display-file ...)` only exists inside the Jupyter kernel — it's not part of curry's core language, so the plain `curry` REPL/CLI doesn't have it and this line would need removing (or wrapping in a check) to run the same script outside a notebook.
