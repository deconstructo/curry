;;; srfi_41_tests.scm — SRFI-41 (Streams). Every derived procedure is
;;; exercised at least once; a dedicated section verifies the whole
;;; point of building this on curry's native delay-force (stack safety
;;; on a long stream, not just correctness).

(import (scheme base) (srfi s41 streams))

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

;;; primitives

(check "stream-null?" (stream-null? stream-null) #t)
(define s123 (stream-cons 1 (stream-cons 2 (stream-cons 3 stream-null))))
(check "stream-car" (stream-car s123) 1)
(check "stream-cdr then stream-car" (stream-car (stream-cdr s123)) 2)
(check "stream-pair? true" (stream-pair? s123) #t)
(check "stream-pair? false" (stream-pair? stream-null) #f)

;;; constructors / destructors

(check "stream constructor macro" (stream->list (stream 1 2 3)) (list 1 2 3))
(check "list->stream" (stream->list (list->stream (list 4 5 6))) (list 4 5 6))
(check "stream->list with count" (stream->list 2 (stream-from 0)) (list 0 1))
(check "stream-ref" (stream-ref (stream 10 20 30) 1) 20)
(check "stream-length" (stream-length (stream 1 2 3)) 3)

;;; transformations

(check "stream-take" (stream->list (stream-take 3 (stream-from 10))) (list 10 11 12))
(check "stream-map" (stream->list (stream-map + (stream 1 2 3) (stream 10 20 30))) (list 11 22 33))
(check "stream-filter" (stream->list (stream-filter even? (stream 1 2 3 4 5 6))) (list 2 4 6))
(check "stream-reverse" (stream->list (stream-reverse (stream 1 2 3))) (list 3 2 1))
(check "stream-append" (stream->list (stream-append (stream 1 2) (stream 3 4))) (list 1 2 3 4))
(check "stream-concat" (stream->list (stream-concat (stream (stream 1 2) (stream 3 4)))) (list 1 2 3 4))
(check "stream-drop" (stream->list (stream-drop 2 (stream 1 2 3 4))) (list 3 4))
(check "stream-drop-while" (stream->list (stream-drop-while even? (stream 2 4 5 6))) (list 5 6))
(check "stream-take-while" (stream->list (stream-take-while even? (stream 2 4 5 6))) (list 2 4))
(check "stream-scan" (stream->list (stream-scan + 0 (stream 1 2 3))) (list 0 1 3 6))
(check "stream-zip" (stream->list (stream-zip (stream 1 2) (stream 'a 'b))) (list (list 1 'a) (list 2 'b)))

;;; folding / iteration

(check "stream-fold" (stream-fold + 0 (stream 1 2 3 4)) 10)
(define %for-each-acc '())
(stream-for-each (lambda (x) (set! %for-each-acc (cons x %for-each-acc))) (stream 1 2 3))
(check "stream-for-each" (reverse %for-each-acc) (list 1 2 3))
(check "stream-iterate" (stream->list (stream-take 4 (stream-iterate (lambda (x) (* x 2)) 1))) (list 1 2 4 8))

;;; generators

(check "stream-range" (stream->list (stream-range 0 5)) (list 0 1 2 3 4))
(check "stream-range with step" (stream->list (stream-range 0 10 2)) (list 0 2 4 6 8))
(check "stream-constant" (stream->list (stream-take 5 (stream-constant 1 2))) (list 1 2 1 2 1))
(check "stream-unfold"
       (stream->list (stream-unfold (lambda (x) (* x x)) (lambda (x) (< x 4)) (lambda (x) (+ x 1)) 0))
       (list 0 1 4 9))

;; stream-unfolds' generator returns (values next-seed result ...) per
;; output stream, where each result is a one-element list to emit a
;; value, #f to skip this round (NOT terminate -- an easy mistake, since
;; the generator keeps getting called with the same seed forever if it
;; never returns '() instead), or '() to end that output stream.
(define (%unfolds-gen seed) (if (> seed 5) (values seed '()) (values (+ seed 1) (list seed))))
(call-with-values
 (lambda () (stream-unfolds %unfolds-gen 0))
 (lambda (s) (check "stream-unfolds" (stream->list s) (list 0 1 2 3 4 5))))

(define %port (open-input-string "abc"))
(check "port->stream" (stream->list (port->stream %port)) (list #\a #\b #\c))

;;; sugar

(check "stream-let"
       (stream->list (stream-let %loop ((n 0)) (if (= n 3) stream-null (stream-cons n (%loop (+ n 1))))))
       (list 0 1 2))
(define-stream (%nats n) (stream-cons n (%nats (+ n 1))))
(check "define-stream" (stream->list (stream-take 3 (%nats 5))) (list 5 6 7))
(check "stream-of" (stream->list (stream-of (* x x) (x in (stream 1 2 3)))) (list 1 4 9))
(check "stream-of with guard"
       (stream->list (stream-of x (x in (stream 1 2 3 4 5 6)) (even? x)))
       (list 2 4 6))

;;; stream-match

(check "stream-match: empty vs non-empty wildcard"
       (stream-match (stream 1 2 3) (() 'empty) ((_ . _) 'nonempty))
       'nonempty)
(check "stream-match: empty case"
       (stream-match stream-null (() 'empty) ((_ . _) 'nonempty))
       'empty)
(check "stream-match: bindings, rest captured as a stream"
       (stream-match (stream 1 2 3) ((a b . rest) (list a b (stream->list rest))))
       (list 1 2 (list 3)))

;;; Stack safety: the entire reason this port uses curry's native
;;; delay-force for the cdr instead of plain delay is that a long
;;; stream must not grow the VM's call stack per element. 100,000
;;; elements is comfortably past the 256-frame guard a naive
;;; non-tail-safe implementation would blow through.

(check "stack-safe: stream-length over 100,000 elements" (stream-length (stream-range 0 100000)) 100000)
(check "stack-safe: stream-fold over 1,000 elements" (stream-fold + 0 (stream-range 1 1001)) 500500)
(check "stack-safe: stream-filter over 50,000 elements"
       (stream-length (stream-filter even? (stream-range 0 50000)))
       25000)

;;; Summary

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
