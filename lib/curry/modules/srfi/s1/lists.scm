(define-library (srfi s1 lists)
  (import (scheme base))
  (export
    ; SRFI-1 procedures already in curry's global env
    cons car cdr caaar cadar caar cdar
    list list* make-list length append reverse
    list-tail list-ref last-pair
    map for-each filter fold-left fold-right
    ; Additional SRFI-1 names
    fold fold-right
    iota any every
    remove delete
    append-map filter-map flat-map
    take drop take-while drop-while
    count partition
    first second third fourth fifth)
  (begin

    (define (iota count . rest)
      (let ((start (if (null? rest) 0 (car rest)))
            (step  (if (or (null? rest) (null? (cdr rest))) 1 (cadr rest))))
        (let loop ((i 0) (acc '()))
          (if (= i count)
              (reverse acc)
              (loop (+ i 1) (cons (+ start (* i step)) acc))))))

    (define (any pred lst)
      (cond ((null? lst) #f)
            ((pred (car lst)) #t)
            (else (any pred (cdr lst)))))

    (define (every pred lst)
      (cond ((null? lst) #t)
            ((not (pred (car lst))) #f)
            (else (every pred (cdr lst)))))

    (define (remove pred lst)
      (filter (lambda (x) (not (pred x))) lst))

    (define (delete x lst . rest)
      (let ((=? (if (null? rest) equal? (car rest))))
        (filter (lambda (y) (not (=? x y))) lst)))

    (define (fold kons knil lst)
      (fold-left kons knil lst))

    (define (append-map f lst)
      (apply append (map f lst)))

    (define (filter-map f lst)
      (let loop ((l lst) (acc '()))
        (if (null? l)
            (reverse acc)
            (let ((r (f (car l))))
              (loop (cdr l) (if r (cons r acc) acc))))))

    (define (flat-map f lst)
      (append-map f lst))

    (define (take lst n)
      (if (or (= n 0) (null? lst))
          '()
          (cons (car lst) (take (cdr lst) (- n 1)))))

    (define (drop lst n)
      (if (or (= n 0) (null? lst))
          lst
          (drop (cdr lst) (- n 1))))

    (define (take-while pred lst)
      (cond ((null? lst) '())
            ((pred (car lst)) (cons (car lst) (take-while pred (cdr lst))))
            (else '())))

    (define (drop-while pred lst)
      (cond ((null? lst) '())
            ((pred (car lst)) (drop-while pred (cdr lst)))
            (else lst)))

    (define (last-pair lst)
      (if (null? (cdr lst)) lst (last-pair (cdr lst))))

    (define (count pred lst)
      (fold-left (lambda (acc x) (if (pred x) (+ acc 1) acc)) 0 lst))

    (define (partition pred lst)
      (let loop ((l lst) (yes '()) (no '()))
        (cond ((null? l) (values (reverse yes) (reverse no)))
              ((pred (car l)) (loop (cdr l) (cons (car l) yes) no))
              (else           (loop (cdr l) yes (cons (car l) no))))))

    (define first  car)
    (define second cadr)
    (define third  caddr)
    (define (fourth lst) (car (cdr (cdr (cdr lst)))))
    (define (fifth  lst) (car (cdr (cdr (cdr (cdr lst))))))))
