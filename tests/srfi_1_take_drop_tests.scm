;;; (srfi 1) take, drop, take-right, drop-right, split-at and their ! forms
;;; raise for a count the list cannot satisfy (#409).
;;;
;;; (take '(1 2) 5) was (1 2) and (drop '(1 2) 5) was (); a negative or
;;; fractional count took or dropped the whole list.

(import (scheme base) (scheme write) (srfi 1))

(define pass 0)
(define fail 0)
(define (check label got expected)
  (if (equal? got expected)
      (set! pass (+ pass 1))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write got)
             (display " expected ") (write expected) (newline)
             (set! fail (+ fail 1)))))
(define (raises? thunk) (guard (e (#t #t)) (thunk) #f))
(define (message thunk) (guard (e ((error-object? e) (error-object-message e)) (#t 'other)) (thunk) 'no-error))
(define l5 '(1 2 3 4 5))

;;; ---- valid counts ----
(check "take 0" (take l5 0) '())
(check "take 2" (take l5 2) '(1 2))
(check "take all" (take l5 5) l5)
(check "take 0 of ()" (take '() 0) '())
(check "drop 0" (drop l5 0) l5)
(check "drop 2" (drop l5 2) '(3 4 5))
(check "drop all" (drop l5 5) '())
(check "drop shares the tail" (eq? (drop l5 2) (cddr l5)) #t)
(check "take-right 2" (take-right l5 2) '(4 5))
(check "take-right 0" (take-right l5 0) '())
(check "take-right all" (take-right l5 5) l5)
(check "drop-right 2" (drop-right l5 2) '(1 2 3))
(check "drop-right 0" (drop-right l5 0) l5)
(check "drop-right all" (drop-right l5 5) '())
(check "split-at 2" (call-with-values (lambda () (split-at l5 2)) list) '((1 2) (3 4 5)))
(check "split-at 0" (call-with-values (lambda () (split-at l5 0)) list) '(() (1 2 3 4 5)))
(check "split-at all" (call-with-values (lambda () (split-at l5 5)) list) '((1 2 3 4 5) ()))
(check "take! 2" (take! (list 1 2 3) 2) '(1 2))
(check "drop-right! 1" (drop-right! (list 1 2 3) 1) '(1 2))
(check "split-at! 1" (call-with-values (lambda () (split-at! (list 1 2 3) 1)) list) '((1) (2 3)))
(check "a long list" (length (take (iota 100000) 99999)) 99999)

;;; ---- dotted lists (allowed by the SRFI) ----
(check "take on a dotted list" (take '(1 2 3 . d) 2) '(1 2))
(check "take every pair of a dotted list" (take '(1 2 3 . d) 3) '(1 2 3))
(check "drop on a dotted list" (drop '(1 2 3 . d) 2) '(3 . d))
(check "drop every pair of a dotted list" (drop '(1 2 3 . d) 3) 'd)
(check "take-right on a dotted list" (take-right '(1 2 3 . d) 2) '(2 3 . d))
(check "take-right 0 on a dotted list" (take-right '(1 2 3 . d) 0) 'd)
(check "drop-right on a dotted list" (drop-right '(1 2 3 . d) 2) '(1))
(check "drop-right 0 on a dotted list" (drop-right '(1 2 3 . d) 0) '(1 2 3))
(check "drop 0 of a non-list" (drop 'x 0) 'x)

;;; ---- a count past the end ----
(check "take past the end" (raises? (lambda () (take '(1 2) 5))) #t)
(check "take 1 of ()" (raises? (lambda () (take '() 1))) #t)
(check "drop past the end" (raises? (lambda () (drop '(1 2) 5))) #t)
(check "drop 1 of ()" (raises? (lambda () (drop '() 1))) #t)
(check "take-right past the end" (raises? (lambda () (take-right '(1 2) 3))) #t)
(check "drop-right past the end" (raises? (lambda () (drop-right '(1 2) 3))) #t)
(check "split-at past the end" (raises? (lambda () (split-at '(1 2) 3))) #t)
(check "take! past the end" (raises? (lambda () (take! (list 1 2) 3))) #t)
(check "drop-right! past the end" (raises? (lambda () (drop-right! (list 1 2) 3))) #t)
(check "take past the pairs of a dotted list" (raises? (lambda () (take '(1 2 . d) 3))) #t)
(check "take from a non-list" (raises? (lambda () (take 5 1))) #t)
(check "the message names the procedure"
       (message (lambda () (take '(1 2) 5))) "take: list has fewer elements than the count")
(check "take-right reports under its own name"
       (message (lambda () (take-right '(1 2) 5))) "take-right: list has fewer elements than the count")

;;; ---- a count that is not an exact non-negative integer ----
(check "take, negative" (raises? (lambda () (take l5 -1))) #t)
(check "drop, negative" (raises? (lambda () (drop l5 -1))) #t)
(check "take-right, negative" (raises? (lambda () (take-right l5 -1))) #t)
(check "drop-right, negative" (raises? (lambda () (drop-right l5 -1))) #t)
(check "split-at, negative" (raises? (lambda () (split-at l5 -1))) #t)
(check "take, fractional" (raises? (lambda () (take l5 1/2))) #t)
(check "take, flonum" (raises? (lambda () (take l5 2.0))) #t)
(check "take, NaN" (raises? (lambda () (take l5 +nan.0))) #t)
(check "drop, infinity" (raises? (lambda () (drop l5 +inf.0))) #t)
(check "take, not a number" (raises? (lambda () (take l5 'two))) #t)
(check "the count message"
       (message (lambda () (drop l5 -1))) "drop: count must be an exact non-negative integer")

(newline)
(display pass) (display " passed, ") (display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
