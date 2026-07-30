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

;;; Summary

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
