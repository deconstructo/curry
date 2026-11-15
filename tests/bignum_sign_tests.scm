;;; A signed integer too big for a machine word reads as itself (#393).
;;;
;;; num_make_bignum_str ignored GMP's failure result, and GMP does not take a
;;; leading '+', so "+100000000000000000000" became 0.

(define pass 0)
(define fail 0)
(define (check label got expected)
  (if (equal? got expected)
      (set! pass (+ pass 1))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write got)
             (display " expected ") (write expected) (newline)
             (set! fail (+ fail 1)))))
(define (s->n s . r) (apply string->number s r))
(define big (expt 10 20))

(check "string->number +10^20" (s->n "+100000000000000000000") big)
(check "string->number -10^20" (s->n "-100000000000000000000") (- big))
(check "string->number 10^20" (s->n "100000000000000000000") big)
(check "a 30 digit number with +" (s->n "+123456789012345678901234567890") 123456789012345678901234567890)
(check "+ in front of the largest fixnum range edge" (s->n "+9223372036854775807") 9223372036854775807)
(check "+ in front of one past it" (s->n "+9223372036854775808") 9223372036854775808)
(check "- one past the other edge" (s->n "-9223372036854775809") -9223372036854775809)

;;; read from source
(check "source +10^20" +100000000000000000000 big)
(check "source -10^20" -100000000000000000000 (- big))
(check "source 10^20" 100000000000000000000 big)
(check "in a list" '(+100000000000000000000 -100000000000000000000) (list big (- big)))

;;; in other places a number can be
(check "ratio with a signed bignum numerator" (s->n "+100000000000000000000/3") (/ big 3))
(check "ratio with a signed bignum denominator" (s->n "1/+100000000000000000000") #f)
(check "negative ratio" (s->n "-100000000000000000000/3") (/ (- big) 3))
(check "complex with a signed bignum real part"
       (real-part (s->n "+100000000000000000000+1i")) big)
(check "complex with a signed bignum imaginary part"
       (imag-part (s->n "1+100000000000000000000i")) big)
(check "radix 16" (s->n "#x+FFFFFFFFFFFFFFFFFFFF") 1208925819614629174706175)
(check "radix 2" (s->n "+1111111111111111111111111111111111111111111111111111111111111111111" 2)
       (- (expt 2 67) 1))
(check "#e" (s->n "#e+100000000000000000000") big)

;;; text GMP does not accept is not a number
(check "+" (s->n "+") #f)
(check "-" (s->n "-") #f)
(check "+-1" (s->n "+-1") #f)
(check "++1" (s->n "++1") #f)
(check "a digit outside the radix" (s->n "+100000000000000000002" 2) #f)
(check "a hex prefix with radix 16 is not accepted either" (s->n "0xFFFFFFFFFFFFFFFFFFFF" 16) #f)
(check "trailing text" (s->n "+100000000000000000000x") #f)

(newline)
(display pass) (display " passed, ") (display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
