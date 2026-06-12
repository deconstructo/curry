# Module: `(curry qt6)`

*v1.4.1 — 2026-06-12*

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

(canvas-save-png!  canvas path)  ; → #t on success, #f on failure
```

`canvas-save-png!` renders the canvas into an off-screen FBO at full device
resolution (HiDPI-correct — a Retina display at 2× DPR produces a 2× physical
pixel PNG) and saves it to `path` as PNG.  It calls the canvas's registered
`draw-proc` exactly once off-screen, so the saved image matches what is shown
on screen including any QPainter HUD overlays drawn after `gl-shader-draw!`.

```scheme
(canvas-on-draw! canvas draw-frame)

; Export button handler
(make-button "Save PNG…"
  (lambda ()
    (let ((path (file-save-dialog "Save PNG" "Images (*.png)")))
      (when path
        (canvas-save-png! canvas path)))))
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

`(x, y)` is the **left baseline** of the first character (QPainter convention).
To centre or right-align text, measure it first with the metrics functions below.

### Text metrics

All four procedures query the painter's **current font** (set by `gfx-set-font!`).
Coordinates are in the same logical pixel space as drawing operations.

```scheme
(gfx-text-width   painter string)  ; → flonum: advance width of string
(gfx-font-ascent  painter)         ; → flonum: distance from baseline to top of cap-height
(gfx-font-descent painter)         ; → flonum: distance from baseline down to descenders
(gfx-font-height  painter)         ; → flonum: full line height (ascent + descent + leading)
```

Common usage patterns:

```scheme
; Horizontally centred text
(let ((tw (gfx-text-width painter label)))
  (gfx-draw-text! painter (- (/ w 2) (/ tw 2)) y label))

; Right-aligned text with 8px margin
(let ((tw (gfx-text-width painter label)))
  (gfx-draw-text! painter (- w tw 8) y label))

; Vertically centred text within a box of height bh
(let ((mid-y (+ box-y (/ bh 2) (/ (gfx-font-ascent painter) 2))))
  (gfx-draw-text! painter x mid-y label))
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
(make-scroll-area)                 ; vertically scrollable vbox panel

(box-add!    sidebar-or-vbox widget)
(layout-add! vbox-or-hbox-or-scroll-area widget)
(tabs-add!   tabs widget label)
```

`make-scroll-area` creates a `QScrollArea` wrapping an inner vertical box.
`layout-add!` works on it identically to `make-vbox` — the scroll area is
transparent at the API level.  Use it for sidebars or panels that may grow
beyond the window height:

```scheme
(define panel (make-scroll-area))
(layout-add! panel widget-a)
(layout-add! panel widget-b)
; ... add many widgets; a scrollbar appears automatically if they overflow
(window-set-central-widget! win panel)
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
(dropdown-index     dd)                    ; → selected index (integer)
(dropdown-set-index! dd i)                 ; programmatically select item i
(dropdown-selected  dd)                    ; alias for dropdown-index
(dropdown-count     dd)                    ; → number of items
(dropdown-add-item! dd label)              ; append a new item without rebuilding
(dropdown-clear!    dd)                    ; remove all items
```

`dropdown-add-item!` appends to an existing dropdown in O(1).  Use it to grow
a bookmark list or history as the user saves entries:

```scheme
(define dd (make-dropdown '("Default") 0 navigate-to!))

; Later, when the user saves a new location:
(dropdown-add-item! dd "My favourite zoom")
(dropdown-set-index! dd (- (dropdown-count dd) 1))
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

## GLSL shaders and textures

Write full OpenGL 3.3 Core Profile fragment shaders directly in Scheme strings. The module handles context setup, HiDPI scaling, and QPainter interleaving automatically.

```scheme
; Compile a program (lazy — actual GL compilation on first draw)
(define prog (make-gl-shader frag-src))          ; default fullscreen-quad vert shader
(define prog (make-gl-shader vert-src frag-src)) ; custom vertex shader

; Draw: runs inside canvas-on-draw! callback
(gl-shader-draw! prog painter)                   ; no extra uniforms
(gl-shader-draw! prog painter uniforms-alist)    ; uniforms as ((name . value) ...)
```

`gl-shader-draw!` auto-sets two uniforms before processing the alist:
- `u_resolution` — physical pixel dimensions `(vec2 w h)` (HiDPI-correct)
- `u_dpr` — device pixel ratio (float)

Uniform value dispatch by Scheme type:

| Scheme type | GLSL type |
|------------|-----------|
| fixnum | `int` |
| `#t`/`#f` | `int` (1/0) |
| 2-element list | `vec2` |
| 3-element list | `vec3` |
| 4-element list | `vec4` |
| `gl-texture` handle | `sampler2D` (bound to next available texture unit) |
| flonum / other numeric | `float` |

The default vertex shader generates a fullscreen quad from `gl_VertexID` — no VBO or vertex attributes needed. After `gl-shader-draw!` returns, `endNativePainting()` has been called and QPainter is fully restored, so `gfx-draw-text!` etc. work normally for HUD overlays.

### GL textures

Upload pixel data to the GPU as a 2D texture. Pass the texture handle as a uniform value and `gl-shader-draw!` binds it to a `sampler2D` automatically.

```scheme
; Create a texture from a bytevector
(make-gl-texture bv w h)          ; R8 (single channel, default)
(make-gl-texture bv w h 'rgba)    ; RGBA8 (four channels)

; Replace the pixel data and schedule a re-upload on the next draw
(gl-texture-update! tex bv)
```

The texture is uploaded lazily on the first `gl-shader-draw!` call after creation (or after `gl-texture-update!`). Filter is `GL_NEAREST`; wrap mode is `GL_CLAMP_TO_EDGE`.

In the uniforms alist, pass the handle directly as the value:
```scheme
(gl-shader-draw! prog painter
  (list (cons "u_my_tex" my-texture)   ; bound to texture unit 0
        (cons "u_other"  other-tex)))  ; bound to texture unit 1
```

Multiple textures in one draw call are each assigned the next available texture unit (starting from 0).

**Example** (from `examples/mandelbrot.scm`):

```scheme
(define prog (make-gl-shader "#version 330 core
out vec4 frag_color;
uniform vec2 u_resolution;
uniform vec2 u_center;
uniform float u_zoom;
uniform float u_dpr;
void main() {
    float pzoom = u_zoom * u_dpr;
    vec2 c = u_center + vec2(gl_FragCoord.x - u_resolution.x*0.5,
                            -(gl_FragCoord.y - u_resolution.y*0.5)) / pzoom;
    frag_color = vec4(length(c) < 2.0 ? vec3(0.0) : vec3(1.0), 1.0);
}"))

(canvas-on-draw! canvas
  (lambda (painter w h)
    (gl-shader-draw! prog painter
      (list (cons "u_center" (list 0.0 0.0))
            (cons "u_zoom"   200.0)))))
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
| `examples/mandelbrot.scm` | Hypercomplex Mandelbrot GPU explorer (complex/quaternion/octonion) |
| `examples/tesseract.scm` | Rotating 4D hypercube |
| `examples/maze4d.scm` | First-person 4D maze — GPU DDA raycaster with anaglyph stereo |
