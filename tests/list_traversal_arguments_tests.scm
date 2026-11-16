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

(newline)
(display pass) (display " passed, ") (display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
