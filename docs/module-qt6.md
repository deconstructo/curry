# Module: `(curry qt6)`

*v0.7.7 — 2026-05-23*

Qt6-based windowing, 2D graphics, widgets, and 4D projection math.

## Requirements

```bash
# Debian/Ubuntu
sudo apt install qt6-base-dev qt6-base-dev-tools

# macOS (Homebrew)
brew install qt@6
cmake -B build -DBUILD_MODULE_QT6=ON \
      -DCMAKE_PREFIX_PATH="$(brew --prefix qt@6)"
```

Build flag: `-DBUILD_MODULE_QT6=ON` (off by default).

## Import

```scheme
(import (curry qt6))
```

## Quickstart — canvas window

```scheme
(import (curry qt6))

(define win (make-window "Hello" 800 600))

(window-on-realize! win
  (lambda ()
    (let ((canvas (window-canvas win)))
      (canvas-on-draw! canvas
        (lambda (painter w h)
          (gfx-clear! painter 0.1 0.1 0.2)
          (gfx-set-color! painter 1.0 0.8 0.2 1.0)
          (gfx-fill-circle! painter (/ w 2) (/ h 2) 80)))
      (canvas-redraw! canvas))))

(window-on-close! win quit-event-loop)
(window-show! win)
(run-event-loop)
```

## Quickstart — plain widget window (IDE pattern)

```scheme
(import (curry qt6))

(define win (make-plain-window "My App" 900 600))

(define sp (make-splitter 'vertical))
(define editor (make-text-edit))
(define output (make-text-edit))
(text-edit-set-read-only! output #t)

(splitter-add! sp editor)
(splitter-add! sp output)
(splitter-set-sizes! sp '(400 160))
(window-set-central-widget! win sp)

(define mb (window-menu-bar win))
(define file-menu (menubar-add-menu! mb "File"))
(menu-add-action! file-menu "Quit" quit-event-loop "Ctrl+Q")

(window-on-close! win quit-event-loop)
(window-show! win)
(run-event-loop)
```

## Window management

### Canvas window (`make-window`)

Creates a `QMainWindow` with a fixed-width (210 px) sidebar on the left and an OpenGL canvas filling the rest. Use this for graphics-heavy apps.

```scheme
(make-window title width height)   ; → window
```

### Plain window (`make-plain-window`)

Creates a `QMainWindow` with no preset central widget. Use `window-set-central-widget!` to attach any widget — a splitter, a text editor, a tab widget, etc.

```scheme
(make-plain-window title width height)        ; → window
(window-set-central-widget! win widget)       ; set the central area
```

### Common window operations

```scheme
(window-show!      win)
(window-hide!      win)
(window-set-title! win title)

(window-on-close!   win proc)    ; proc called (no args) when ✕ is clicked
(window-on-key!     win proc)    ; proc: (lambda (key-string modifiers) ...)
(window-on-realize! win proc)    ; proc called once after the window first appears

(window-canvas     win)          ; → canvas  (make-window only)
(window-sidebar    win)          ; → vbox    (make-window only)
(window-menu-bar   win)          ; → menubar
(window-status-bar win)          ; → statusbar
(window-add-toolbar! win)        ; → toolbar (optional area symbol: 'top 'bottom 'left 'right)

(run-event-loop)                 ; start Qt event loop (blocks until quit)
(quit-event-loop)                ; exit the event loop
(qt-process-events)              ; flush pending events without blocking (useful in long ops)
```

Key strings: `"a"`, `"space"`, `"Return"`, `"Escape"`, `"Left"`, `"Right"`, `"Up"`, `"Down"`, `"F1"` … `"F12"`.  
Modifier list: `'(shift ctrl alt meta)` (subset present).

## Canvas and drawing

```scheme
(canvas-on-draw!   canvas proc)  ; proc: (lambda (painter width height) ...)
(canvas-on-mouse!  canvas proc)  ; proc: (lambda (event button x y mods) ...)
                                 ;   event: 'press | 'release | 'move | 'double-press
                                 ;   button: 'left | 'right | 'middle | 'none
(canvas-on-scroll! canvas proc)  ; proc: (lambda (dx dy x y mods) ...)
(canvas-redraw!    canvas)       ; schedule a repaint

(qt-painter-width  painter)      ; → number
(qt-painter-height painter)      ; → number
(qt-widget-width   widget)       ; → number
(qt-widget-height  widget)
(qt-gpu?           canvas)       ; → #t if OpenGL initialised successfully
```

## 2D graphics — painter API

All `gfx-*` procedures take `painter` as the first argument. `painter` is valid only inside a `canvas-on-draw!` callback. Color components are floats 0.0–1.0.

### Color and pen

```scheme
(gfx-set-color!     painter r g b)          ; fill and stroke, opaque
(gfx-set-color!     painter r g b a)        ; fill and stroke with alpha
(gfx-set-pen-color! painter r g b a)        ; stroke only
(gfx-set-pen-width! painter w)
(gfx-set-antialias! painter bool)           ; default: #t
(gfx-set-blend!     painter mode)           ; 'normal | 'add | 'multiply | 'screen | 'overlay
(gfx-set-font!      painter)                ; reset to default
(gfx-set-font!      painter family)
(gfx-set-font!      painter family size)
(gfx-set-font!      painter family size bold?)
(gfx-set-font!      painter family size bold? italic?)
```

### State stack

```scheme
(gfx-save!    painter)
(gfx-restore! painter)
```

### Transforms

```scheme
(gfx-translate! painter dx dy)
(gfx-rotate!    painter angle)   ; radians
(gfx-scale!     painter sx sy)
```

### Shapes

```scheme
(gfx-clear!        painter)               ; fill with black
(gfx-clear!        painter r g b)
(gfx-clear!        painter r g b a)
(gfx-fill-rect!    painter x y w h)
(gfx-draw-rect!    painter x y w h)       ; stroke only
(gfx-fill-circle!  painter cx cy r)
(gfx-draw-circle!  painter cx cy r)
(gfx-fill-ellipse! painter cx cy rx ry)
(gfx-draw-ellipse! painter cx cy rx ry)
(gfx-draw-line!    painter x1 y1 x2 y2)
(gfx-draw-arc!     painter cx cy r start span)   ; angles in radians; 0 = East, CCW positive
(gfx-fill-pie!     painter cx cy r start span)

; Polygons — pts is a list of (x . y) pairs
(gfx-fill-polygon! painter pts)
(gfx-draw-polygon! painter pts)

(gfx-draw-text! painter x y string)
```

### Batch drawing (efficient for many primitives)

```scheme
; N points from parallel x-vector and y-vector, uniform color and diameter
(gfx-draw-points! painter x-vec y-vec r g b a size)

; N line segments from flat coord-vector #(x0 y0 x1 y1 x2 y2 x3 y3 ...)
; Each group of 4 floats is one segment
(gfx-draw-lines! painter coords r g b a width)

; N triangles from flat coord-vector #(x0 y0 x1 y1 x2 y2 ...)
; Each group of 6 floats is one filled triangle
(gfx-fill-triangles! painter coords r g b a)

; Blit an RGBA pixel buffer (bytevector, row-major, 4 bytes/pixel)
(gfx-draw-image! painter dst-x dst-y dst-w dst-h bvec img-w img-h)
```

## Widgets

All widgets are opaque values that can be placed in layout containers.

### Layout containers

```scheme
(make-vbox)                        ; vertical stack
(make-hbox)                        ; horizontal row
(make-group-box title)             ; titled group with vbox inside
(make-tabs)                        ; tab widget

(box-add!    sidebar-or-vbox widget)
(layout-add! vbox-or-hbox widget)
(tabs-add!   tabs widget label)
```

### Splitter

A resizable pane divider. The result is a plain widget and can be passed to `window-set-central-widget!` or nested inside another layout.

```scheme
(make-splitter)               ; vertical by default
(make-splitter 'vertical)
(make-splitter 'horizontal)

(splitter-add!       splitter widget)
(splitter-set-sizes! splitter '(h1 h2 ...))  ; pixel sizes for each pane
```

### Label and separator

```scheme
(make-label      text)             ; word-wrapping label
(label-set-text! label text)
(make-separator)                   ; horizontal rule
```

### Button

```scheme
(make-button label proc)           ; proc called (no args) on click
```

### Toggle (checkbox)

```scheme
(make-toggle label initial-bool proc)   ; proc: (lambda (on?) ...)
```

### Slider

```scheme
(make-slider label lo hi step initial)
(make-slider label lo hi step initial on-change)   ; on-change: (lambda (v) ...)
(slider-value      slider)
(slider-set-value! slider v)
```

### Dropdown

```scheme
(make-dropdown items initial-index proc)   ; items: list of strings
                                           ; proc: (lambda (index) ...)
(dropdown-index     dd)                    ; → selected index
(dropdown-set-index! dd i)
(dropdown-selected  dd)                    ; alias for dropdown-index
```

### Radio group

```scheme
(make-radio-group items initial-index proc)  ; proc: (lambda (index) ...)
(radio-index group)                          ; → selected index
```

### Spin box

```scheme
(make-spin-box lo hi step initial proc)    ; proc: (lambda (v) ...)
(spin-value      sb)
(spin-set-value! sb v)
```

### Single-line text input

```scheme
(make-text-input placeholder proc)         ; proc: (lambda (text) ...)  fired on every keystroke
(text-value       input)                   ; → string
(text-set-value!  input text)
```

### Multi-line text editor

`make-text-edit` creates a `QPlainTextEdit` with a monospace font (Menlo/Courier) and 4-space tab stops. Suitable for code editors and output logs.

```scheme
(make-text-edit)                           ; → text-edit widget

(text-edit-text          te)              ; → string (full content)
(text-edit-set-text!     te string)       ; replace all content
(text-edit-append!       te string)       ; append a line (efficient for output logs)
(text-edit-clear!        te)              ; clear all content
(text-edit-set-read-only! te bool)        ; lock for use as an output pane
(text-edit-on-change!    te proc)         ; proc called (no args) on any edit
```

### Progress bar

```scheme
(make-progress-bar lo hi initial)
(progress-set! pb v)
```

### Universal widget properties

```scheme
(widget-set-style!    widget css-string)   ; Qt stylesheet
(widget-set-enabled!  widget bool)
(widget-set-visible!  widget bool)
(widget-set-tooltip!  widget text)
(widget-set-min-size! widget w h)
(widget-set-max-size! widget w h)
```

## Menus and toolbar

```scheme
(define mb        (window-menu-bar win))
(define file-menu (menubar-add-menu! mb "File"))

(menu-add-action!    menu title proc)              ; proc called (no args) on trigger
(menu-add-action!    menu title proc shortcut)     ; shortcut e.g. "Ctrl+S"
(menu-add-menu!      menu title)                   ; → submenu
(menu-add-separator! menu)

(define tb (window-add-toolbar! win))              ; → toolbar (default: top)
(define tb (window-add-toolbar! win 'bottom))      ; 'top | 'bottom | 'left | 'right
(toolbar-add-action!    tb label proc)
(toolbar-add-separator! tb)
```

On macOS the menu bar is automatically moved to the system menu bar.

## Status bar

```scheme
(define sb (window-status-bar win))
(statusbar-set-text! sb "Ready")
```

## File dialogs

Both procedures return a path string on confirmation, or `#f` if the user cancels.

```scheme
(file-open-dialog)                                ; title and filter default to "All Files (*)"
(file-open-dialog title)
(file-open-dialog title filter)                   ; e.g. "Curry Files (*.scm);;All Files (*)"

(file-save-dialog)
(file-save-dialog title)
(file-save-dialog title filter)
```

## Timer

```scheme
(define t (make-timer interval-ms proc))   ; proc called (no args) on each tick
(timer-start!        t)
(timer-stop!         t)
(timer-set-interval! t ms)
```

## 4D projection math

Pure math — no GPU required.

```scheme
(define proj (make-4d-projector))            ; default fov4d = 4.0
(define proj (make-4d-projector fov4d))
(define proj (make-4d-projector fov4d fov3d))

; Project a 4D point (Scheme vector of 4 flonums) to a 3D point (vector of 3)
(project-4d proj point4d)     ; → #(x y z)

; Rotate in the xw-plane by angle (radians)
(rotate-4d-xw point4d angle)  ; → #(x y z w)
```

The projection uses perspective division on the w-axis: `scale = fov4d / (fov4d − w)`.

## Actor-based parallel simulation

All module callbacks are safe to call from non-Qt threads via `send!`. Scheme procedure values captured in callbacks are rooted against Boehm GC by an internal list. See `examples/solar-system-qt6.scm`:

```scheme
(set! anim-timer
  (make-timer 16
    (lambda ()
      (send! sim-actor 'step)
      (canvas-redraw! canvas))))
(timer-start! anim-timer)
```

## Examples

| File | Description |
|------|-------------|
| `examples/solar-system-qt6.scm` | N-body simulation with 15 concurrent actors |
| `examples/mandelbrot_qt6.scm` | Mandelbrot set explorer |
| `examples/tesseract.scm` | Rotating 4D hypercube |
