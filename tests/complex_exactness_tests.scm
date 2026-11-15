;;; exact / inexact on complex numbers (#386).
;;;
;;; (inexact 1+2i) used to be 1.0 (the imaginary part was dropped) and
;;; (exact 1.5+2.5i) was returned unchanged, with inexact parts.

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

(check "inexact of an exact complex" (inexact 1+2i) 1.0+2.0i)
(check "exact->inexact is the same" (exact->inexact 1+2i) 1.0+2.0i)
(check "inexact keeps both parts" (list (real-part (inexact 3+4i)) (imag-part (inexact 3+4i))) '(3.0 4.0))
(check "inexact of an inexact complex is unchanged" (inexact 1.5+2.5i) 1.5+2.5i)
(check "inexact of rational parts"
       (inexact (make-rectangular 1/2 1/4)) (make-rectangular 0.5 0.25))
(check "inexact of a bignum part"
       (real-part (inexact (make-rectangular (expt 10 30) 1))) 1e30)

(check "exact of an inexact complex" (exact 1.5+2.5i) (make-rectangular 3/2 5/2))
(check "inexact->exact is the same" (inexact->exact 0.5+0.25i) (make-rectangular 1/2 1/4))
(check "exact parts are exact" (list (exact? (real-part (exact 1.5+2.5i))) (exact? (imag-part (exact 1.5+2.5i)))) '(#t #t))
(check "exact of an exact complex is unchanged" (exact 1+2i) 1+2i)
(check "exact of a complex whose imaginary part is 0.0 is a real"
       (exact (make-rectangular 2.0 0.0)) 2)
(check "exact of a complex with a NaN part raises"
       (raises? (lambda () (exact (make-rectangular (/ 0. 0.) 1.)))) #t)
(check "exact of a complex with an infinite part raises"
       (raises? (lambda () (exact (make-rectangular 1. +inf.0)))) #t)

(check "a round trip" (exact (inexact 3+4i)) 3+4i)
(check "numerically equal after conversion" (= (inexact 1+2i) 1+2i) #t)
(check "reals are unchanged" (list (inexact 1) (inexact 1/2) (exact 0.5) (exact 3)) '(1.0 0.5 1/2 3))

(newline)
(display pass) (display " passed, ") (display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
