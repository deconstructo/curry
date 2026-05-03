;;; Tests for parameterize + dynamic-wind correctness

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

;;; parameterize restores on normal exit
(define p (make-parameter 1))
(parameterize ((p 2)) 'ignored)
(check "parameterize restore normal" (p) 1)

;;; parameterize restores via escape continuation
(define q (make-parameter 10))
(call/cc
  (lambda (k)
    (parameterize ((q 99))
      (k 'escaped))))
(check "parameterize restore via escape" (q) 10)

;;; nested parameterize
(define r (make-parameter 'a))
(parameterize ((r 'b))
  (parameterize ((r 'c))
    (check "nested parameterize inner" (r) 'c))
  (check "nested parameterize outer" (r) 'b))
(check "nested parameterize restored" (r) 'a)

;;; dynamic-wind after runs on escape
(define log '())
(call/cc
  (lambda (k)
    (dynamic-wind
      (lambda () (set! log (cons 'in log)))
      (lambda () (k 'done))
      (lambda () (set! log (cons 'out log))))))
(check "dynamic-wind after on escape" log '(out in))

;;; dynamic-wind + parameterize interaction
(define s (make-parameter 0))
(define dw-log '())
(call/cc
  (lambda (k)
    (dynamic-wind
      (lambda () (set! dw-log (cons 'wind-in dw-log)))
      (lambda ()
        (parameterize ((s 42))
          (k 'out)))
      (lambda () (set! dw-log (cons 'wind-out dw-log))))))
(check "param restored after dw escape" (s) 0)
(check "dw after ran on escape" (memv 'wind-out dw-log) '(wind-out wind-in))

;;; parameterize with converter
(define cvt-param (make-parameter 0 (lambda (x) (* x 2))))
(check "make-parameter converter applied at init" (cvt-param) 0)
(parameterize ((cvt-param 5))
  (check "parameterize converter applied" (cvt-param) 10))
(check "parameterize converter restored" (cvt-param) 0)

;;; multiple bindings in one parameterize
(define pa (make-parameter 'a))
(define pb (make-parameter 'b))
(parameterize ((pa 'x) (pb 'y))
  (check "multi-param pa" (pa) 'x)
  (check "multi-param pb" (pb) 'y))
(check "multi-param pa restored" (pa) 'a)
(check "multi-param pb restored" (pb) 'b)

;;; dynamic-wind normal execution (no escape)
(define dw-normal '())
(dynamic-wind
  (lambda () (set! dw-normal (cons 'in dw-normal)))
  (lambda () (set! dw-normal (cons 'body dw-normal)))
  (lambda () (set! dw-normal (cons 'out dw-normal))))
(check "dynamic-wind normal order" dw-normal '(out body in))

;;; Summary
(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
