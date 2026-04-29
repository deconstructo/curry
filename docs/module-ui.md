# Module: (curry ui)

GTK4 graphical user interface: windows, menus, controls, and a Cairo 2D drawing canvas. Suited for building interactive applications — simulations, data visualisers, editors.

## Installation

```bash
# Debian/Ubuntu
sudo apt install libgtk-4-dev

# macOS
brew install gtk4
```

Enable: `-DBUILD_MODULE_UI=ON` (off by default).

## Import

```scheme
(import (curry ui))
```

## Event loop

GTK4 requires its event loop to run on the main thread.

```scheme
(run-event-loop)    ; start GTK main loop (blocks until quit)
(quit-event-loop)   ; request exit
```

Typically you create your windows, wire up callbacks, then call `run-event-loop` at the end of your script.

## Windows

```scheme
(make-window title width height)  ; → window handle
(window-show!  win)
(window-hide!  win)
(window-set-title! win title)
(window-register! win)            ; register with GtkApplication (required for menus)
(window-on-close!  win thunk)     ; called when user closes the window
(window-on-key!    win proc)      ; (proc keyname) on keypress
(window-on-resize! win proc)      ; (proc width height) on resize
(window-width  win)               ; → integer
(window-height win)               ; → integer
```

## Drawing canvas

Each window contains a single GTK drawing area.

```scheme
(window-canvas win)               ; → canvas handle
(canvas-on-draw! canvas proc)     ; (proc cr width height) on expose
(canvas-redraw! canvas)           ; schedule a redraw
```

The `proc` in `canvas-on-draw!` receives a Cairo context `cr`, the canvas width, and height. Use the `cr-*` procedures below to draw.

## Cairo drawing

The Cairo context is only valid during the draw callback.

### Colour and style

```scheme
(cr-set-source-rgb!  cr r g b)           ; r g b in [0.0, 1.0]
(cr-set-source-rgba! cr r g b a)         ; a = opacity [0.0, 1.0]
(cr-set-line-width!  cr width)
(cr-set-font-size!   cr size)
```

### Paths

```scheme
(cr-new-path!  cr)
(cr-move-to!   cr x y)
(cr-line-to!   cr x y)
(cr-curve-to!  cr x1 y1 x2 y2 x y)      ; cubic Bézier
(cr-arc!       cr cx cy radius angle1 angle2)
(cr-rectangle! cr x y width height)
(cr-close-path! cr)
```

### Painting

```scheme
(cr-stroke!         cr)    ; stroke path, clear it
(cr-fill!           cr)    ; fill path, clear it
(cr-fill-preserve!  cr)    ; fill path, keep it for stroking
(cr-paint!          cr)    ; paint entire surface with current source
(cr-clip!           cr)    ; use current path as clip region
(cr-reset-clip!     cr)
```

### Text

```scheme
(cr-text!         cr x y string)          ; draw text at (x, y)
(cr-text-extents  cr string)              ; → (width . height)
```

### Transforms

```scheme
(cr-save!       cr)    ; push graphics state
(cr-restore!    cr)    ; pop graphics state
(cr-translate!  cr dx dy)
(cr-scale!      cr sx sy)
(cr-rotate!     cr radians)
```

## Widgets and sidebar

Each window has a vertical sidebar for controls.

```scheme
(window-sidebar win)               ; → box handle
(box-add! box widget)              ; add widget to a box
```

### Widget constructors

```scheme
(make-button  label proc)          ; proc called on click (no args)
(make-toggle  label initial proc)  ; proc called with bool on toggle
(make-slider  label min max step initial)  ; horizontal slider
(make-label   text)
(make-entry   placeholder proc)    ; proc called with string on change
(make-separator)
```

### Widget accessors / mutators

```scheme
(widget-set-label! widget text)
(slider-value      slider)          ; → flonum
(slider-set-value! slider val)
```

## Menus

```scheme
(make-menu title)                  ; → menu handle
(menu-add-item!      menu label action-name)
(menu-add-separator! menu)
(window-add-menu!    win menu)     ; attach menu bar to window
```

Actions are named strings; wire them up via GTK GAction (currently requires `window-register!` and GtkApplication).

## Dialogs

```scheme
(message-dialog win type title message)
```

`type` is one of `'info`, `'warning`, `'error`, `'question`.

## Example: gravity simulation viewer

```scheme
(import (curry ui))

; Simulation state
(define bodies '((0.0 0.0 0.0 0.0)     ; x y vx vy
                 (200.0 0.0 0.0 1.5)))

(define (step-simulation! dt D)
  ; ... physics in dimension D ...
  )

(define canvas #f)
(define D 3.0)

; Window
(define win (make-window "Gravity" 800 600))

; Draw callback
(define (draw cr w h)
  (cr-set-source-rgb! cr 0.05 0.05 0.1)
  (cr-paint! cr)
  (cr-set-source-rgb! cr 1.0 0.9 0.2)
  (for-each
    (lambda (b)
      (let ((x (+ (/ w 2) (car b)))
            (y (+ (/ h 2) (cadr b))))
        (cr-arc! cr x y 5 0 6.2832)
        (cr-fill! cr)))
    bodies))

; Sidebar controls
(define sidebar (window-sidebar win))
(box-add! sidebar (make-label "Dimension D:"))
(define d-slider (make-slider "D" 2.0 5.0 0.01 3.0))
(box-add! sidebar d-slider)
(box-add! sidebar
  (make-button "Step"
    (lambda ()
      (step-simulation! 0.016 (slider-value d-slider))
      (canvas-redraw! canvas))))
(box-add! sidebar
  (make-button "Quit" quit-event-loop))

; Canvas
(set! canvas (window-canvas win))
(canvas-on-draw! canvas draw)

(window-show! win)
(run-event-loop)
```

## Notes

- All GTK operations must be called from the main thread (the thread that calls `run-event-loop`). Use `actor-send` to pass data from worker actors to the main thread, then call `canvas-redraw!` from a GTK idle callback (not yet exposed; use a timer as a workaround).
- The Cairo context `cr` is only valid inside a `canvas-on-draw!` callback; do not store it.
- GTK4 on macOS requires `brew install gtk4` and may need `DISPLAY` or Wayland/X11 configuration. Native macOS rendering (Metal) is not yet used — GTK4 uses its own renderer.
