;;; examples/ncurses_demo.scm — a small interactive tour of (curry ncurses)
;;;
;;; Run directly in a real terminal (not through a pipe or a non-tty
;;; harness): ./build/curry examples/ncurses_demo.scm
;;; Press any key to advance through each screen; 'q' quits early.

(import (curry ncurses))

(define (wait-for-key win)
  (let ((k (ncurses-getch win)))
    (or (eq? k #\q) (eq? k 'q))))

(call-with-ncurses
  (lambda (win)
    (ncurses-add-string! win 0 0 "(curry ncurses) demo — press any key to continue, q to quit")
    (ncurses-add-string! win 2 0 "Plain text.")
    (with-attributes win a-bold
      (ncurses-add-string! win 3 0 "Bold text."))
    (with-attributes win a-underline
      (ncurses-add-string! win 4 0 "Underlined text."))
    (with-attributes win (bitwise-or a-bold a-reverse)
      (ncurses-add-string! win 5 0 "Bold + reverse."))
    (ncurses-refresh! win)
    (unless (wait-for-key win)

      ;; A bordered sub-window
      (ncurses-erase! win)
      (ncurses-add-string! win 0 0 "A bordered window, sized to the terminal:")
      (let* ((h (ncurses-window-height win)) (w (ncurses-window-width win))
             (box (ncurses-window-new (- h 4) (- w 4) 3 2)))
        (ncurses-box! box)
        (ncurses-add-string! box 1 2 "Inside the box.")
        (ncurses-refresh! win)
        (ncurses-refresh! box)
        (unless (wait-for-key win)

          ;; Colors, if the terminal supports them
          (ncurses-window-delete! box)
          (ncurses-erase! win)
          (if (ncurses-start-color!)
              (begin
                (ncurses-init-color-pair! 1 'red 'black)
                (ncurses-init-color-pair! 2 'green 'black)
                (ncurses-add-string! win 0 0 "Color pairs 1 (red) and 2 (green):")
                (ncurses-set-color! win 1)
                (ncurses-add-string! win 2 0 "This line is red-on-black.")
                (ncurses-set-color! win 2)
                (ncurses-add-string! win 3 0 "This line is green-on-black."))
              (ncurses-add-string! win 0 0 "This terminal doesn't support color."))
          (ncurses-add-string! win 5 0 "Arrow keys / Enter / Backspace translate to symbols — try one, then q:")
          (ncurses-refresh! win)
          (let loop ()
            (let ((k (ncurses-getch win)))
              (unless (or (eq? k #\q) (eq? k 'q))
                (ncurses-add-string! win 7 0 "got: ")
                (ncurses-add-string! win (string-append "                    "))
                (ncurses-move! win 7 5)
                (ncurses-add-string! win (if (symbol? k) (symbol->string k)
                                             (if (char? k) (string k)
                                                 (number->string k))))
                (ncurses-refresh! win)
                (loop)))))))))
