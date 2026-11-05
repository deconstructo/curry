;;; SRFI-166 (srfi 166 color): ANSI SGR formatters. Each wraps its
;;; formatters' output in a start and an end sequence. The sequences are
;;; written through `output`, so they count as characters in the default
;;; string-width; use terminal-aware (srfi 166 unicode) when padding or
;;; trimming colored text.
;;; https://srfi.schemers.org/srfi-166/

(define-library (srfi s166 color-impl)
  (import (scheme base) (srfi s166 base-impl))
  (export as-red as-blue as-green as-cyan as-yellow as-magenta as-white as-black
          as-bold as-italic as-underline as-color as-true-color
          on-red on-blue on-green on-cyan on-yellow on-magenta on-white on-black
          on-color on-true-color)
  (begin

    (define (%sgr params) (string-append "\x1b;[" params "m"))

    (define (%wrap on off fmts)
      (apply each (append (list (%sgr on)) fmts (list (%sgr off)))))

    (define (%n->s n) (number->string n))

    (define (%fg n) (lambda fmts (%wrap (%n->s (+ 30 n)) "39" fmts)))
    (define (%bg n) (lambda fmts (%wrap (%n->s (+ 40 n)) "49" fmts)))

    (define as-black (%fg 0)) (define as-red (%fg 1)) (define as-green (%fg 2))
    (define as-yellow (%fg 3)) (define as-blue (%fg 4)) (define as-magenta (%fg 5))
    (define as-cyan (%fg 6)) (define as-white (%fg 7))
    (define on-black (%bg 0)) (define on-red (%bg 1)) (define on-green (%bg 2))
    (define on-yellow (%bg 3)) (define on-blue (%bg 4)) (define on-magenta (%bg 5))
    (define on-cyan (%bg 6)) (define on-white (%bg 7))

    (define (as-bold . fmts) (%wrap "1" "22" fmts))
    (define (as-italic . fmts) (%wrap "3" "23" fmts))
    (define (as-underline . fmts) (%wrap "4" "24" fmts))

    (define (%check-range who v lo hi)
      (unless (and (exact-integer? v) (<= lo v hi))
        (error (string-append who ": component out of range") v lo hi)))

    ;; 8-bit color: components 0-5 address the 6x6x6 cube at 16..231.
    (define (%cube who r g b)
      (%check-range who r 0 5) (%check-range who g 0 5) (%check-range who b 0 5)
      (%n->s (+ 16 (* 36 r) (* 6 g) b)))

    (define (%rgb who r g b)
      (%check-range who r 0 255) (%check-range who g 0 255) (%check-range who b 0 255)
      (string-append (%n->s r) ";" (%n->s g) ";" (%n->s b)))

    (define (as-color r g b . fmts)
      (%wrap (string-append "38;5;" (%cube "as-color" r g b)) "39" fmts))
    (define (as-true-color r g b . fmts)
      (%wrap (string-append "38;2;" (%rgb "as-true-color" r g b)) "39" fmts))
    (define (on-color r g b . fmts)
      (%wrap (string-append "48;5;" (%cube "on-color" r g b)) "49" fmts))
    (define (on-true-color r g b . fmts)
      (%wrap (string-append "48;2;" (%rgb "on-true-color" r g b)) "49" fmts))))
