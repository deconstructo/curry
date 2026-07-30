;;; (srfi s158 generators-and-accumulators)

(import (srfi s158 generators-and-accumulators))

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

(check "list->generator round-trips through generator->list"
       (generator->list (list->generator '(1 2 3)))
       '(1 2 3))
(check "vector->generator" (generator->list (vector->generator #(1 2 3))) '(1 2 3))
(check "make-range-generator" (generator->list (make-range-generator 0 5)) '(0 1 2 3 4))
(check "make-iota-generator with start/step" (generator->list (make-iota-generator 5 10 2)) '(10 12 14 16 18))
(check "gtake limits an infinite generator" (generator->list (gtake (make-range-generator 0) 3)) '(0 1 2))
(check "gdrop skips the first n values"
       (let ((g (make-range-generator 0 6))) (gdrop g 2) (generator->list g))
       '(2 3 4 5))
(check "gappend concatenates generators"
       (generator->list (gappend (list->generator '(1 2)) (list->generator '(3 4))))
       '(1 2 3 4))
(check "gmap combines multiple generators"
       (generator->list (gmap + (list->generator '(1 2 3)) (list->generator '(10 20 30))))
       '(11 22 33))
(check "gfilter keeps matching values" (generator->list (gfilter even? (list->generator '(1 2 3 4 5)))) '(2 4))
(check "gzip pairs up values"
       (generator->list (gzip (list->generator '(1 2)) (list->generator '(a b))))
       '((1 a) (2 b)))
(check "generator-fold accumulates" (generator-fold + 0 (list->generator '(1 2 3 4))) 10)
(check "generator-count" (generator-count even? (list->generator '(1 2 3 4))) 2)
(check "generator-any" (generator-any even? (list->generator '(1 3 5 6))) #t)
(check "generator-every" (generator-every even? (list->generator '(2 4 6))) #t)
(check "generator-find" (generator-find even? (list->generator '(1 3 4 5))) 4)

;;; make-coroutine-generator — real-thread suspend/resume via (curry sync)

(define cg (make-coroutine-generator
            (lambda (yield) (yield 1) (yield 2) (yield 3))))
(check "coroutine generator: first value" (cg) 1)
(check "coroutine generator: second value" (cg) 2)
(check "coroutine generator: third value" (cg) 3)
(check "coroutine generator: exhausted after producer returns" (eof-object? (cg)) #t)
(check "coroutine generator: stays exhausted" (eof-object? (cg)) #t)

;;; accumulators

(define acc (list-accumulator))
(acc 1) (acc 2) (acc 3)
(check "list-accumulator preserves insertion order" (acc (eof-object)) '(1 2 3))

(define sacc (sum-accumulator))
(sacc 1) (sacc 2) (sacc 3)
(check "sum-accumulator" (sacc (eof-object)) 6)

(define cnt (count-accumulator))
(cnt 'a) (cnt 'b)
(check "count-accumulator" (cnt (eof-object)) 2)

;;; Summary

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
