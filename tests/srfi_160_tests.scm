;;; srfi_160_tests.scm — (srfi 160): SRFI-4 base ops plus the extended
;;; SRFI-160-style surface (map/fold/filter/comparator/generator/etc)
;;; for all 9 kinds (u8..s64, f64), layered in pure Scheme on top of
;;; (srfi 4). Full coverage on u8 (small unsigned) and spot checks on
;;; s64 (signed, bignum boundary) and f64 (float) for cross-kind
;;; regressions, since the same generated code is shared by all 9.

(import (srfi 160))

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

;;; ---- base SRFI-4 ops still reachable through (srfi 160) ----

(check "u8vector base ops reachable" (u8vector->list (u8vector 1 2 3)) '(1 2 3))
(check "f64vector-append reachable (2-arg only, unlike the other 8 kinds)"
       (f64vector->list (f64vector-append (f64vector 1.0) (f64vector 2.0)))
       '(1.0 2.0))

;;; ---- u8vector: full extended-op coverage ----

(check "u8vector-empty? true" (u8vector-empty? (u8vector)) #t)
(check "u8vector-empty? false" (u8vector-empty? (u8vector 1)) #f)

(check "u8vector= equal" (u8vector= (u8vector 1 2 3) (u8vector 1 2 3)) #t)
(check "u8vector= different length" (u8vector= (u8vector 1 2) (u8vector 1 2 3)) #f)
(check "u8vector= different element" (u8vector= (u8vector 1 2 3) (u8vector 1 2 4)) #f)
;; regression: the original generated code called (loop rest) instead of
;; (loop (cdr rest)) in the 3+-argument chain, an infinite loop -- this
;; specifically exercises the chain past the first pair.
(check "u8vector= three-way chain terminates and is correct"
       (u8vector= (u8vector 1 2) (u8vector 1 2) (u8vector 1 2))
       #t)
(check "u8vector= three-way chain, third differs"
       (u8vector= (u8vector 1 2) (u8vector 1 2) (u8vector 1 3))
       #f)

(let ((v (u8vector 1 2 3)))
  (u8vector-swap! v 0 2)
  (check "u8vector-swap!" (u8vector->list v) '(3 2 1)))

(let ((v (u8vector 1 2 3 4)))
  (u8vector-reverse! v)
  (check "u8vector-reverse!" (u8vector->list v) '(4 3 2 1)))

(check "u8vector-reverse-copy" (u8vector->list (u8vector-reverse-copy (u8vector 1 2 3))) '(3 2 1))
(check "u8vector-reverse-copy leaves original untouched"
       (let ((v (u8vector 1 2 3))) (u8vector-reverse-copy v) (u8vector->list v))
       '(1 2 3))

(check "u8vector-map" (u8vector->list (u8vector-map (lambda (x) (* x 2)) (u8vector 1 2 3))) '(2 4 6))
(check "u8vector-map two vectors" (u8vector->list (u8vector-map + (u8vector 1 2 3) (u8vector 10 20 30))) '(11 22 33))

(let ((v (u8vector 1 2 3)))
  (u8vector-map! (lambda (x) (+ x 1)) v)
  (check "u8vector-map!" (u8vector->list v) '(2 3 4)))

(check "u8vector-for-each side effects"
       (let ((acc 0)) (u8vector-for-each (lambda (x) (set! acc (+ acc x))) (u8vector 1 2 3)) acc)
       6)

(check "u8vector-count" (u8vector-count odd? (u8vector 1 2 3 4 5)) 3)
(check "u8vector-index found" (u8vector-index even? (u8vector 1 3 4 5)) 2)
(check "u8vector-index not found" (u8vector-index even? (u8vector 1 3 5)) #f)
(check "u8vector-index-right" (u8vector-index-right even? (u8vector 2 3 4 5)) 2)
(check "u8vector-skip" (u8vector-skip even? (u8vector 2 4 5 6)) 2)
;; -skip-right finds the rightmost index where the predicate is FALSE
;; (i.e. index-right of (not pred)): scanning (2 4 5 6) right-to-left,
;; index 3 (6) is even so it's skipped, index 2 (5) is odd -> matches.
(check "u8vector-skip-right" (u8vector-skip-right even? (u8vector 2 4 5 6)) 2)

(check "u8vector-any true" (u8vector-any even? (u8vector 1 3 4)) #t)
(check "u8vector-any false" (u8vector-any even? (u8vector 1 3 5)) #f)
(check "u8vector-every true" (u8vector-every odd? (u8vector 1 3 5)) #t)
(check "u8vector-every false" (u8vector-every odd? (u8vector 1 3 4)) #f)

(check "u8vector-filter" (u8vector->list (u8vector-filter even? (u8vector 1 2 3 4 5 6))) '(2 4 6))
(check "u8vector-remove" (u8vector->list (u8vector-remove even? (u8vector 1 2 3 4 5 6))) '(1 3 5))
(check "u8vector-partition"
       (call-with-values (lambda () (u8vector-partition even? (u8vector 1 2 3 4)))
         (lambda (yes no) (list (u8vector->list yes) (u8vector->list no))))
       '((2 4) (1 3)))

(check "u8vector-fold" (u8vector-fold + 0 (u8vector 1 2 3 4)) 10)
;; fold's kons order is (kons acc elem), the opposite of fold-right below
;; -- cons-ing acc first nests rather than building a reversed list.
(check "u8vector-fold order (acc first)" (u8vector-fold cons '() (u8vector 1 2 3)) '(((() . 1) . 2) . 3))
;; regression: fold-right must pass elements first, accumulator LAST to
;; kons (the opposite order from fold) -- the original generated code
;; used fold's order for both, producing garbage for non-commutative kons.
(check "u8vector-fold-right order (elements first, acc last)"
       (u8vector-fold-right cons '() (u8vector 1 2 3))
       '(1 2 3))
(check "u8vector-fold-right with +" (u8vector-fold-right + 0 (u8vector 1 2 3 4)) 10)

(check "u8vector-concatenate" (u8vector->list (u8vector-concatenate (list (u8vector 1 2) (u8vector 3 4)))) '(1 2 3 4))
(check "u8vector-concatenate empty list" (u8vector->list (u8vector-concatenate '())) '())

(check "u8vector-unfold"
       (u8vector->list (u8vector-unfold (lambda (i seed) (values seed (+ seed 1))) 5 0))
       '(0 1 2 3 4))
(check "u8vector-unfold-right"
       (u8vector->list (u8vector-unfold-right (lambda (i seed) (values seed (+ seed 1))) 5 0))
       '(4 3 2 1 0))

;;; ---- comparators ----

(check "u8vector-comparator is a comparator" (comparator? u8vector-comparator) #t)
(check "u8vector-comparator =? equal" (=? u8vector-comparator (u8vector 1 2) (u8vector 1 2)) #t)
(check "u8vector-comparator =? unequal" (=? u8vector-comparator (u8vector 1 2) (u8vector 1 3)) #f)
(check "u8vector-comparator <? shorter first" (<? u8vector-comparator (u8vector 1) (u8vector 1 2)) #t)
(check "u8vector-comparator <? elementwise" (<? u8vector-comparator (u8vector 1 2) (u8vector 1 3)) #t)

;;; ---- generators ----

(check "u8vector->generator"
       (let ((g (u8vector->generator (u8vector 9 8 7))))
         (list (g) (g) (g) (eof-object? (g))))
       '(9 8 7 #t))
(check "u8vector->generator with range"
       (let ((g (u8vector->generator (u8vector 9 8 7 6) 1 3)))
         (list (g) (g) (eof-object? (g))))
       '(8 7 #t))
(check "make-u8vector-generator alias" (procedure? (make-u8vector-generator (u8vector 1))) #t)

;;; ---- s64vector: bignum boundary spot check ----

(check "s64vector-map preserves bignum-range values"
       (s64vector->list (s64vector-map (lambda (x) x) (s64vector -9223372036854775808 9223372036854775807)))
       '(-9223372036854775808 9223372036854775807))
(check "s64vector-fold with bignum values"
       (s64vector-fold + 0 (s64vector 9223372036854775807 1))
       9223372036854775808)
(check "u64vector-comparator on UINT64_MAX"
       (=? u64vector-comparator (u64vector 18446744073709551615) (u64vector 18446744073709551615))
       #t)

;;; ---- f64vector: float spot check ----

(check "f64vector-map" (f64vector->list (f64vector-map (lambda (x) (* x 2.0)) (f64vector 1.5 2.5))) '(3.0 5.0))
(check "f64vector-fold" (f64vector-fold + 0.0 (f64vector 1.5 2.5)) 4.0)
(check "f64vector-concatenate folds pairwise over the 2-arg f64vector-append"
       (f64vector->list (f64vector-concatenate (list (f64vector 1.0 2.0) (f64vector 3.0) (f64vector 4.0 5.0))))
       '(1.0 2.0 3.0 4.0 5.0))
(check "f64vector-concatenate empty list" (f64vector->list (f64vector-concatenate '())) '())
(check "f64vector-comparator" (=? f64vector-comparator (f64vector 1.0 2.0) (f64vector 1.0 2.0)) #t)

;;; ---- Summary ----

(newline)
(display "srfi-160 tests: ")
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0)
    (begin (display "SOME TESTS FAILED") (newline) (exit 1))
    (begin (display "all OK") (newline)))
