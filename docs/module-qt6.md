# Module: `(curry qt6)`

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

## Quickstart

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

## Window management

```scheme
(make-window title width height)   ; → window
(window-show!     win)
(window-hide!     win)
(window-set-title! win title)

(window-on-close!   win proc)      ; proc called with no args when window closes
(window-on-key!     win proc)      ; proc called as (proc key-string modifiers)
(window-on-realize! win proc)      ; proc called once after the window is shown

(window-canvas   win)              ; → canvas (the central drawing area)
(window-sidebar  win)              ; → vbox (the right-hand sidebar container)
(window-menu-bar win)              ; → menubar
(window-status-bar win)            ; → statusbar
(window-add-toolbar! win toolbar)

(run-event-loop)                   ; start Qt event loop (blocks)
(quit-event-loop)                  ; exit the event loop
(qt-process-events)                ; process pending events without blocking
```

Key strings match Qt key names: `"a"`, `"space"`, `"Return"`, `"Escape"`, `"Left"`, `"Right"`, `"Up"`, `"Down"`, `"F1"` etc.

## Canvas and drawing

```scheme
(canvas-on-draw!  canvas proc)     ; proc: (lambda (painter width height) ...)
(canvas-on-mouse! canvas proc)     ; proc: (lambda (event x y) ...)
                                   ; event is 'press, 'release, or 'move
(canvas-redraw!   canvas)          ; schedule a repaint

(qt-painter-width  painter)        ; → number
(qt-painter-height painter)        ; → number
(qt-widget-width   canvas)         ; → number
(qt-widget-height  canvas)
(qt-gpu?           canvas)         ; → #t if OpenGL initialised
```

## 2D graphics — painter API

All `gfx-*` procedures take `painter` as the first argument. Color components are floats 0.0–1.0.

### Color and pen

```scheme
(gfx-set-color!     painter r g b a)        ; fill and stroke color
(gfx-set-pen-color! painter r g b a)        ; stroke only
(gfx-set-pen-width! painter w)
(gfx-set-antialias! painter bool)           ; default: #t
(gfx-set-blend!     painter mode)           ; 'normal | 'add | 'multiply
(gfx-set-font!      painter family size)
(gfx-set-font!      painter family size bold?)
```

### State stack

```scheme
(gfx-save!    painter)
(gfx-restore! painter)
```

### Transforms

```scheme
(gfx-translate! painter dx dy)
(gfx-rotate!    painter angle-degrees)
(gfx-scale!     painter sx sy)
```

### Shapes

```scheme
(gfx-clear!        painter r g b)                  ; fill background
(gfx-fill-rect!    painter x y w h)
(gfx-draw-rect!    painter x y w h)                ; stroke only
(gfx-fill-circle!  painter cx cy r)
(gfx-draw-circle!  painter cx cy r)
(gfx-fill-ellipse! painter cx cy rx ry)
(gfx-draw-ellipse! painter cx cy rx ry)
(gfx-draw-line!    painter x1 y1 x2 y2)
(gfx-draw-arc!     painter cx cy r start-deg span-deg)
(gfx-fill-pie!     painter cx cy r start-deg span-deg)

; Polygons — coords is a flat vector #(x0 y0 x1 y1 ...)
(gfx-fill-polygon! painter coords)
(gfx-draw-polygon! painter coords)

(gfx-draw-text! painter x y string)
```

### Batch drawing (efficient for many primitives)

```scheme
; Draw N points from parallel x-vector and y-vector, all the same color and size
(gfx-draw-points! painter x-vec y-vec r g b a size)

; Draw N line segments from flat coord-vector #(x0 y0 x1 y1 x2 y2 x3 y3 ...)
; Each group of 4 floats is one segment (x0,y0)→(x1,y1)
(gfx-draw-lines! painter coords r g b a width)

; Draw N triangles from flat coord-vector #(x0 y0 x1 y1 x2 y2 ...)
; Each group of 6 floats is one filled triangle
(gfx-fill-triangles! painter coords r g b a)

; Blit an RGBA pixel buffer (bytevector, row-major, 4 bytes/pixel)
(gfx-draw-image! painter dst-x dst-y dst-w dst-h bvec img-w img-h)
```

## Widgets

All widgets are values that can be placed in layout containers.

### Layout containers

```scheme
(make-vbox)                        ; vertical box
(make-hbox)                        ; horizontal box
(make-group-box title)             ; titled group with vbox inside
(make-tabs)                        ; tabbed container

(box-add!    container widget)     ; add widget to a sidebar/vbox/hbox
(layout-add! container widget)     ; alias for layouts created with make-vbox/hbox
(tabs-add!   tabs label widget)    ; add widget as a tab
```

### Sidebar

The sidebar (returned by `window-sidebar`) is a vbox that fills the right panel. Add labels, sliders, buttons, etc. to it.

```scheme
(box-add! (window-sidebar win) some-widget)
```

### Label and separator

```scheme
(make-label   text)
(label-set-text! label text)
(make-separator)
```

### Button

```scheme
(make-button label proc)           ; proc called with no args on click
```

### Toggle (checkbox)

```scheme
(make-toggle label initial-bool proc)   ; proc: (lambda (on?) ...)
```

### Slider

```scheme
(make-slider label lo hi step initial)        ; all numbers
(make-slider label lo hi step initial on-change)  ; on-change: (lambda (v) ...)
(slider-value      slider)         ; → current value
(slider-set-value! slider v)
```

### Dropdown

```scheme
(make-dropdown items initial-index proc)   ; items: list of strings
                                           ; proc: (lambda (index) ...)
(dropdown-index    dd)
(dropdown-set-index! dd i)
(dropdown-selected dd)             ; → selected string
```

### Radio group

```scheme
(make-radio-group items initial-index proc)  ; items: list of strings
                                             ; proc: (lambda (index) ...)
(radio-index group)                ; → selected index
```

### Spin box

```scheme
(make-spin-box lo hi step initial proc)    ; proc: (lambda (v) ...)
(spin-value      sb)
(spin-set-value! sb v)
```

### Text input

```scheme
(make-text-input placeholder proc)         ; proc: (lambda (text) ...)
(text-value      input)
(text-set-value! input text)
```

### Progress bar

```scheme
(make-progress-bar lo hi initial)
(progress-set! pb v)
```

### Universal widget properties

```scheme
(widget-set-style!   widget css-string)
(widget-set-enabled! widget bool)
(widget-set-visible! widget bool)
(widget-set-tooltip! widget text)
(widget-set-min-size! widget w h)
(widget-set-max-size! widget w h)
```

## Menus and toolbar

```scheme
(define mb (window-menu-bar win))
(define file-menu (menu-add-menu! mb "File"))
(menu-add-action!    file-menu "Open" proc)    ; proc called with no args
(menu-add-separator! file-menu)
(menu-add-action!    file-menu "Quit" quit-event-loop)

(define tb (make-toolbar))
(window-add-toolbar! win tb)
(toolbar-add-action!    tb label proc)
(toolbar-add-separator! tb)
```

## Status bar

```scheme
(define sb (window-status-bar win))
(statusbar-set-text! sb "Ready")
```

## Timers

```scheme
(define t (make-timer interval-ms proc))   ; proc called with no args on each tick
(timer-start!        t)
(timer-stop!         t)
(timer-set-interval! t ms)
```

## 4D projection math

These utilities project 4D coordinates into 3D for visualization, independent of Qt (pure math, no GPU required).

```scheme
; Create a projector with a 4D field-of-view angle (radians, default π/3)
; and optional 3D fov (default π/3)
(define proj (make-4d-projector))
(define proj (make-4d-projector fov4d))
(define proj (make-4d-projector fov4d fov3d))

; Project a 4D point #(x y z w) to a 3D point #(x y z) using perspective division
(project-4d proj point4d)   ; → #(x y z)

; Rotate a 4D point in the xw-plane by angle (radians)
(rotate-4d-xw angle point4d)   ; → #(x y z w)
```

The projection uses the w-axis perspective formula: scale = fov4d / (fov4d + w). Combining multiple `rotate-4d-xw` calls (varying the angle per frame) creates smooth 4D rotation animations.

## Actor-based parallel simulation

The module is thread-safe: `send!` may be called from any thread (including the Qt main thread), and callback closures registered with `canvas-on-draw!`, `make-button`, etc. are protected from Boehm GC by an internal root list. See `examples/solar-system-qt6.scm` for a complete example using the actor model:

```scheme
;;; Timer fires in Qt thread → sends 'step to sim-actor (non-blocking)
(set! anim-timer
  (make-timer 16
    (lambda ()
      (send! sim-actor 'step)
      (canvas-redraw! canvas))))
(timer-start! anim-timer)
```

## Example — complete simulation script

`examples/solar-system-qt6.scm` — N-body gravitational simulation in D spatial dimensions using 15 concurrent threads: 1 Qt event loop, 1 simulation actor, 1 gatherer actor, and 12 worker actors for parallel force computation. Run with:

```bash
./build/curry examples/solar-system-qt6.scm
```
