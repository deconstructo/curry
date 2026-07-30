;;; (srfi s113 sets-and-bags)

(import (srfi s128 comparators) (srfi s113 sets-and-bags))

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

;;; sets

(define s1 (set string-comparator "a" "b" "c"))
(define s2 (set string-comparator "b" "c" "d"))

(check "set-size" (set-size s1) 3)
; SRFI-113/SRFI-1 fold convention is (proc elem acc), not curry's native
; set-fold's (proc acc elem) — (fold cons '() '(1 2 3)) => (3 2 1)
(check "set-fold calls proc as (elem acc), matching SRFI-1 fold order"
       (set-fold cons '() (set number-comparator 1 2 3))
       '(3 2 1))
(check "set-contains? for a present element" (set-contains? s1 "a") #t)
(check "set-contains? for an absent element" (set-contains? s1 "z") #f)
(check "set-union size" (set-size (set-union s1 s2)) 4)
(check "set-intersection size" (set-size (set-intersection s1 s2)) 2)
(check "set-difference size" (set-size (set-difference s1 s2)) 1)
(check "set=? on a copy" (set=? s1 (set-copy s1)) #t)
(check "set<=? for a subset" (set<=? (set string-comparator "a") s1) #t)
(check "set<? is strict" (set<? (set string-comparator "a") s1) #t)
(check "set<? false for equal sets" (set<? s1 s1) #f)
(check "set-disjoint? false when sets overlap" (set-disjoint? s1 s2) #f)

(define s3 (set-adjoin s1 "z"))
(check "set-adjoin does not mutate the original" (set-size s1) 3)
(check "set-adjoin grows the new set" (set-size s3) 4)

(define s4 (list->set string-comparator '("x" "y")))
(check "list->set" (set-size s4) 2)

;;; bags

(define b1 (bag string-comparator "a" "a" "b"))
(check "bag-size counts duplicates" (bag-size b1) 3)
(check "bag-unique-size counts distinct elements" (bag-unique-size b1) 2)
(check "bag-element-count" (bag-element-count b1 "a") 2)

(bag-adjoin! b1 "a")
(check "bag-adjoin! increments the count" (bag-element-count b1 "a") 3)
(bag-delete! b1 "a")
(check "bag-delete! decrements the count" (bag-element-count b1 "a") 2)

(define b2 (bag string-comparator "a" "c"))
(check "bag-union takes the max count per element" (bag-size (bag-union b1 b2)) 4)
(check "bag-sum adds counts per element" (bag-size (bag-sum b1 b2)) 5)
(check "bag=? on a copy" (bag=? b1 (bag-copy b1)) #t)

;;; Summary

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
