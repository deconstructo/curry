;;; srfi_legacy_dashed_names_tests.scm — regression coverage for the
;;; legacy dashed-name library aliases (e.g. (srfi srfi-170), distinct
;;; from the numeric (srfi 170)). A code-review pass found these four
;;; hadn't been updated to re-export the new Tier-1 SRFI-gap additions
;;; alongside their numeric-named counterparts -- `(import (srfi
;;; srfi-170))` then calling `user-info:parsed-full-name` raised
;;; unbound-variable while `(import (srfi 170))` worked fine for the
;;; exact same call. This file exists specifically so that class of gap
;;; can't reappear silently.

(import (srfi srfi-125) (srfi srfi-128) (srfi srfi-170) (srfi srfi-227)
        (srfi srfi-9) (srfi srfi-31) (srfi srfi-45)
        (srfi srfi-95) (srfi srfi-78) (srfi srfi-212))

(define pass 0)
(define fail 0)

;; Named assert-equal, not check -- (srfi srfi-78) exports its own `check`
;; syntax, and this file exercises that real macro directly further down;
;; a same-named local procedure would shadow it and silently turn
;; `(check (+ 1 1) => 2)` into a 3-argument call to this procedure
;; instead (with `=>` evaluated as a bare, unbound variable).
(define (assert-equal label result expected)
  (if (equal? result expected)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write result)
             (display " expected ") (write expected)
             (newline)
             (set! fail (+ fail 1)))))

;;; (srfi srfi-125)
(assert-equal "srfi-125 legacy shim exports hash-table-mutable?"
       (hash-table-mutable? (make-hash-table (make-equal-comparator)))
       #t)

;;; (srfi srfi-128)
(assert-equal "srfi-128 legacy shim exports make-eq-comparator"
       (comparator? (make-eq-comparator))
       #t)

;;; (srfi srfi-170)
(assert-equal "srfi-170 legacy shim exports owner/unchanged" owner/unchanged -1)
(assert-equal "srfi-170 legacy shim exports group/unchanged" group/unchanged -1)
(assert-equal "srfi-170 legacy shim exports user-info:parsed-full-name"
       (string? (user-info:parsed-full-name (user-info (user-uid))))
       #t)

;;; (srfi srfi-227)
(define-optionals (f a #:optional (b (* a 2))) (+ a b))
(assert-equal "srfi-227 legacy shim exports define-optionals" (f 5) 15)

(define-optionals* (g a #:optional (b (* a 3)) (c (+ a b))) (list a b c))
(assert-equal "srfi-227 legacy shim exports define-optionals*" (g 4) (list 4 12 16))

;;; (srfi srfi-9)
(define-record-type <pt> (make-pt x) pt? (x pt-x))
(assert-equal "srfi-9 legacy shim exports define-record-type" (pt-x (make-pt 7)) 7)

;;; (srfi srfi-31)
(assert-equal "srfi-31 legacy shim exports rec" (rec x (+ 1 1)) 2)

;;; (srfi srfi-45)
(assert-equal "srfi-45 legacy shim exports lazy/force/eager"
       (force (eager (force (lazy 5))))
       5)

;;; (srfi srfi-95)
(assert-equal "srfi-95 legacy shim exports sort" (sort (list 3 1 2) <) (list 1 2 3))

;;; (srfi srfi-78)
;; 'summary, not 'off -- 'off means the check is never even evaluated or
;; counted (see s78/lightweight-testing.scm's own comment).
(check-set-mode! 'summary)
(check (+ 1 1) => 2)
(assert-equal "srfi-78 legacy shim exports check/check-passed?" (check-passed? 1) #t)

;;; (srfi srfi-212)
(define (%dashed-original x) (+ x 1))
(define-alias %dashed-alias %dashed-original)
(assert-equal "srfi-212 legacy shim exports define-alias" (%dashed-alias 9) 10)

;;; Summary

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
