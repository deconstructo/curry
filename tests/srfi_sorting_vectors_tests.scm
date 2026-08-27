;;; (srfi s132 sorting) and (srfi s133 vectors)

(import (srfi s132 sorting) (srfi s133 vectors))

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

;;; s132

(check "list-sort sorts ascending" (list-sort < '(3 1 2)) '(1 2 3))
(check "list-sorted? true for sorted input" (list-sorted? < '(1 2 3)) #t)
(check "list-sorted? false for unsorted input" (list-sorted? < '(1 3 2)) #f)
(check "vector-sort returns a new sorted vector" (vector-sort < #(3 1 2)) #(1 2 3))

(define v (vector 3 1 2))
(vector-sort! < v)
(check "vector-sort! sorts in place" v #(1 2 3))

(check "list-merge merges two sorted lists" (list-merge < '(1 3 5) '(2 4 6)) '(1 2 3 4 5 6))

;;; s133

(check "vector-empty? true for #()" (vector-empty? #()) #t)
(check "vector= true for equal vectors" (vector= = #(1 2 3) #(1 2 3)) #t)
(check "vector= false for differing vectors" (vector= = #(1 2 3) #(1 2 4)) #f)

(define v2 (vector 1 2 3 4))
(vector-swap! v2 0 3)
(check "vector-swap! swaps two elements" v2 #(4 2 3 1))

(check "vector-index finds the first matching index" (vector-index even? #(1 3 4 5)) 2)
(check "vector-count counts matching elements" (vector-count even? #(1 2 3 4)) 2)
(check "vector-any true when a match exists" (vector-any even? #(1 3 5)) #f)
(check "vector-every true when all match" (vector-every even? #(2 4 6)) #t)
(check "vector-fold accumulates left to right" (vector-fold + 0 #(1 2 3 4)) 10)
(check "vector-binary-search finds a present key" (vector-binary-search #(1 3 5 7 9) 5 <) 2)
(check "vector-binary-search returns #f for an absent key" (vector-binary-search #(1 3 5 7 9) 4 <) #f)
(check "vector-concatenate joins vectors" (vector-concatenate (list #(1 2) #(3 4))) #(1 2 3 4))
(check "vector-unfold builds via seed-free generator" (vector-unfold (lambda (i) i) 5) #(0 1 2 3 4))

;;; SRFI-133 Tier 2 gap-closing additions

(check "vector-reverse-copy" (vector-reverse-copy #(1 2 3 4)) #(4 3 2 1))
(check "vector-reverse-copy with a range" (vector-reverse-copy #(1 2 3 4 5) 1 4) #(4 3 2))

(check "vector-append-subvectors"
       (vector-append-subvectors #(1 2 3) 0 2 #(10 20 30) 1 3)
       #(1 2 20 30))

(let ((v (vector 1 2 3 4)))
  (vector-map! (lambda (x) (* x x)) v)
  (check "vector-map! mutates in place" v #(1 4 9 16)))

(check "vector-cumulate accumulates a running total"
       (vector-cumulate + 0 #(1 2 3 4))
       #(1 3 6 10))

(check "vector-skip finds first non-matching index" (vector-skip even? #(2 4 6 7 8)) 3)
(check "vector-skip-right finds last non-matching index" (vector-skip-right even? #(2 7 4 6 8)) 1)

(call-with-values (lambda () (vector-partition even? #(1 2 3 4 5 6)))
  (lambda (result count)
    (check "vector-partition groups matches first, stably" result #(2 4 6 1 3 5))
    (check "vector-partition returns the match count" count 3)))

(check "reverse-vector->list" (reverse-vector->list #(1 2 3)) (list 3 2 1))
(check "reverse-list->vector" (reverse-list->vector (list 1 2 3)) #(3 2 1))

(let ((v (make-vector 5 0)))
  (vector-unfold! (lambda (i) i) v 1 4)
  (check "vector-unfold! fills a range in place" v #(0 1 2 3 0)))

(let ((v (make-vector 5 0)))
  (vector-unfold-right! (lambda (i) (* i 10)) v 1 4)
  (check "vector-unfold-right! fills a range in place, right to left" v #(0 10 20 30 0)))

;;; Summary

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
