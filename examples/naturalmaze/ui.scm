;;; ui.scm — window, input bindings, timer
;;; Requires Qt6 already imported.

(define *win*    #f)
(define *canvas* #f)
(define *timer*  #f)

(define (setup-ui!)
  (set! *win*    (make-window "Natural Maze  [click to look]" 1280 720))
  (set! *canvas* (window-canvas *win*))

  ;; Input: key down
  (window-on-key! *win*
    (lambda (key mods)
      (cond
        ((or (equal? key "w") (equal? key "Up"))    (key-down! "w"))
        ((or (equal? key "s") (equal? key "Down"))  (key-down! "s"))
        ((or (equal? key "a") (equal? key "Left"))  (key-down! "a"))
        ((or (equal? key "d") (equal? key "Right")) (key-down! "d"))
        ((or (equal? key "q") (equal? key "Q")) (try-step-w! -1))
        ((or (equal? key "e") (equal? key "E")) (try-step-w!  1))
        ((equal? key "r")       (set! *pitch* 0.0))   ; reset pitch
        ((equal? key "Escape")
         (if (canvas-mouse-grabbed? *canvas*)
             (canvas-release-mouse! *canvas*)
             (quit-event-loop))))))

  ;; Input: key up
  (window-on-key-up! *win*
    (lambda (key mods)
      (cond
        ((or (equal? key "w") (equal? key "Up"))    (key-up! "w"))
        ((or (equal? key "s") (equal? key "Down"))  (key-up! "s"))
        ((or (equal? key "a") (equal? key "Left"))  (key-up! "a"))
        ((or (equal? key "d") (equal? key "Right")) (key-up! "d"))
)))

  ;; Mouse: click to grab
  (canvas-on-mouse! *canvas*
    (lambda (event btn x y mods)
      (when (eq? event 'press)
        (canvas-grab-mouse! *canvas*))))

  ;; Mouse: look around when grabbed
  (canvas-on-grab-move! *canvas*
    (lambda (dx dy)
      (set! *yaw*   (+ *yaw* (* dx MOUSE-SENS-H)))
      (set! *pitch* (max (- PITCH-MAX)
                         (min PITCH-MAX
                              (+ *pitch* (* (- dy) MOUSE-SENS-V)))))))

  ;; W step shortcuts via mouse wheel when grabbed (optional bonus)
  (canvas-on-scroll! *canvas*
    (lambda (sdx sdy x y mods)
      (when (> (abs sdy) 2)
        (try-step-w! (if (> sdy 0) 1 -1)))))

  ;; Main draw
  (canvas-on-draw! *canvas* draw-frame!)

  ;; Timer — passes elapsed ms for smooth movement
  (set! *timer*
    (make-timer/dt 16
      (lambda (dt-ms)
        (tick! dt-ms)
        (canvas-redraw! *canvas*))))

  ;; Window close
  (window-on-close! *win* (lambda () (quit-event-loop)))

  ;; Realize: compile shader and build maze texture
  (window-on-realize! *win*
    (lambda ()
      (init-gl!)
      (timer/dt-start! *timer*)))

  (window-show! *win*))
