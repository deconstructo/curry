;;; srfi_legacy_dashed_names_tests.scm — regression coverage for the
;;; legacy dashed-name library aliases (e.g. (srfi srfi-170), distinct
;;; from the numeric (srfi 170)). A code-review pass found these four
;;; hadn't been updated to re-export the new Tier-1 SRFI-gap additions
;;; alongside their numeric-named counterparts -- `(import (srfi
;;; srfi-170))` then calling `user-info:parsed-full-name` raised
;;; unbound-variable while `(import (srfi 170))` worked fine for the
;;; exact same call. This file exists specifically so that class of gap
;;; can't reappear silently.

(import (srfi srfi-125) (srfi srfi-128) (srfi srfi-170) (srfi srfi-227))

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

;;; (srfi srfi-125)
(check "srfi-125 legacy shim exports hash-table-mutable?"
       (hash-table-mutable? (make-hash-table (make-equal-comparator)))
       #t)

;;; (srfi srfi-128)
(check "srfi-128 legacy shim exports make-eq-comparator"
       (comparator? (make-eq-comparator))
       #t)

;;; (srfi srfi-170)
(check "srfi-170 legacy shim exports owner/unchanged" owner/unchanged -1)
(check "srfi-170 legacy shim exports group/unchanged" group/unchanged -1)
(check "srfi-170 legacy shim exports user-info:parsed-full-name"
       (string? (user-info:parsed-full-name (user-info (user-uid))))
       #t)

;;; (srfi srfi-227)
(define-optionals (f a #:optional (b (* a 2))) (+ a b))
(check "srfi-227 legacy shim exports define-optionals" (f 5) 15)

(define-optionals* (g a #:optional (b (* a 3)) (c (+ a b))) (list a b c))
(check "srfi-227 legacy shim exports define-optionals*" (g 4) (list 4 12 16))

;;; Summary

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
