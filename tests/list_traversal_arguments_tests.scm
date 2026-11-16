;;; map, for-each and their relatives reject an argument that is not a list
;;; (#408), and vector-map / vector-for-each stop at the shortest vector (#411).
;;;
;;; (map car 5) was (), (map f '(1 2 . 3)) called f twice and returned (1 2),
;;; and (vector-map + #(1 2 3) #(1)) read past the end of the shorter vector.

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
(define (id x) x)
(define circ (let ((l (list 1 2 3))) (set-cdr! (cddr l) l) l))
(define long (let loop ((i 99) (acc '())) (if (< i 0) acc (loop (- i 1) (cons i acc)))))   ; above the parallel threshold

;;; ---- map ----
(check "map over a list" (map id '(1 2 3)) '(1 2 3))
(check "map over the empty list" (map id '()) '())
(check "map over a long list (parallel path)" (map (lambda (x) (* 2 x)) long) (map/seq (lambda (x) (* 2 x)) long))
(check "map stops at the shortest list" (map + '(1 2 3) '(10 20)) '(11 22))
(check "map with a circular list next to a finite one" (map + '(1 2 3 4) circ) '(2 4 6 5))
(check "map: a number" (raises? (lambda () (map car 5))) #t)
(check "map: a vector" (raises? (lambda () (map id (vector 1 2)))) #t)
(check "map: a string" (raises? (lambda () (map id "abc"))) #t)
(check "map: an improper list" (raises? (lambda () (map id '(1 2 . 3)))) #t)
(check "map: a circular list alone" (raises? (lambda () (map id circ))) #t)
(check "map: two circular lists" (raises? (lambda () (map + circ circ))) #t)
(check "map: a non-list as the second list" (raises? (lambda () (map + '(1 2) 5))) #t)
(check "map: a non-list next to an empty list" (raises? (lambda () (map + '() 5))) #t)
(check "map: an improper list that ends first" (raises? (lambda () (map + '(1 . 2) '(1 2 3)))) #t)
(check "map: an improper list that is longer is not reached" (map + '(1 2) '(1 2 3 . 4)) '(2 4))
(check "map: nothing is called for a non-list"
       (let ((n 0)) (guard (e (#t n)) (map (lambda (x) (set! n (+ n 1))) 5))) 0)
(check "map/seq: a number" (raises? (lambda () (map/seq car 5))) #t)
(check "map/seq: an improper list" (raises? (lambda () (map/seq id '(1 . 2)))) #t)
(check "map through apply" (raises? (lambda () (apply map (list id 5)))) #t)

;;; ---- for-each ----
(check "for-each over a list"
       (let ((acc '())) (for-each (lambda (x) (set! acc (cons x acc))) '(1 2 3)) acc) '(3 2 1))
(check "for-each stops at the shortest list"
       (let ((acc '())) (for-each (lambda (x y) (set! acc (cons (+ x y) acc))) '(1 2 3) '(10 20)) acc) '(22 11))
(check "for-each over the empty list" (raises? (lambda () (for-each id '()))) #f)
(check "for-each: a number" (raises? (lambda () (for-each id 5))) #t)
(check "for-each: an improper list" (raises? (lambda () (for-each id '(1 2 . 3)))) #t)
(check "for-each: a circular list alone" (raises? (lambda () (for-each id circ))) #t)
(check "for-each: a non-list as the second list" (raises? (lambda () (for-each + '(1) 5))) #t)
(check "for-each/par over a long list"
       (let ((v (make-vector 100 0))) (for-each/par (lambda (i) (vector-set! v i i)) long) (vector-ref v 99)) 99)
(check "for-each/par: a number" (raises? (lambda () (for-each/par id 5))) #t)
(check "for-each/par: an improper list" (raises? (lambda () (for-each/par id '(1 . 2)))) #t)
(check "for-each/par: a circular list" (raises? (lambda () (for-each/par id circ))) #t)
(check "for-each/par: a non-list as the second list" (raises? (lambda () (for-each/par + '(1) 5))) #t)

;;; ---- filter, fold-left, fold-right, reduce ----
(check "filter" (filter odd? '(1 2 3 4 5)) '(1 3 5))
(check "filter: a number" (raises? (lambda () (filter odd? 5))) #t)
(check "filter: an improper list" (raises? (lambda () (filter odd? '(1 . 2)))) #t)
(check "filter: a circular list" (raises? (lambda () (filter odd? circ))) #t)
(check "fold-left" (fold-left + 0 '(1 2 3)) 6)
(check "fold-left over the empty list" (fold-left + 0 '()) 0)
(check "fold-left: a number" (raises? (lambda () (fold-left + 0 5))) #t)
(check "fold-left: a circular list" (raises? (lambda () (fold-left + 0 circ))) #t)
(check "fold-right" (fold-right cons '() '(1 2 3)) '(1 2 3))
(check "fold-right: a number" (raises? (lambda () (fold-right cons '() 5))) #t)
(check "fold-right: an improper list" (raises? (lambda () (fold-right cons '() '(1 . 2)))) #t)
(check "fold-right: a circular list" (raises? (lambda () (fold-right cons '() circ))) #t)
(check "reduce" (reduce + 0 '(1 2 3)) 6)
(check "reduce over the empty list" (reduce + 0 '()) 0)
(check "reduce over a long list" (reduce + 0 long) 4950)
(check "reduce: a number" (raises? (lambda () (reduce + 0 5))) #t)
(check "reduce: an improper list" (raises? (lambda () (reduce + 0 '(1 2 . 3)))) #t)
(check "reduce: a circular list" (raises? (lambda () (reduce + 0 circ))) #t)
(check "reduce/seq: a number" (raises? (lambda () (reduce/seq + 0 5))) #t)

;;; ---- vector-map and vector-for-each stop at the shortest vector (#411) ----
(check "vector-map, equal lengths" (vector-map + (vector 1 2) (vector 10 20)) (vector 11 22))
(check "vector-map, second shorter" (vector-map + (vector 1 2 3 4 5 6 7 8) (vector 1)) (vector 2))
(check "vector-map, first shorter" (vector-map + (vector 1) (vector 1 2 3 4 5 6 7 8)) (vector 2))
(check "vector-map, an empty vector" (vector-map + (vector 1 2 3) (vector)) (vector))
(check "vector-map, three vectors" (vector-map + (vector 1 2 3) (vector 1 2) (vector 1 2 3 4)) (vector 3 6))
(check "vector-for-each, second shorter"
       (let ((acc '())) (vector-for-each (lambda (a b) (set! acc (cons b acc))) (make-vector 40 1) (vector 7 8)) acc)
       '(8 7))
(check "vector-for-each, an empty vector"
       (let ((n 0)) (vector-for-each (lambda (a b) (set! n (+ n 1))) (vector 1 2) (vector)) n) 0)
(check "vector-map: not a vector" (raises? (lambda () (vector-map id '(1 2)))) #t)

;;; ---- from the review of #408 ----
(define (message thunk) (guard (e ((error-object? e) (error-object-message e)) (#t 'other)) (thunk) 'no-error))
(define (ends-with? s suffix)
  (and (string? s) (>= (string-length s) (string-length suffix))
       (string=? (substring s (- (string-length s) (string-length suffix)) (string-length s)) suffix)))
(check "the error is an error object naming map"
       (ends-with? (message (lambda () (map car 5))) "map: not a list") #t)
(check "the error names for-each"
       (ends-with? (message (lambda () (for-each car '(1 . 2)))) "for-each: not a proper list") #t)
(define lasso (let ((l (list 1 2 3 4 5))) (set-cdr! (cddddr l) (cddr l)) l))   ; 1 2 [3 4 5 3 4 5 ...]
(check "map: a cycle after a prefix" (raises? (lambda () (map id lasso))) #t)
(check "map: a cycle after a prefix, next to a finite list"
       (map + lasso '(1 2 3 4 5 6 7 8 9)) '(2 4 6 8 10 9 11 13 12))
(check "for-each: a cycle after a prefix" (raises? (lambda () (for-each id lasso))) #t)
(define long-improper (let ((l (list-copy long))) (set-cdr! (list-tail l 49) 'end) l))   ; 50 pairs, parallel path
(check "map: a long improper list" (raises? (lambda () (map id long-improper))) #t)
(check "reduce: a long improper list" (raises? (lambda () (reduce + 0 long-improper))) #t)
(check "for-each/par: a long improper list" (raises? (lambda () (for-each/par id long-improper))) #t)
(check "fold-left: an improper list" (raises? (lambda () (fold-left + 0 '(1 2 . 3)))) #t)
(check "map: a list made improper by the procedure"
       (raises? (lambda () (let ((l (list 1 2 3 4))) (map (lambda (x) (if (= x 2) (set-cdr! (cdr l) 99)) x) l)))) #t)
(check "for-each: nothing is called for a non-list"
       (let ((n 0)) (guard (e (#t n)) (for-each (lambda (x) (set! n (+ n 1))) 5))) 0)
(check "fold-right: nothing is called for an improper list"
       (let ((n 0)) (guard (e (#t n)) (fold-right (lambda (x a) (set! n (+ n 1))) 0 '(1 2 . 3)))) 0)
(check "map with three lists" (map + '(1 2 3) '(10 20 30) '(100 200)) '(111 222))
(check "vector-for-each, first shorter"
       (let ((acc '())) (vector-for-each (lambda (a b) (set! acc (cons b acc))) (vector 7 8) (make-vector 40 1)) acc)
       '(1 1))
(define-library (test traversal-in-library)
  (import (scheme base))
  (export in-library)
  (begin (define (in-library x) (guard (e (#t 'raised)) (map car x)))))
(import (test traversal-in-library))
(check "inside a define-library body" (in-library 5) 'raised)

;;; ---- fold-right on a long list (#418: a segmentation fault) ----
(define million (let loop ((i 0) (acc '())) (if (= i 1000000) acc (loop (+ i 1) (cons i acc)))))
(check "fold-right over a million elements" (fold-right + 0 million) 499999500000)
(check "fold-right order" (fold-right list 0 '(1 2 3)) '(1 (2 (3 0))))
(check "fold-right over the empty list" (fold-right + 7 '()) 7)
(check "fold-right does not change its argument"
       (let ((l (list 1 2 3))) (fold-right cons '() l) l) '(1 2 3))

;;; ---- string-map (#419: it was not defined) ----
(check "string-map" (string-map char-upcase "abc") "ABC")
(check "string-map over the empty string" (string-map char-upcase "") "")
(check "string-map with two strings stops at the shorter"
       (string-map (lambda (a b) (if (char<? a b) a b)) "adcz" "bbb") "abb")
(check "string-map, first string shorter"
       (string-map (lambda (a b) b) "ab" "wxyz") "wx")
(check "string-map beyond ASCII" (string-map char-upcase "straße λ") "STRAßE Λ")
(check "string-map returns a new string"
       (let ((s (string-copy "abc"))) (eq? s (string-map (lambda (c) c) s))) #f)
(check "string-map: not a string" (raises? (lambda () (string-map char-upcase '(#\a)))) #t)
(check "string-map: second argument not a string" (raises? (lambda () (string-map (lambda (a b) a) "ab" 5))) #t)
(check "string-map: the procedure returns a non-character"
       (raises? (lambda () (string-map (lambda (c) 1) "abc"))) #t)

(newline)
(display pass) (display " passed, ") (display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
