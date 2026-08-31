;;; case_lambda_tests.scm — (scheme case-lambda), R7RS.
;;;
;;; Previously (scheme case-lambda) was listed in modules.c's "alias
;;; GLOBAL_ENV" table alongside the real (scheme base)/(scheme write)/etc.
;;; libraries -- (import (scheme case-lambda)) silently succeeded while
;;; providing nothing at all, since GLOBAL_ENV never actually had a
;;; case-lambda binding. Fixed with a real implementation.
;;;
;;; Also regression coverage for the specific shape of a real
;;; syntax-rules engine bug found while implementing this (see issue
;;; #101): case-lambda's own outer macro is written to deliberately avoid
;;; an outer `...` wrapping a sub-pattern that itself contains a variable
;;; followed by its own `...` (a nested-ellipsis shape src/syntax_rules.c
;;; mishandles) -- these tests exercise every arity shape case-lambda
;;; needs (fixed, variadic, multiple clauses) as the actual proof the
;;; workaround produces correct behavior, not just that it doesn't crash.

(import (scheme case-lambda))

(define pass 0)
(define fail 0)

(define (check label got expected)
  (if (equal? got expected)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " — got ") (write got)
             (display "  expected ") (write expected) (newline)
             (set! fail (+ fail 1)))))

(define f
  (case-lambda
    (() 'zero)
    ((a) (list 'one a))
    ((a b) (list 'two a b))
    ((a b . rest) (list 'many a b rest))))

(check "case-lambda: zero-arg clause" (f) 'zero)
(check "case-lambda: one-arg clause" (f 1) '(one 1))
(check "case-lambda: two-arg clause" (f 1 2) '(two 1 2))
(check "case-lambda: variadic clause, exact rest boundary" (f 1 2 3 4) '(many 1 2 (3 4)))
(check "case-lambda: variadic clause, more args" (f 1 2 3 4 5) '(many 1 2 (3 4 5)))

;; R7RS's own canonical example.
(define range
  (case-lambda
    ((e) (range 0 e))
    ((b e) (let loop ((i b) (acc '()))
             (if (>= i e) (reverse acc) (loop (+ i 1) (cons i acc)))))))
(check "case-lambda: R7RS range example, one arg" (range 3) '(0 1 2))
(check "case-lambda: R7RS range example, two args" (range 2 5) '(2 3 4))

;; A purely-variadic single clause (no fixed clauses at all).
(define g (case-lambda (args args)))
(check "case-lambda: single fully-variadic clause" (g 1 2 3) '(1 2 3))
(check "case-lambda: single fully-variadic clause, zero args" (g) '())

;; No matching clause raises rather than silently misbehaving, and the
;; error names the actual argument count that failed to match (found by
;; independent review: the original message gave no way to tell what was
;; actually wrong).
(define h (case-lambda ((a) a) ((a b) (+ a b))))
(check "case-lambda: no matching clause raises"
       (guard (e (#t 'raised)) (h 1 2 3))
       'raised)
(check "case-lambda: no-match error names the actual arity"
       (guard (e (#t (error-object-irritants e))) (h 1 2 3))
       '(3))

;; Regression: curry's syntax-rules does not rename template-introduced
;; binders on collision with a user identifier (a real hygiene gap,
;; independent of issue #101's nested-ellipsis bug) -- case-lambda's own
;; expansion used to bind plain `args`/`len`, so a clause body referencing
;; an outer variable literally named `args` or `len` silently saw the
;; macro's own internal value instead, with no error. Found by independent
;; review; fixed by renaming the template's own binders to `%cl-args`/
;; `%cl-len`.
(define args 'outer-args)
(define len 'outer-len)
(check "case-lambda: does not capture a user variable named args/len"
       ((case-lambda ((a) (list args len))) 1)
       '(outer-args outer-len))

;;; Summary

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
