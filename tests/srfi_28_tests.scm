;;; srfi_28_tests.scm — (srfi 28) Basic Format Strings: the format
;;; procedure, its ~a/~s/~%/~~ directives, and the ~N@* positional-
;;; reference directive (specified by SRFI-29 as an extension to this
;;; same format engine, but implemented here since it's part of the one
;;; shared format procedure both SRFIs use -- see format.scm's own
;;; header comment).

(import (scheme base) (srfi 28))

(define pass 0)
(define fail 0)

(define-syntax check
  (syntax-rules ()
    ((_ label expr expected)
     (let ((got expr))
       (if (equal? got expected)
           (begin (set! pass (+ pass 1)))
           (begin
             (set! fail (+ fail 1))
             (display "FAIL: ") (display label) (newline)
             (display "  expected: ") (write expected) (newline)
             (display "  got:      ") (write got) (newline)))))))

;;; ---- basic directives ----

(check "~a displays (no quoting)" (format "~a" "hi") "hi")
(check "~a on a non-string object" (format "~a" 42) "42")
(check "~a on a list" (format "~a" (list 1 2 3)) "(1 2 3)")
(check "~s writes (quoting)" (format "~s" "hi") "\"hi\"")
(check "~s on a symbol" (format "~s" 'foo) "foo")
(check "~% inserts a newline" (format "a~%b") "a\nb")
(check "~~ inserts a literal tilde" (format "~~") "~")
(check "plain text passes through unchanged" (format "hello, world") "hello, world")
(check "multiple directives interleaved with text"
       (format "~a is ~a, ~s isn't." "Hi" "friendly" 5)
       "Hi is friendly, 5 isn't.")
(check "consecutive directives with no text between"
       (format "~a~a~a" 1 2 3) "123")

;;; ---- error conditions (SRFI-28's own named cases) ----

(check "trailing bare ~ at end of string raises"
       (guard (e (#t 'caught)) (format "abc~"))
       'caught)
(check "unrecognized directive raises"
       (guard (e (#t 'caught)) (format "~q" 1))
       'caught)
(check "too few arguments for ~a raises"
       (guard (e (#t 'caught)) (format "~a"))
       'caught)
(check "too few arguments for ~s raises"
       (guard (e (#t 'caught)) (format "~a ~s" 1))
       'caught)

;;; ---- ~N@* positional reference (SRFI-29's extension) ----

(check "~N@* references an argument out of sequence, without consuming it"
       (format "~1@*~a, c'est ~a." "fromage" "bon")
       "bon, c'est fromage.")
(check "~0@* references the first argument explicitly"
       (format "~0@*~a" "first") "first")
(check "a positional reference doesn't disturb the sequential cursor for later directives"
       (format "~a ~1@*~a ~a" "A" "B" "C")
       ;; cursor: ~a consumes A (cursor->1); ~1@*~a reads index 1 (B),
       ;; cursor unchanged at 1; plain ~a consumes cursor position 1 (B)
       "A B B")
(check "out-of-range positional index raises"
       (guard (e (#t 'caught)) (format "~5@*~a" "only-one"))
       'caught)
(check "~N@* not followed by ~a or ~s raises"
       (guard (e (#t 'caught)) (format "~1@*~%"))
       'caught)
(check "malformed ~N (missing @*) raises"
       (guard (e (#t 'caught)) (format "~1a" "x"))
       'caught)

;;; ---- Summary ----

(newline)
(display "srfi-28 tests: ")
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0)
    (begin (display "SOME TESTS FAILED") (newline) (exit 1))
    (begin (display "all OK") (newline)))
