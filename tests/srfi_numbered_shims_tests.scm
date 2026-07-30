;;; (srfi N) numbered re-export shims over (srfi sN name)
;;;
;;; Each shim just imports the corresponding (srfi sN name) library and
;;; re-exports its bindings under the bare-number library name (srfi N),
;;; matching SRFI-97's naming convention (as used by Chibi-Scheme, Gauche,
;;; and CHICKEN in R7RS mode). Deliberately exercised one shim per process
;;; here via separate `guard`-free top-level checks rather than importing
;;; every numbered SRFI into one script: several (e.g. 69/90 vs 125/126)
;;; export the same generic names (make-hash-table, hash-table-ref, ...)
;;; with different semantics, and curry's flat top-level namespace means
;;; importing more than one into the same scope has the same "last one in
;;; wins, or doesn't" ambiguity any Scheme has for colliding imports — not
;;; a shim bug, just not a realistic thing to do in one file.

(import (srfi 1) (srfi 8) (srfi 18) (srfi 19) (srfi 27) (srfi 59) (srfi 64)
        (srfi 98) (srfi 112) (srfi 113) (srfi 128) (srfi 132) (srfi 133) (srfi 145)
        (srfi 158) (srfi 170) (srfi 174) (srfi 194) (srfi 215) (srfi 227)
        (srfi 238))

(define pass 0)
(define fail 0)

(define (check label result expected)
  (if (equal? result expected)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write result)
             (display " expected ") (write expected)
             (newline)
             (set! fail (+ fail 1)))))

(check "(srfi 1): iota" (iota 3) '(0 1 2))
(check "(srfi 8): receive" (receive (a b) (values 1 2) (+ a b)) 3)
(check "(srfi 18): thread-start!/thread-join!"
       (thread-join! (thread-start! (make-thread (lambda () 42))))
       42)
(check "(srfi 19): make-date/date?" (date? (make-date 0 0 0 0 1 1 2000 0)) #t)
(check "(srfi 27): random-real returns a real" (real? (random-real)) #t)
(check "(srfi 59): pathname->vicinity" (pathname->vicinity "/a/b/c.scm") "/a/b/")
(check "(srfi 64): test-assert is bound" (if test-assert #t #f) #t)
(check "(srfi 98): get-environment-variables returns a list" (list? (get-environment-variables)) #t)
(check "(srfi 112): implementation-name returns a string" (string? (implementation-name)) #t)
(check "(srfi 113): set-size" (set-size (set equal-comparator 1 2 3)) 3)
(check "(srfi 128): comparator? on a basic-type comparator" (comparator? real-comparator) #t)
(check "(srfi 132): list-sort" (list-sort < '(3 1 2)) '(1 2 3))
(check "(srfi 133): vector-fold" (vector-fold + 0 #(1 2 3)) 6)
(check "(srfi 145): assume raises on a false expression"
       (guard (e (#t 'caught)) (assume #f))
       'caught)
(check "(srfi 158): generator->list" (generator->list (list->generator '(1 2 3))) '(1 2 3))
(check "(srfi 170): file-info is bound" (procedure? file-info) #t)
(check "(srfi 174): timespec constructor/predicate" (timespec? (timespec 1 0)) #t)
(check "(srfi 194): random-integer-generator produces an integer"
       (integer? ((make-random-integer-generator 0 10)))
       #t)
(check "(srfi 215): send-log is bound" (procedure? send-log) #t)
(define of (opt-lambda (a #:optional (b 10)) (+ a b)))
(check "(srfi 227): opt-lambda with a defaulted optional" (of 5) 15)
(check "(srfi 238): codeset? is bound" (procedure? codeset?) #t)

;;; Summary

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
