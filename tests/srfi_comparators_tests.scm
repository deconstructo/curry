;;; (srfi s128 comparators)

(import (srfi s128 comparators))

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

(check "comparator? on a basic-type comparator" (comparator? real-comparator) #t)
(check "=? chains equality across args" (=? real-comparator 1 1 1) #t)
(check "<? true for ascending" (<? real-comparator 1 2 3) #t)
(check "<? false for non-ascending" (<? real-comparator 1 3 2) #f)
(check ">? true for descending" (>? real-comparator 3 2 1) #t)
(check "<=? true for equal run" (<=? real-comparator 1 1 2) #t)
(check "string-comparator equality" (=? string-comparator "abc" "abc") #t)
(check "comparator-hash is stable for equal? values"
       (= (comparator-hash string-comparator "abc") (comparator-hash string-comparator "abc"))
       #t)

(define lc (list-comparator real-comparator))
(check "list-comparator equality" (=? lc '(1 2 3) '(1 2 3)) #t)
(check "list-comparator ordering: shorter prefix is less" (<? lc '(1 2) '(1 2 3)) #t)
(check "list-comparator ordering: elementwise" (<? lc '(1 2 3) '(1 3)) #t)

(define vc (vector-comparator real-comparator))
(check "vector-comparator equality" (=? vc #(1 2 3) #(1 2 3)) #t)
(check "vector-comparator ordering" (<? vc #(1 2) #(1 3)) #t)

(define pc (pair-comparator real-comparator string-comparator))
(check "pair-comparator equality" (=? pc (cons 1 "a") (cons 1 "a")) #t)
(check "pair-comparator ordering by car" (<? pc (cons 1 "a") (cons 2 "a")) #t)

(define dc (make-default-comparator))
(check "default-comparator orders by type rank: bool < number" (<? dc #t 1) #t)
(check "default-comparator orders by type rank: number < string" (<? dc 1 "a") #t)
(check "default-comparator equality on lists" (=? dc '(1 2) '(1 2)) #t)

(check "comparator-check-type returns the object when it matches"
       (comparator-check-type real-comparator 5)
       5)
(check "comparator-check-type raises on mismatch"
       (guard (e (#t 'caught)) (comparator-check-type real-comparator "x"))
       'caught)

;;; make-eq-comparator / make-eqv-comparator / make-equal-comparator --
;;; the spec's typed constructor form alongside the ready-made
;;; eq-comparator/eqv-comparator/equal-comparator values.
(check "make-eq-comparator produces a comparator" (comparator? (make-eq-comparator)) #t)
(check "make-eq-comparator matches eq-comparator's equality"
       (=? (make-eq-comparator) 'a 'a) #t)
(check "make-eqv-comparator matches eqv-comparator's equality"
       (=? (make-eqv-comparator) 1.0 1.0) #t)
(check "make-equal-comparator matches equal-comparator's equality"
       (=? (make-equal-comparator) (list 1 2) (list 1 2)) #t)

;;; Standalone hash procedures + hash-bound/hash-salt + comparator-if<=> --
;;; Tier 2 gap-closing additions.

(check "boolean-hash is within hash-bound" (< (boolean-hash #t) (hash-bound)) #t)
(check "char-hash is consistent for equal chars" (= (char-hash #\a) (char-hash #\a)) #t)
(check "char-ci-hash ignores case" (= (char-ci-hash #\a) (char-ci-hash #\A)) #t)
(check "string-hash is consistent for equal strings"
       (= (string-hash "hello") (string-hash "hello")) #t)
(check "string-ci-hash ignores case"
       (= (string-ci-hash "Hello") (string-ci-hash "HELLO")) #t)
(check "symbol-hash is consistent for eq symbols" (= (symbol-hash 'a) (symbol-hash 'a)) #t)
(check "number-hash is consistent for equal numbers" (= (number-hash 42) (number-hash 42)) #t)
(check "default-hash is within hash-bound" (< (default-hash "anything") (hash-bound)) #t)
(check "hash-salt is a nonnegative exact integer within hash-bound"
       (and (exact-integer? (hash-salt)) (>= (hash-salt) 0) (< (hash-salt) (hash-bound)))
       #t)

(check "comparator-if<=>: less branch, default comparator"
       (comparator-if<=> 1 2 'less 'eq 'gtr) 'less)
(check "comparator-if<=>: equal branch, default comparator"
       (comparator-if<=> 5 5 'less 'eq 'gtr) 'eq)
(check "comparator-if<=>: greater branch, default comparator"
       (comparator-if<=> 9 5 'less 'eq 'gtr) 'gtr)
(check "comparator-if<=>: explicit comparator"
       (comparator-if<=> real-comparator 9 5 'less 'eq 'gtr) 'gtr)

;;; Summary

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
