;;; (srfi srfi-N) numbered re-export shims over (srfi sN name)
;;;
;;; SRFI-261 (finalized 2025-12-07) specifies `(srfi srfi-N)` as the primary
;;; portable form for referring to SRFI N, distinct from both `(srfi N)`
;;; (an R7RS-restricted shorthand SRFI-261 also documents, which curry
;;; already implemented -- see srfi_numbered_shims_tests.scm) and curry's
;;; own descriptive `(srfi sN name)` naming. Each `(srfi srfi-N)` shim just
;;; imports the corresponding `(srfi sN name)` library and re-exports its
;;; bindings, mirroring `(srfi N)`'s shim exactly one library-name segment
;;; different. This file mirrors srfi_numbered_shims_tests.scm's checks
;;; (same set of libraries, same collision-avoidance rationale for why
;;; 69/90/126 aren't imported here) but via the srfi-N form, to prove the
;;; new naming convention actually resolves and re-exports correctly.

(import (srfi srfi-1) (srfi srfi-8) (srfi srfi-18) (srfi srfi-19) (srfi srfi-27)
        (srfi srfi-59) (srfi srfi-98) (srfi srfi-112) (srfi srfi-113)
        (srfi srfi-128) (srfi srfi-132) (srfi srfi-133) (srfi srfi-145)
        (srfi srfi-158) (srfi srfi-170) (srfi srfi-174) (srfi srfi-194)
        (srfi srfi-215) (srfi srfi-227) (srfi srfi-238))

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

(check "(srfi srfi-1): iota" (iota 3) '(0 1 2))
(check "(srfi srfi-8): receive" (receive (a b) (values 1 2) (+ a b)) 3)
(check "(srfi srfi-18): thread-start!/thread-join!"
       (thread-join! (thread-start! (make-thread (lambda () 42))))
       42)
(check "(srfi srfi-19): make-date/date?" (date? (make-date 0 0 0 0 1 1 2000 0)) #t)
(check "(srfi srfi-27): random-real returns a real" (real? (random-real)) #t)
(check "(srfi srfi-59): pathname->vicinity" (pathname->vicinity "/a/b/c.scm") "/a/b/")
(check "(srfi srfi-98): get-environment-variables returns a list" (list? (get-environment-variables)) #t)
(check "(srfi srfi-112): implementation-name returns a string" (string? (implementation-name)) #t)
(check "(srfi srfi-113): set-size" (set-size (set equal-comparator 1 2 3)) 3)
(check "(srfi srfi-128): comparator? on a basic-type comparator" (comparator? real-comparator) #t)
(check "(srfi srfi-132): list-sort" (list-sort < '(3 1 2)) '(1 2 3))
(check "(srfi srfi-133): vector-fold" (vector-fold + 0 #(1 2 3)) 6)
(check "(srfi srfi-145): assume raises on a false expression"
       (guard (e (#t 'caught)) (assume #f))
       'caught)
(check "(srfi srfi-158): generator->list" (generator->list (list->generator '(1 2 3))) '(1 2 3))
(check "(srfi srfi-170): file-info is bound" (procedure? file-info) #t)
(check "(srfi srfi-174): timespec constructor/predicate" (timespec? (timespec 1 0)) #t)
(check "(srfi srfi-194): random-integer-generator produces an integer"
       (integer? ((make-random-integer-generator 0 10)))
       #t)
(check "(srfi srfi-215): send-log is bound" (procedure? send-log) #t)
(define of (opt-lambda (a #:optional (b 10)) (+ a b)))
(check "(srfi srfi-227): opt-lambda with a defaulted optional" (of 5) 15)
(check "(srfi srfi-238): codeset? is bound" (procedure? codeset?) #t)

;;; Summary

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
