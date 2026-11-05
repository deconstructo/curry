;;; Cuneiform numerals inside lists and vectors (issue #354).
;;;
;;; At top level a space separates the place groups of a sexagesimal
;;; literal (`𒁹 𒌋𒁹` is 71). Inside a list or vector a space always ends
;;; a token, so `(𒁹 𒌋)` has two elements. A number whose cuneiform text
;;; has a space, or is also an Akkadian keyword (𒁹 = 1 is `define`, 𒌋 = 10
;;; is `and`), is written `#s1,10` inside a container, which reads back to
;;; the same number anywhere.

(define pass-count 0)
(define fail-count 0)
(define (check name expected actual)
  (if (equal? expected actual)
      (set! pass-count (+ pass-count 1))
      (begin
        (set! fail-count (+ fail-count 1))
        (display "FAIL: ") (display name)
        (display " expected=") (write expected)
        (display " actual=") (write actual) (newline))))

(define (rd s) (read (open-input-string s)))
(define (wr x) (let ((p (open-output-string))) (write x p) (get-output-string p)))
(define (dsp x) (let ((p (open-output-string))) (display x p) (get-output-string p)))

;; ── Reading ─────────────────────────────────────────────────────────────
(check "top level: places are separated by spaces" 71 (rd "𒁹 𒌋𒁹"))
(check "top level: three places (was cut off after two)" 3661 (rd "𒁹 𒁹 𒁹"))
(check "list: a space ends the token" 2 (length (rd "(𒁹 𒌋)")))
(check "list: three elements" 3 (length (rd "(𒌋 𒌋 𒌋)")))
(check "list: a number and a word" '(2 3) (map (lambda (x) (if (number? x) x 0)) (list 2 3)))
(check "list: digits in one token still add up" '(5 70)
       (rd "(𒁹𒁹𒁹𒁹𒁹 #s1,10)"))
(check "vector: a space ends the token" 2 (vector-length (rd "#(𒁹 𒌋)")))
(check "bracket list" 2 (length (rd "[𒁹 𒌋]")))
(check "nested lists" '(2 1) (map length (rd "((𒁹 𒌋) (𒁹))")))
(check "a quoted list" 2 (length (cadr (rd "'(𒁹 𒌋)"))))
(check "a keyword in a list is still a symbol" #t (symbol? (car (rd "(𒁹 𒌋)"))))
(check "dotted pair" '(70 . 71) (rd "(#s1,10 . #s1,11)"))
(check "reading a list does not change the next top-level read" 71
       (let ((p (open-input-string "(𒁹 𒌋) 𒁹 𒌋𒁹")))
         (read p) (read p)))
(check "a read error does not leave container state behind" 71
       (begin (guard (e (#t #f)) (rd "(𒁹 𒌋"))
              (rd "𒁹 𒌋𒁹")))

;; ── #s with a sign ──────────────────────────────────────────────────────
(check "#s negative" -70 (rd "#s-1,10"))
(check "#s positive sign" 70 (rd "#s+1,10"))
(check "#s negative fraction" -1/2 (rd "#s-0;30"))
(check "#s sign alone is an error" 'error (guard (e (#t 'error)) (rd "#s-")))
(check "#s two signs is an error" 'error (guard (e (#t 'error)) (rd "#s--1,10")))
(check "#s mixed signs is an error" 'error (guard (e (#t 'error)) (rd "#s-+1")))
(check "#s trailing junk is an error" 'error (guard (e (#t 'error)) (rd "#s-1,10x")))

;; ── Writing ─────────────────────────────────────────────────────────────
(current-number-notation 'cuneiform)
(check "top level: the writer's own multi-place output reads back" 100000 (rd (wr 100000)))
(check "top level keeps cuneiform" "𒁹 𒌋" (wr 70))
(check "top level, one place" "𒁹𒁹𒁹𒁹𒁹" (wr 5))
(check "list: a single-glyph keyword numeral" "(#s1 #s10)" (wr '(1 10)))
(check "list: a plain run of digits" "(𒁹𒁹𒁹𒁹𒁹)" (wr '(5)))
(check "list: two places" "(#s1,10)" (wr '(70)))
(check "list: negative" "(#s-1,10)" (wr '(-70)))
(check "list: fraction" "(#s0;30 #s1;30)" (wr '(1/2 3/2)))
(check "list: an inexact number stays decimal" "(0.5 5.0)" (wr '(0.5 5.0)))
(check "list: a negative single-place number" "(#s-1 #s-5)" (wr '(-1 -5)))
(check "negatives read back" '(-1 -5 -70) (rd (wr '(-1 -5 -70))))
(check "vector" "#(#s1,10 𒁹𒁹𒁹𒁹𒁹)" (wr (vector 70 5)))
(check "pair" "(#s1,10 . #s1,11)" (wr (cons 70 71)))
(check "display" "(#s1,10)" (dsp '(70)))
(check "write-shared" "(#s1,10)"
       (let ((p (open-output-string))) (write-shared '(70) p) (get-output-string p)))

;; ── Round trips ─────────────────────────────────────────────────────────
(define data '(1 10 5 59 60 70 3661 -70 1/2 3/2 0.5 100000 (3 (70 1/3)) 0))
(check "a nested list reads back equal" data (rd (wr data)))
(check "a vector reads back equal" (list->vector data) (rd (wr (list->vector data))))
(current-number-notation #f)
(check "decimal output is untouched" "(1 10 70)" (wr '(1 10 70)))

(display "cuneiform list tests: ") (display pass-count) (display " passed, ")
(display fail-count) (display " failed") (newline)
(if (> fail-count 0) (exit 1))
