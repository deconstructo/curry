;;; test_qt6.scm — interactive test harness for the (curry qt6) module
;;;
;;; Exercises all three API layers:
;;;   Layer 3: windows, menus, toolbar, status bar, all widget types
;;;   Layer 2: gfx-* 2D drawing — shapes, text, transforms, blending, batching
;;;   Layer 1: qt-* raw queries
;;;
;;; Run: ./build/curry tests/test_qt6.scm
;;;
;;; Controls:
;;;   Left sidebar — all widget types; changing them updates the canvas
;;;   Canvas       — live demo of the selected drawing mode
;;;   Menu / File  — Open (no-op), Save (no-op), Quit
;;;   Menu / Demo  — switch drawing demo via menu actions
;;;   Toolbar      — Pause, Reset, Quit buttons
;;;   Status bar   — shows current mouse position and last key pressed
;;;   q / Escape   — quit

(import (curry qt6))
(import (scheme base))
(import (scheme inexact))
(import (scheme write))

;;; ---- State ----

(define *demo*       0)     ; which drawing demo is active (0-4)
(define *angle*      0.0)   ; rotation angle for animation
(define *color-idx*  0)     ; selected colour index
(define *size*       60.0)  ; shape size from slider
(define *pen-width*  2.0)   ; stroke width
(define *antialias*  #t)
(define *paused*     #f)
(define *blend*      "normal")
(define *mouse-x*    0)
(define *mouse-y*    0)
(define *last-key*   "(none)")
(define *spin-val*   1.0)
(define *text-input* "Hello, Qt6!")

;;; ---- Colour palette ----

(define *palette*
  (vector
    (vector 0.2  0.5  1.0)   ; blue
    (vector 1.0  0.3  0.2)   ; red
    (vector 0.2  0.8  0.3)   ; green
    (vector 1.0  0.8  0.1)   ; yellow
    (vector 0.8  0.3  0.9)   ; purple
    (vector 0.1  0.9  0.9)   ; cyan
    (vector 1.0  0.5  0.0)   ; orange
    (vector 1.0  1.0  1.0))) ; white

(define *color-names*
  '("Blue" "Red" "Green" "Yellow" "Purple" "Cyan" "Orange" "White"))

(define *demo-names*
  '("Shapes" "Transforms" "Blending" "Batch points" "Triangles"))

(define *blend-modes*
  '("normal" "add" "multiply" "screen" "overlay"))

;;; ---- Widget handles ----

(define canvas       #f)
(define status-bar   #f)
(define size-slider  #f)
(define pen-slider   #f)
(define color-drop   #f)
(define blend-drop   #f)
(define demo-radio   #f)
(define antialias-cb #f)
(define spin-box     #f)
(define text-entry   #f)
(define progress-bar #f)
(define anim-timer   #f)
(define frame-count  0)

;;; ---- Helpers ----

(define (current-color)
  (vector-ref *palette* *color-idx*))

(define (cr r g b) (vector-ref (vector r g b) 0))  ; placeholder

(define (set-main-color! painter a)
  (let ((col (current-color)))
    (gfx-set-color! painter
      (vector-ref col 0) (vector-ref col 1) (vector-ref col 2) a)))

(define (update-status!)
  (when status-bar
    (statusbar-set-text! status-bar
      (string-append
        "Mouse: (" (number->string *mouse-x*) ", " (number->string *mouse-y*) ")  "
        "Key: " *last-key* "  "
        "Demo: " (list-ref *demo-names* *demo*) "  "
        "Frame: " (number->string frame-count)))))

;;; ---- Drawing demos ----

;;; Demo 0: Basic shapes
(define (draw-shapes painter w h)
  (let* ((cx  (/ w 2.0))
         (cy  (/ h 2.0))
         (s   *size*)
         (col (current-color))
         (r   (vector-ref col 0))
         (g   (vector-ref col 1))
         (b   (vector-ref col 2)))

    ; Filled circle (top-left quadrant)
    (gfx-set-color! painter r g b 0.9)
    (gfx-fill-circle! painter (- cx (/ s 1.2)) (- cy (/ s 1.2)) (/ s 1.5))

    ; Stroked circle (top-right)
    (gfx-set-color! painter r g b 1.0)
    (gfx-set-pen-width! painter *pen-width*)
    (gfx-draw-circle! painter (+ cx (/ s 1.2)) (- cy (/ s 1.2)) (/ s 1.5))

    ; Filled rectangle (bottom-left)
    (gfx-set-color! painter r g b 0.7)
    (gfx-fill-rect! painter (- cx (* s 1.6)) (+ cy (* s 0.3))
                             (* s 1.2) (* s 0.9))

    ; Stroked rectangle (bottom-right)
    (gfx-set-color! painter r g b 1.0)
    (gfx-set-pen-width! painter *pen-width*)
    (gfx-draw-rect! painter (+ cx (* s 0.4)) (+ cy (* s 0.3))
                             (* s 1.2) (* s 0.9))

    ; Ellipse (centre)
    (gfx-set-color! painter r g b 0.5)
    (gfx-fill-ellipse! painter cx cy (* s 0.6) (* s 0.3))

    ; Diagonal line
    (gfx-set-color! painter 0.8 0.8 0.8 0.7)
    (gfx-set-pen-width! painter 1.5)
    (gfx-draw-line! painter 20 20 (- w 20) (- h 20))

    ; Polygon (pentagon)
    (let ((pts (let loop ((i 0) (acc '()))
                  (if (= i 5) (reverse acc)
                      (let* ((a  (* i (/ (* 2.0 3.14159265) 5.0)))
                             (px (+ cx (* (/ s 2.0) (cos a))))
                             (py (+ cy (* (/ s 2.0) (sin a)))))
                        (loop (+ i 1) (cons (cons px py) acc)))))))
      (gfx-set-color! painter b r g 0.6)
      (gfx-fill-polygon! painter pts)
      (gfx-set-color! painter 1.0 1.0 1.0 0.8)
      (gfx-set-pen-width! painter *pen-width*)
      (gfx-draw-polygon! painter pts))

    ; Text
    (gfx-set-color! painter 0.9 0.9 0.9 0.9)
    (gfx-set-font! painter "Sans" 14)
    (gfx-draw-text! painter 10 (- h 40) *text-input*)
    (gfx-set-font! painter "Monospace" 11)
    (gfx-draw-text! painter 10 (- h 20)
      (string-append "spin=" (number->string *spin-val*) "  pen=" (number->string *pen-width*)))))

;;; Demo 1: Transforms (animated rotating shapes)
(define (draw-transforms painter w h)
  (let* ((cx (/ w 2.0))
         (cy (/ h 2.0))
         (col (current-color))
         (r (vector-ref col 0)) (g (vector-ref col 1)) (b (vector-ref col 2)))

    ; Draw 8 rotated rectangles
    (let loop ((i 0))
      (when (< i 8)
        (let ((angle (+ *angle* (* i (/ (* 2.0 3.14159265) 8.0))))
              (alpha (/ (- 8 i) 8.0)))
          (gfx-save! painter)
          (gfx-translate! painter cx cy)
          (gfx-rotate! painter angle)
          (gfx-set-color! painter r g b (* alpha 0.85))
          (gfx-fill-rect! painter (- *size* 5) (- 5) (* 2 (+ *size* 10)) 10)
          (gfx-restore! painter))
        (loop (+ i 1))))

    ; Orbiting circle
    (let ((ox (+ cx (* *size* 1.8 (cos *angle*))))
          (oy (+ cy (* *size* 1.8 (sin *angle*)))))
      (gfx-set-color! painter b r g 0.9)
      (gfx-fill-circle! painter ox oy (/ *size* 4.0)))

    ; Scale demo — pulsing circle at bottom-left
    (let ((scale (+ 0.5 (* 0.5 (sin (* *angle* 2.0))))))
      (gfx-save! painter)
      (gfx-translate! painter 120 (- h 80))
      (gfx-scale! painter scale scale)
      (gfx-set-color! painter g b r 0.7)
      (gfx-fill-circle! painter 0.0 0.0 40.0)
      (gfx-restore! painter))

    ; HUD
    (gfx-set-color! painter 0.7 0.7 0.7 0.8)
    (gfx-set-font! painter "Monospace" 11)
    (gfx-draw-text! painter 10 16
      (string-append "angle = " (number->string (inexact->exact (round (* *angle* 57.296))))
                     "°"))))

;;; Demo 2: Blend modes
(define (draw-blending painter w h)
  (let* ((cx  (/ w 2.0))
         (cy  (/ h 2.0))
         (r   (/ *size* 1.5))
         (col (current-color))
         (cr  (vector-ref col 0)) (cg (vector-ref col 1)) (cb (vector-ref col 2)))

    ; White base rectangle to show blending against
    (gfx-set-blend! painter 'normal)
    (gfx-set-color! painter 0.9 0.9 0.9 1.0)
    (gfx-fill-rect! painter (/ cx 2.0) (/ cy 2.0) cx cy)

    ; Three overlapping circles with current blend mode
    (gfx-set-blend! painter (string->symbol *blend*))
    (gfx-set-color! painter 1.0 0.2 0.2 0.8)
    (gfx-fill-circle! painter (- cx (* r 0.6)) cy r)
    (gfx-set-color! painter 0.2 1.0 0.2 0.8)
    (gfx-fill-circle! painter (+ cx (* r 0.6)) cy r)
    (gfx-set-color! painter cr cg cb 0.8)
    (gfx-fill-circle! painter cx (- cy (* r 0.6)) r)
    (gfx-set-blend! painter 'normal)

    ; Label
    (gfx-set-color! painter 0.2 0.2 0.2 1.0)
    (gfx-set-font! painter "Sans" 13 #t)
    (gfx-draw-text! painter (/ cx 2.0) (- h 20)
      (string-append "Blend mode: " *blend*))))

;;; Demo 3: Batch point drawing
(define (draw-batch-points painter w h)
  (let* ((n   500)
         (col (current-color))
         (r   (vector-ref col 0)) (g (vector-ref col 1)) (b (vector-ref col 2))
         (xv  (make-vector n 0.0))
         (yv  (make-vector n 0.0)))

    ; Lissajous figure
    (let loop ((i 0))
      (when (< i n)
        (let* ((t    (+ *angle* (* i (/ (* 2.0 3.14159265) n))))
               (x    (+ (/ w 2.0) (* (* w 0.4) (sin (* t 3.0)))))
               (y    (+ (/ h 2.0) (* (* h 0.4) (cos (* t 2.0))))))
          (vector-set! xv i x)
          (vector-set! yv i y))
        (loop (+ i 1))))

    ; Draw all 500 points in one call
    (gfx-draw-points! painter xv yv r g b 0.8 3.0)

    ; Overlay a second figure with additive blending
    (let loop ((i 0))
      (when (< i n)
        (let* ((t  (+ (* -0.7 *angle*) (* i (/ (* 2.0 3.14159265) n))))
               (x  (+ (/ w 2.0) (* (* w 0.35) (sin (* t 5.0)))))
               (y  (+ (/ h 2.0) (* (* h 0.35) (cos (* t 3.0))))))
          (vector-set! xv i x)
          (vector-set! yv i y))
        (loop (+ i 1))))
    (gfx-draw-points! painter xv yv b r g 0.5 2.0)

    (gfx-set-color! painter 0.6 0.6 0.6 0.7)
    (gfx-set-font! painter "Monospace" 11)
    (gfx-draw-text! painter 10 16 (string-append "1000 points, batch rendered  n=" (number->string n)))))

;;; Demo 4: Filled triangles
(define (draw-triangles painter w h)
  (let* ((n    60)     ; number of triangles
         (cv   (make-vector (* n 6) 0.0))
         (col  (current-color))
         (r    (vector-ref col 0)) (g (vector-ref col 1)) (b (vector-ref col 2))
         (cx   (/ w 2.0)) (cy (/ h 2.0)))

    ; Fill coordinate vector with rotating fan triangles
    (let loop ((i 0))
      (when (< i n)
        (let* ((a1  (+ *angle* (* i (/ (* 2.0 3.14159265) n))))
               (a2  (+ a1 (/ (* 2.0 3.14159265) n)))
               (rad (* *size* 1.5 (+ 0.3 (* 0.7 (/ i n))))))
          (vector-set! cv (* i 6)       cx)
          (vector-set! cv (+ (* i 6) 1) cy)
          (vector-set! cv (+ (* i 6) 2) (+ cx (* rad (cos a1))))
          (vector-set! cv (+ (* i 6) 3) (+ cy (* rad (sin a1))))
          (vector-set! cv (+ (* i 6) 4) (+ cx (* rad (cos a2))))
          (vector-set! cv (+ (* i 6) 5) (+ cy (* rad (sin a2)))))
        (loop (+ i 1))))

    ; gfx-fill-triangles! draws the lot in one call
    (gfx-fill-triangles! painter cv r g b 0.7)

    ; Outline rings
    (gfx-set-color! painter 1.0 1.0 1.0 0.2)
    (gfx-set-pen-width! painter 0.5)
    (gfx-draw-circle! painter cx cy (* *size* 1.5))
    (gfx-draw-circle! painter cx cy (* *size* 0.45))

    (gfx-set-color! painter 0.7 0.7 0.7 0.7)
    (gfx-set-font! painter "Monospace" 11)
    (gfx-draw-text! painter 10 16
      (string-append (number->string n) " triangles in one gfx-fill-triangles! call"))))

;;; ---- Main draw callback ----

(define (draw painter w h)
  (gfx-clear! painter 0.08 0.08 0.12)
  (gfx-set-antialias! painter *antialias*)

  (cond
    ((= *demo* 0) (draw-shapes     painter w h))
    ((= *demo* 1) (draw-transforms painter w h))
    ((= *demo* 2) (draw-blending   painter w h))
    ((= *demo* 3) (draw-batch-points painter w h))
    ((= *demo* 4) (draw-triangles  painter w h)))

  ; Layer 1 demo: show GPU status and device dimensions
  (gfx-set-color! painter 0.4 0.4 0.4 0.7)
  (gfx-set-font! painter "Monospace" 10)
  (gfx-draw-text! painter 10 (- h 6)
    (string-append "qt-painter: " (number->string (qt-painter-width painter))
                   "×" (number->string (qt-painter-height painter)))))

;;; ---- Window setup ----

(define win (make-window "Qt6 Module Test Harness" 1100 720))

;;; ---- Menus ----

(define mb (window-menu-bar win))

(define file-menu (menubar-add-menu! mb "File"))
(menu-add-action! file-menu "New"  (lambda () (statusbar-set-text! status-bar "New (no-op)")) "Ctrl+N")
(menu-add-action! file-menu "Open" (lambda () (statusbar-set-text! status-bar "Open (no-op)")) "Ctrl+O")
(menu-add-action! file-menu "Save" (lambda () (statusbar-set-text! status-bar "Save (no-op)")) "Ctrl+S")
(menu-add-separator! file-menu)
(menu-add-action! file-menu "Quit" quit-event-loop "Ctrl+Q")

(define demo-menu (menubar-add-menu! mb "Demo"))
(for-each
  (lambda (name idx)
    (menu-add-action! demo-menu name
      (lambda ()
        (set! *demo* idx)
        (when status-bar
          (statusbar-set-text! status-bar (string-append "Demo: " name))))))
  *demo-names* '(0 1 2 3 4))

(define help-menu (menubar-add-menu! mb "Help"))
(menu-add-action! help-menu "About"
  (lambda ()
    (when status-bar
      (statusbar-set-text! status-bar
        "Qt6 test harness — tests all three API layers"))))

;;; ---- Toolbar ----

(define tb (window-add-toolbar! win 'top))
(toolbar-add-action! tb "Pause"
  (lambda ()
    (set! *paused* (not *paused*))
    (if *paused* (timer-stop! anim-timer) (timer-start! anim-timer))))
(toolbar-add-action! tb "Reset"
  (lambda ()
    (set! *angle* 0.0)
    (set! frame-count 0)))
(toolbar-add-separator! tb)
(toolbar-add-action! tb "Quit" quit-event-loop)

;;; ---- Window events ----

(window-on-close! win quit-event-loop)

(window-on-key! win
  (lambda (key mods)
    (set! *last-key* (string-append key
                       (if (null? mods) ""
                           (string-append "+" (symbol->string (car mods))))))
    (cond
      ((equal? key "q")      (quit-event-loop))
      ((equal? key "Escape") (quit-event-loop))
      ((equal? key "space")
       (set! *paused* (not *paused*))
       (if *paused* (timer-stop! anim-timer) (timer-start! anim-timer)))
      ((equal? key "Right")
       (set! *demo* (modulo (+ *demo* 1) 5)))
      ((equal? key "Left")
       (set! *demo* (modulo (- *demo* 1) 5))))
    (update-status!)))

;;; ---- Sidebar and canvas setup (runs on realize) ----

(window-on-realize! win
  (lambda ()
    (set! canvas     (window-canvas win))
    (set! status-bar (window-status-bar win))
    (let ((sb (window-sidebar win)))

      ; ---- Demo selector (radio group) ----
      (box-add! sb (make-label "Drawing Demo"))
      (set! demo-radio
        (make-radio-group *demo-names* *demo*
          (lambda (idx)
            (set! *demo* idx)
            (statusbar-set-text! status-bar
              (string-append "Demo: " (list-ref *demo-names* idx))))))
      (box-add! sb demo-radio)

      (box-add! sb (make-separator))

      ; ---- Colour dropdown ----
      (box-add! sb (make-label "Colour"))
      (set! color-drop
        (make-dropdown *color-names* *color-idx*
          (lambda (idx)
            (set! *color-idx* idx))))
      (box-add! sb color-drop)

      (box-add! sb (make-separator))

      ; ---- Size slider ----
      (box-add! sb (make-label "Size"))
      (set! size-slider
        (make-slider "size" 10.0 150.0 1.0 60.0
          (lambda (v) (set! *size* v))))
      (box-add! sb size-slider)

      ; ---- Pen width slider ----
      (box-add! sb (make-label "Pen width"))
      (set! pen-slider
        (make-slider "pen" 0.5 8.0 0.5 2.0
          (lambda (v) (set! *pen-width* v))))
      (box-add! sb pen-slider)

      (box-add! sb (make-separator))

      ; ---- Blend mode dropdown ----
      (box-add! sb (make-label "Blend mode (Demo 2)"))
      (set! blend-drop
        (make-dropdown *blend-modes* 0
          (lambda (idx)
            (set! *blend* (list-ref *blend-modes* idx)))))
      (box-add! sb blend-drop)

      (box-add! sb (make-separator))

      ; ---- Antialias toggle ----
      (box-add! sb
        (make-toggle "Antialiasing" #t
          (lambda (on) (set! *antialias* on))))

      (box-add! sb (make-separator))

      ; ---- Spin box ----
      (box-add! sb (make-label "Spin value"))
      (set! spin-box
        (make-spin-box 0.0 10.0 0.1 1.0
          (lambda (v) (set! *spin-val* v))))
      (box-add! sb spin-box)

      ; ---- Text input ----
      (box-add! sb (make-label "Display text"))
      (set! text-entry
        (make-text-input "Type something..."
          (lambda (s) (set! *text-input* s))))
      (text-set-value! text-entry "Hello, Qt6!")
      (box-add! sb text-entry)

      (box-add! sb (make-separator))

      ; ---- Progress bar ----
      (box-add! sb (make-label "Frame progress"))
      (set! progress-bar (make-progress-bar 0 360 0))
      (box-add! sb progress-bar)

      (box-add! sb (make-separator))

      ; ---- Buttons ----
      (box-add! sb
        (make-button "Pause / Resume"
          (lambda ()
            (set! *paused* (not *paused*))
            (if *paused* (timer-stop! anim-timer) (timer-start! anim-timer)))))

      (box-add! sb
        (make-button "Next Demo"
          (lambda ()
            (set! *demo* (modulo (+ *demo* 1) 5)))))

      (box-add! sb
        (make-button "Reset Angle"
          (lambda ()
            (set! *angle* 0.0)
            (set! frame-count 0))))

      (box-add! sb
        (make-button "GPU status"
          (lambda ()
            (statusbar-set-text! status-bar
              (string-append "GPU: "
                (if (qt-gpu? canvas) "OpenGL OK" "CPU (software)"))))))

      (box-add! sb (make-separator))

      (box-add! sb
        (make-button "Quit" quit-event-loop))

      ; ---- Style demo ----
      (let ((styled-lbl (make-label "Styled widget")))
        (widget-set-style! styled-lbl
          "color: #88ccff; font-weight: bold; padding: 4px; border-radius: 4px; background: #223344;")
        (widget-set-tooltip! styled-lbl "This label has custom Qt stylesheet applied")
        (box-add! sb styled-lbl)))

    ; ---- Mouse callback ----
    (canvas-on-mouse! canvas
      (lambda (event button x y mods)
        (set! *mouse-x* x)
        (set! *mouse-y* y)
        (update-status!)))

    ; ---- Draw callback ----
    (canvas-on-draw! canvas draw)

    ; ---- Animation timer at ~60fps ----
    (set! anim-timer
      (make-timer 16
        (lambda ()
          (unless *paused*
            (set! *angle*    (+ *angle* 0.025))
            (set! frame-count (+ frame-count 1))
            (when size-slider
              (set! *size* (slider-value size-slider)))
            (when pen-slider
              (set! *pen-width* (slider-value pen-slider)))
            ; Update progress bar (modulo 360 frames)
            (when progress-bar
              (progress-set! progress-bar (modulo frame-count 361)))
            (canvas-redraw! canvas)
            (update-status!)))))
    (timer-start! anim-timer)

    ; Initial status
    (statusbar-set-text! status-bar
      "Qt6 module test harness — use controls to explore all API layers")))

;;; ---- Launch ----

(window-show! win)
(run-event-loop)
