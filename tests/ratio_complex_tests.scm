;;; Complex numbers whose real part is a ratio, and ratios with a zero
;;; denominator (#387).
;;;
;;; "1/2+1i" was read as the ratio 1 over the complex number 2+1i and raised
;;; "exact integer required, got complex"; write printed such numbers in a form
;;; read could not read back. "1/0" killed the process with SIGFPE.

(define pass 0)
(define fail 0)
(define (check label got expected)
  (if (equal? got expected)
      (set! pass (+ pass 1))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write got)
             (display " expected ") (write expected) (newline)
             (set! fail (+ fail 1)))))

(define (parts z) (list (real-part z) (imag-part z)))

(check "string->number: ratio real part, integer imaginary part"
       (parts (string->number "1/2+1i")) '(1/2 1))
(check "ratio real part, ratio imaginary part"
       (parts (string->number "1/2+1/3i")) '(1/2 1/3))
(check "negative imaginary ratio" (parts (string->number "1/2-1/3i")) '(1/2 -1/3))
(check "negative ratio real part" (parts (string->number "-1/2+1/3i")) '(-1/2 1/3))
(check "explicit plus sign" (parts (string->number "+1/2+1/3i")) '(1/2 1/3))
(check "integer real part, ratio imaginary part" (parts (string->number "1+1/3i")) '(1 1/3))
(check "flonum real part, ratio imaginary part" (parts (string->number "1.5+1/3i")) '(1.5 1/3))
(check "ratio real part, flonum imaginary part" (parts (string->number "1/2+3.5i")) '(1/2 3.5))
(check "ratio real part with a unit imaginary part" (parts (string->number "1/2+i")) '(1/2 1))
(check "a purely imaginary ratio" (parts (string->number "1/2i")) '(0 1/2))
(check "a bignum ratio real part"
       (real-part (string->number "100000000000000000000000/3+1i")) 100000000000000000000000/3)
(check "hexadecimal ratio parts"
       (parts (string->number "ff/10+a/2i" 16)) '(255/16 5))

(check "plain ratio" (string->number "1/2") 1/2)
(check "a bignum ratio" (string->number "12345678901234567890123/3") 4115226300411522630041)
(check "ff/10 in radix 16" (string->number "ff/10" 16) 255/16)
(check "1/ is not a number" (string->number "1/") #f)
(check "1/2+ is not a number" (string->number "1/2+") #f)
(check "1/2/3 is not a number" (string->number "1/2/3") #f)
(check "1/2x is not a number" (string->number "1/2x") #f)
(check "a ratio with the j suffix is not a number" (string->number "1/2+1/3j") #f)

;;; A zero denominator is not a number (it used to kill the process).
(check "1/0" (string->number "1/0") #f)
(check "0/0" (string->number "0/0") #f)
(check "1/00" (string->number "1/00") #f)
(check "0/5 is zero" (string->number "0/5") 0)
(check "a zero denominator in the imaginary part" (string->number "1/2+1/0i") #f)
(check "a zero denominator in the real part" (string->number "1/0+1i") #f)
(check "the token 1/0 reads as a symbol" (symbol? (quote 1/0)) #t)

;;; Literals in source, and write -> read round trips.
(define z 1/2+1/3i)
(check "a literal in source" (parts z) '(1/2 1/3))
(check "arithmetic on it" (* z 2) (make-rectangular 1 2/3))
(check "a sum with its conjugate" (+ 1/2+1/3i 1/2-1/3i) 1)
(check "write then read gives the same number"
       (let ((p (open-output-string)))
         (write (make-rectangular 3/4 -5/6) p)
         (= (string->number (get-output-string p)) (make-rectangular 3/4 -5/6)))
       #t)
(check "its printed form" (number->string (make-rectangular 1/2 1/3)) "1/2+1/3i")

(newline)
(display pass) (display " passed, ") (display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
