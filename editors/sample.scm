#!/usr/bin/env curry
;;; sample.scm — syntax-highlighting showcase (not part of the test suite).
;;; Open this file after installing an editors/ grammar to check the colors.

#| block comment
   #| nested block comment |#
   still inside the outer comment |#

;; ---- special forms and builtins ----------------------------------
(define (fact n)
  (if (<= n 1) 1 (* n (fact (- n 1)))))

(let loop ((i 0) (acc '()))
  (if (>= i 10)
      (reverse acc)
      (loop (+ i 1) (cons i acc))))

(map (lambda (x) (* x x)) '(1 2 3))

;; ---- literals ------------------------------------------------------
(define greeting "hello\nworld \x3bb; end")
(define ch #\newline)
(define hex-char #\x41)
(define flags (list #t #f #true #false))
(define nums (list 42 -17 3.14 1/3 6.02e23 #xFF #b1010 #o755))
(define kw #:when)

;; ---- Babylonian sexagesimal ---------------------------------------
(define three-halves #s1;30)      ; Neugebauer: 1;30 = 3/2
(define one-hour #s1,0,0)         ; 1,0,0 = 3600
(define eleven 𒌋𒁹)               ; cuneiform numeral 11
(define seventy-one 𒁹 𒌋𒁹)        ; single-space groups merge: 1,11 = 71
#;(this whole datum is commented out)

;; ---- Akkadian synonyms — the same language, older words -----------
;; A lone digit-glyph is a synonym, not a number: 𒁹 is `define`.
;; Cuneiform tokens are separated by DOUBLE spaces (single spaces merge
;; adjacent numeral groups into one sexagesimal token, as above).
(šakānum y (matāḫum 1 2))         ; (define y (+ 1 2))
(𒁹 z (𒋻𒁹  𒌋𒁹  𒁹𒁹))            ; (define z (+ 11 2))
(naṭālum (rēšum '(1 2 3)))        ; (display (car '(1 2 3)))
(𒅆 (𒊕 '(4 5 6)))                  ; same, in cuneiform

;; ---- records, quasiquote, CAS -------------------------------------
(define-record-type point
  (make-point x y) point?
  (x point-x) (y point-y))

(define template `(a ,y ,@nums #(1 2) #u8(1 2 3)))

(define x (sym-var 'x))
(define dx (simplify (sym-diff (* x x) x)))    ; d/dx x² = 2x
