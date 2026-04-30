# Module: (curry plplot)

Scientific 2D/3D plotting using [PLplot](http://plplot.sourceforge.net/) — a cross-platform plotting library that can render to PNG, PDF, SVG, X11, and Qt windows among other drivers.

## Import

```scheme
(import (curry plplot))
```

## Requirements

```bash
sudo apt install libplplot-dev
```

Build with:
```bash
cmake -B build -DBUILD_MODULE_PLPLOT=ON
```

## Initialisation and output

### `(pl-init)`

Initialise PLplot. Call once before any drawing.

### `(pl-end)`

Close the current output device and finalise the plot.

### `(pl-sdev driver)`

Set the output driver before `pl-init`. Common drivers: `"pngcairo"`, `"pdfcairo"`, `"svgcairo"`, `"xwin"`, `"qtwidget"`.

### `(pl-sfnam filename)`

Set the output filename (used by file-based drivers like `"pngcairo"`).

### `(pl-adv page)`

Advance to the next page (or sub-page). Pass `0` to advance to a fresh page.

## Viewport and window

### `(pl-env xmin xmax ymin ymax just axis)`

Set up a standard viewport, axes, and labels in one call.
- `just`: 0 = independent axes, 1 = equal scales, 2 = equal + square box
- `axis`: 0 = box only, 1 = box + axes, 2 = box + axes + numeric labels

### `(pl-vport xmin xmax ymin ymax)`

Set the viewport in normalised device coordinates (0–1).

### `(pl-wind xmin xmax ymin ymax)`

Set the world coordinate window.

## Axes and labels

### `(pl-box xopt yopt xtick nxsub ytick nysub)`

Draw axes with tick marks. `xopt`/`yopt` are option strings (e.g. `"bcnst"` for a framed, labelled axis).

### `(pl-lab xlabel ylabel title)`

Set the x-axis label, y-axis label, and plot title.

## Line plots

### `(pl-line xs ys)`

Draw a line through the points given by lists `xs` and `ys`.

### `(pl-lines-points xs ys code)`

Draw points with symbol code `code` (integer). Negative codes draw lines between points.

### `(pl-string xs ys text)`

Draw the string `text` at each point `(x, y)`.

## Colour and style

### `(pl-col0 n)`

Select a colour from the default colour palette (0–15).

### `(pl-width w)`

Set the line width in units of 0.005 mm (integer).

### `(pl-lsty style)`

Set the line style: 1=solid, 2=dashed, 3=dotted, etc.

## 3D surface plots

### `(pl-mesh xs ys zs nx ny)`

Draw a 3D mesh surface. `xs` and `ys` are 1D lists (axis tick values), `zs` is a 2D list of lists (ny rows × nx columns).

### `(pl-surf3d xs ys zs nx ny opt clev nlev)`

3D shaded surface with optional contour lines. `opt` is a bitmask; pass `0` for defaults. `clev` is a list of contour levels, `nlev` is the count.

## Contour plots

### `(pl-cont zs nx ny kx lx ky ly clev nlev)`

Draw a 2D contour map. `zs` is a 2D list of lists (ny rows × nx columns). `kx`/`lx` and `ky`/`ly` are column/row index ranges (1-based). `clev` is a list of contour levels.

## Image / colour map

### `(pl-shade zs nx ny xmin xmax ymin ymax shade-min shade-max fill-pattern dx dy)`

Draw a colour-shaded band.

### `(pl-image zs nx ny xmin xmax ymin ymax zmin zmax dxmin dxmax dymin dymax)`

Display a 2D array `zs` as a false-colour image.

## Error bars

### `(pl-errx xs y1s y2s)`

Draw horizontal error bars at `y = y1..y2` for each x.

### `(pl-erry xs y1s y2s)`

Draw vertical error bars.

## Text

### `(pl-ptex x y dx dy just text)`

Draw `text` at world coordinate `(x,y)`. `dx`/`dy` set the text direction, `just` sets horizontal justification (0=left, 0.5=centre, 1=right).

## Fill and fill patterns

### `(pl-fill xs ys)`

Fill the polygon defined by `xs` and `ys` with the current colour.

### `(pl-pat nlin inc del)`

Set fill pattern. `nlin`=number of line families, `inc` and `del` are lists of inclinations and spacings.

## Miscellaneous

### `(pl-schr ht scale)`

Set character height and scale.

### `(pl-smaj dx dy)`

Set major tick mark size.

### `(pl-smin dx dy)`

Set minor tick mark size.

### `(pl-flush)`

Flush the output buffer (useful for interactive drivers).

## Example — line plot to PNG

```scheme
(import (curry plplot))

(pl-sdev "pngcairo")
(pl-sfnam "output.png")
(pl-init)
(pl-adv 0)
(pl-env -3.14 3.14 -1.2 1.2 0 1)
(pl-lab "x" "sin(x)" "Sine wave")
(pl-col0 1)

(let* ((n   200)
       (xs  (map (lambda (i) (* (- i 100) (/ 3.14159 100.0))) (iota n)))
       (ys  (map sin xs)))
  (pl-line xs ys))

(pl-end)
```

## Example — trajectory phase portrait

```scheme
(import (curry plplot))
(import (curry regex))

(pl-sdev "pdfcairo")
(pl-sfnam "phase.pdf")
(pl-init)
(pl-adv 0)
(pl-env -2.0 2.0 -2.0 2.0 0 1)
(pl-lab "x" "dx/dt" "Phase portrait")

; draw multiple trajectories
(for-each
  (lambda (x0)
    (let loop ((x x0) (v 0.0) (pts-x '()) (pts-y '()) (i 0))
      (if (> i 500)
        (begin
          (pl-col0 (+ 1 (modulo (inexact->exact (floor (* x0 3))) 7)))
          (pl-line (reverse pts-x) (reverse pts-y)))
        (let ((ax (- (- x) (* 0.1 v))))
          (loop (+ x (* 0.02 v))
                (+ v (* 0.02 ax))
                (cons x pts-x)
                (cons v pts-y)
                (+ i 1))))))
  '(-1.8 -1.2 -0.6 0.0 0.6 1.2 1.8))

(pl-end)
```
