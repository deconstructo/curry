(define-library (surfage s132 sorting)
  (import (scheme base))
  (export
    list-sorted? vector-sorted?
    list-sort list-stable-sort list-sort! list-stable-sort!
    vector-sort vector-sort! vector-stable-sort vector-stable-sort!
    list-merge list-merge! vector-merge vector-merge!)
  (begin

    ; Every sort here is a stable merge sort — SRFI-132 permits list-sort/
    ; vector-sort to be unstable, but there's no benefit to a second,
    ; less-predictable algorithm, so the "plain" and "stable" names are
    ; aliases of the same implementation.

    (define (list-sorted? less? lst)
      (or (null? lst) (null? (cdr lst))
          (and (not (less? (cadr lst) (car lst)))
               (list-sorted? less? (cdr lst)))))

    (define (vector-sorted? less? v . range)
      (let ((start (if (pair? range) (car range) 0))
            (end (if (and (pair? range) (pair? (cdr range))) (cadr range) (vector-length v))))
        (let loop ((i (+ start 1)))
          (or (>= i end) (and (not (less? (vector-ref v i) (vector-ref v (- i 1)))) (loop (+ i 1)))))))

    (define (list-merge less? a b)
      (cond ((null? a) b)
            ((null? b) a)
            ((less? (car b) (car a)) (cons (car b) (list-merge less? a (cdr b))))
            (else (cons (car a) (list-merge less? (cdr a) b)))))

    (define list-merge! list-merge)

    (define (%list-split lst)
      (let loop ((slow lst) (fast lst) (acc '()))
        (if (or (null? fast) (null? (cdr fast)))
            (values (reverse acc) slow)
            (loop (cdr slow) (cddr fast) (cons (car slow) acc)))))

    (define (list-stable-sort less? lst)
      (if (or (null? lst) (null? (cdr lst)))
          lst
          (call-with-values (lambda () (%list-split lst))
            (lambda (a b) (list-merge less? (list-stable-sort less? a) (list-stable-sort less? b))))))

    (define list-sort list-stable-sort)
    (define list-sort! list-stable-sort)
    (define list-stable-sort! list-stable-sort)

    (define (vector-stable-sort less? v . range)
      (let* ((start (if (pair? range) (car range) 0))
             (end (if (and (pair? range) (pair? (cdr range))) (cadr range) (vector-length v)))
             (sorted (list-stable-sort less? (vector->list v start end)))
             (out (vector-copy v)))
        (let loop ((i start) (l sorted))
          (if (pair? l) (begin (vector-set! out i (car l)) (loop (+ i 1) (cdr l)))))
        out))

    (define (vector-stable-sort! less? v . range)
      (let* ((start (if (pair? range) (car range) 0))
             (end (if (and (pair? range) (pair? (cdr range))) (cadr range) (vector-length v)))
             (sorted (list-stable-sort less? (vector->list v start end))))
        (let loop ((i start) (l sorted))
          (if (pair? l) (begin (vector-set! v i (car l)) (loop (+ i 1) (cdr l)))))
        v))

    (define vector-sort vector-stable-sort)
    (define vector-sort! vector-stable-sort!)

    (define (vector-merge less? a b)
      (list->vector (list-merge less? (vector->list a) (vector->list b))))

    (define (vector-merge! less? to a b)
      (let loop ((i 0) (l (list-merge less? (vector->list a) (vector->list b))))
        (if (pair? l) (begin (vector-set! to i (car l)) (loop (+ i 1) (cdr l)))))
      to)))
