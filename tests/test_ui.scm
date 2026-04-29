;;; test_ui.scm — interactive GTK4 UI test
;;;
;;; Run: ./build/curry tests/test_ui.scm
;;;
;;; Controls:
;;;   Size slider   — radius of the circle (10–200)
;;;   Steps slider  — number of concentric rings drawn inside (0–10)
;;;   Colour menu   — pick fill colour from a dropdown
;;;   Outline       — toggle whether an outline stroke is drawn
;;;   Randomise     — button: pick a random colour via the slider combo
;;;   q / Escape    — quit

(import (curry ui))
(import (scheme base))
(import (scheme inexact))
(import (scheme write))

;;; ---- Colour palette ---------------------------------------------------

(define *palette*
  '#((1.0  0.25 0.25)   ; Red
     (1.0  0.60 0.10)   ; Orange
     (0.95 0.90 0.10)   ; Yellow
     (0.20 0.80 0.25)   ; Green
     (0.15 0.55 1.0)    ; Blue
     (0.55 0.20 0.90)   ; Purple
     (1.0  0.40 0.75)   ; Pink
     (0.90 0.90 0.90))) ; White

(define *color-names*
  '("Red" "Orange" "Yellow" "Green" "Blue" "Purple" "Pink" "White"))

;;; ---- Mutable state ---------------------------------------------------

(define *radius*       80.0)
(define *rings*         3)
(define *color-idx*     4)   ; Blue to start
(define *outline*       #t)

;;; ---- Widget handles (set in realize) ---------------------------------

(define canvas       #f)
(define size-slider  #f)
(define rings-slider #f)
(define color-drop   #f)

;;; ---- Drawing ---------------------------------------------------------

(define (draw cr w h)
  ; Read current control values every frame
  (when size-slider
    (set! *radius* (slider-value size-slider)))
  (when rings-slider
    (set! *rings* (inexact->exact (round (slider-value rings-slider)))))

  (let* ((cx   (/ w 2.0))
         (cy   (/ h 2.0))
         (col  (vector-ref *palette* *color-idx*))
         (r    (car   col))
         (g    (cadr  col))
         (b    (caddr col)))

    ; Background
    (cr-set-source-rgb! cr 0.08 0.08 0.12)
    (cr-paint! cr)

    ; Main circle — full opacity
    (cr-set-source-rgba! cr r g b 1.0)
    (cr-arc! cr cx cy *radius* 0.0 6.28318)
    (cr-fill! cr)

    ; Concentric rings — stroked at equal spacing inside the main circle
    (when (> *rings* 0)
      (let ((spacing (/ *radius* (+ *rings* 1))))
        (let loop ((k 1))
          (when (<= k *rings*)
            (cr-set-source-rgba! cr
              (* r 0.3) (* g 0.3) (* b 0.3) 0.85)
            (cr-set-line-width! cr 1.5)
            (cr-arc! cr cx cy (* k spacing) 0.0 6.28318)
            (cr-stroke! cr)
            (loop (+ k 1))))))

    ; Optional outline
    (when *outline*
      (cr-set-source-rgba! cr 1.0 1.0 1.0 0.6)
      (cr-set-line-width!  cr 2.0)
      (cr-arc! cr cx cy *radius* 0.0 6.28318)
      (cr-stroke! cr))

    ; HUD
    (cr-set-source-rgba! cr 0.8 0.8 0.8 0.9)
    (cr-set-font-size!   cr 13)
    (cr-text! cr 10 20
      (string-append "r = "
        (number->string (inexact->exact (round *radius*)))))
    (cr-text! cr 10 38
      (string-append "rings = " (number->string *rings*)))
    (cr-text! cr 10 56
      (string-append "colour = "
        (list-ref *color-names* *color-idx*)))))

;;; ---- Window setup ----------------------------------------------------

(define win (make-window "Circle Demo" 800 560))

(window-on-close! win quit-event-loop)

(window-on-key! win
  (lambda (key mods)
    (cond
      ((equal? key "q")      (quit-event-loop))
      ((equal? key "Escape") (quit-event-loop))
      ((equal? key "o")      ; toggle outline via keyboard too
       (set! *outline* (not *outline*))
       (when canvas (canvas-redraw! canvas))))))

(window-on-realize! win
  (lambda ()
    (set! canvas (window-canvas win))

    (let ((sb (window-sidebar win)))

      ; ---- Size ----
      (box-add! sb (make-label "Circle size"))
      (set! size-slider (make-slider "radius" 10.0 200.0 1.0 80.0))
      (box-add! sb size-slider)

      (box-add! sb (make-separator))

      ; ---- Rings ----
      (box-add! sb (make-label "Inner rings"))
      (set! rings-slider (make-slider "rings" 0.0 10.0 1.0 3.0))
      (box-add! sb rings-slider)

      (box-add! sb (make-separator))

      ; ---- Colour dropdown ----
      (box-add! sb (make-label "Colour"))
      (set! color-drop
        (make-dropdown *color-names* *color-idx*
          (lambda (idx)
            (set! *color-idx* idx)
            (canvas-redraw! canvas))))
      (box-add! sb color-drop)

      (box-add! sb (make-separator))

      ; ---- Outline toggle ----
      (box-add! sb
        (make-toggle "Outline" *outline*
          (lambda (on)
            (set! *outline* on)
            (canvas-redraw! canvas))))

      (box-add! sb (make-separator))

      ; ---- Buttons ----
      (box-add! sb
        (make-button "Next colour"
          (lambda ()
            (set! *color-idx*
              (modulo (+ *color-idx* 1) (vector-length *palette*)))
            (canvas-redraw! canvas))))

      (box-add! sb
        (make-button "Quit" quit-event-loop)))

    ; Attach draw callback — redraws whenever the canvas needs painting.
    ; Moving the sliders queues redraws automatically via GTK.
    (canvas-on-draw! canvas draw)))

(window-show! win)
(run-event-loop)
