;;; (srfi s8 receive), (srfi s145 assume), (srfi s227 optional-arguments)

(import (srfi s8 receive) (srfi s145 assume) (srfi s227 optional-arguments))

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

;;; receive

(check "receive binds multiple values"
       (receive (a b) (values 1 2) (+ a b))
       3)

;;; assume

(check "assume passes silently when true" (begin (assume (= 1 1)) 'ok) 'ok)
(check "assume raises when false"
       (guard (e (#t 'caught)) (assume (= 1 2) "boom"))
       'caught)

;;; opt-lambda / let-optionals*

(define f (opt-lambda (a #:optional (b 10) c) (list a b (default-object? c))))
(check "opt-lambda: all args supplied" (f 1 2 3) (list 1 2 #f))
(check "opt-lambda: default value used, trailing optional also defaulted" (f 1) (list 1 10 #t))
(check "opt-lambda: bare optional defaults to default-object" (f 1 2) (list 1 2 #t))

(define g (opt-lambda (a #:rest r) (cons a r)))
(check "opt-lambda: #:rest collects remaining args" (g 1 2 3) '(1 2 3))

(check "let-optionals* binds sequentially with defaults"
       (let-optionals* (list 1 2) ((x 0) (y 0) (z 99)) (list x y z))
       (list 1 2 99))

(check "let-optionals* accepts a dotted tail-var after the option specs"
       (let-optionals* (list 1 2 3 4) ((x 0) (y 0) . rest) (list x y rest))
       (list 1 2 '(3 4)))

;;; opt*-lambda / define-optionals / define-optionals*

(define h (opt*-lambda (a #:optional (b (* a 2)) (c (+ a b))) (list a b c)))
(check "opt*-lambda: later default sees an earlier optional's value" (h 5) (list 5 10 15))
(check "opt*-lambda: explicit args still override defaults" (h 5 1) (list 5 1 6))

(define-optionals (i a #:optional (b (* a 3))) (+ a b))
(check "define-optionals: default applied" (i 4) 16)
(check "define-optionals: explicit arg overrides default" (i 4 100) 104)

(define-optionals* (j a #:optional (b (* a 3)) (c (+ a b))) (list a b c))
(check "define-optionals*: later default sees an earlier optional's value" (j 4) (list 4 12 16))

;;; Summary

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
