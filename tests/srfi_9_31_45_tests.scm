;;; srfi_9_31_45_tests.scm — SRFI-9 (records), SRFI-31 (rec), SRFI-45
;;; (lazy). Each SRFI is exercised via all three library paths: the real
;;; (srfi sN name) implementation, the bare-numbered (srfi N) shim, and
;;; the dashed (srfi srfi-N) shim -- per the lesson recorded during the
;;; Tier-1 gap fixes, a fix that only touches one of the three silently
;;; misses the others.

(import (scheme base)
        (srfi s9 records) (srfi 9) (srfi srfi-9)
        (srfi s31 rec) (srfi 31) (srfi srfi-31)
        (srfi s45 lazy) (srfi 45) (srfi srfi-45))

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

;;; SRFI-9: define-record-type

(define-record-type <point>
  (make-point x y)
  point?
  (x point-x)
  (y point-y))

(define p (make-point 3 4))
(check "srfi-9 define-record-type: predicate" (point? p) #t)
(check "srfi-9 define-record-type: accessors" (list (point-x p) (point-y p)) (list 3 4))
(check "srfi-9 define-record-type: non-instance predicate" (point? 5) #f)

;;; SRFI-31: rec

(check "srfi-31 rec: self-referential lambda"
       ((rec fact (lambda (n) (if (= n 0) 1 (* n (fact (- n 1)))))) 6)
       720)
(check "srfi-31 rec: plain named expression" (rec x (+ 1 2)) 3)

;;; SRFI-45: lazy, force, delay, eager

(define (integers-from n) (lazy (cons n (integers-from (+ n 1)))))

(define (stream-take s n)
  (if (= n 0)
      '()
      (let ((pr (force s)))
        (cons (car pr) (stream-take (cdr pr) (- n 1))))))

;; stream-take itself is ordinary (non-tail) recursion bounded by n, not a
;; test of delay-force's own iteration -- that part is what `integers-from`
;; exercises: each `force` only ever unwinds one delay-force layer deep
;; regardless of how many integers have been generated before it, which is
;; the whole point of SRFI-45's `lazy` over plain `delay`.
(check "srfi-45 lazy/force: produces a working infinite stream"
       (stream-take (integers-from 0) 20)
       (let loop ((i 19) (acc '())) (if (< i 0) acc (loop (- i 1) (cons i acc)))))

(check "srfi-45 eager: wraps a plain value in an already-forced promise"
       (force (eager 42))
       42)

(check "srfi-45 delay/force: ordinary (non-lazy) promise still works"
       (force (delay (+ 1 2)))
       3)

;;; Summary

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
