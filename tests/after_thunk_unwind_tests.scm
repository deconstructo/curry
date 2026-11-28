;; #425: an after thunk that raises while a continuation unwinds through its
;; dynamic-wind runs once, not twice.
(define pass 0)
(define fail 0)
(define (check name got want)
  (if (equal? got want)
      (set! pass (+ pass 1))
      (begin (set! fail (+ fail 1))
             (display "FAIL: ") (display name) (display " got ") (write got)
             (display " want ") (write want) (newline))))

(define log '())
(define (L x) (set! log (cons x log)))

;; the issue's example: guard escapes, after thunk raises
(set! log '())
(define r1
  (guard (o (#t (list 'outer o)))
    (guard (e (#t (L 'clause) 'v))
      (dynamic-wind
        (lambda () (L 'in))
        (lambda () (raise-continuable 'x))
        (lambda () (L 'out) (raise 'from-after))))))
;; The inner guard is outside the dynamic-wind, so it is still installed when
;; the after thunk runs during the escape (R7RS 6.10) and handles the thunk's
;; own raise: its clause runs once, for that exception, after the after thunk
;; (#431), and the after thunk runs once.
(check "guard: result" r1 'v)
(check "guard: after ran once" (reverse log) '(in out clause))

;; same through an explicit call/cc escape
(set! log '())
(define r2
  (guard (o (#t (list 'outer o)))
    (call/cc
      (lambda (k)
        (dynamic-wind
          (lambda () (L 'in))
          (lambda () (k 'escaped))
          (lambda () (L 'out) (raise 'from-after)))))))
(check "call/cc: result" r2 '(outer from-after))
(check "call/cc: after ran once" (reverse log) '(in out))

;; nested: the inner after thunk's exception goes to the handlers outside
;; the inner dynamic-wind, here the middle one
(set! log '())
(define r3
  (guard (o (#t (list 'outer o)))
    (call/cc
      (lambda (k)
        (dynamic-wind
          (lambda () (L 'in1))
          (lambda ()
            (guard (m (#t (L 'middle) (k 'done)))
              (dynamic-wind
                (lambda () (L 'in2))
                (lambda () (k 'escaped))
                (lambda () (L 'out2) (raise 'from-after2)))))
          (lambda () (L 'out1)))))))
(check "nested: result" r3 'done)
(check "nested: order" (reverse log) '(in1 in2 out2 middle out1))

;; a normal escape still runs every after thunk, in order
(set! log '())
(call/cc
  (lambda (k)
    (dynamic-wind (lambda () (L 'a)) 
      (lambda () (dynamic-wind (lambda () (L 'b)) (lambda () (k 1)) (lambda () (L 'c))))
      (lambda () (L 'd)))))
(check "plain escape" (reverse log) '(a b c d))

;; handlers are intact afterwards
(check "handler intact"
  (guard (e (#t (list 'caught e))) (raise 'z))
  '(caught z))

(display pass) (display " passed, ") (display fail) (display " failed") (newline)
(if (> fail 0) (exit 1))
