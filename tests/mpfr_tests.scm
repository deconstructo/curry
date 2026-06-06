;;; MPFR arbitrary-precision float tests.
;;; Only run when curry is compiled with -DBUILD_MPFR=ON.

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

(define (check-pred label result)
  (if result
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label) (newline)
             (set! fail (+ fail 1)))))

(define (check-approx label result expected eps)
  (if (< (abs (- result expected)) eps)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write result)
             (display " expected ~") (write expected)
             (newline)
             (set! fail (+ fail 1)))))

;;; ----------------------------------------------------------------------
;;; Type predicates and construction
;;; ----------------------------------------------------------------------
(check "mpfr? 1.0"        (mpfr? 1.0) #f)
(check "mpfr? fixnum"     (mpfr? 42)  #f)
(check "mpfr? (mpfr 1.0)" (mpfr? (mpfr 1.0)) #t)
(check "mpfr? mpfr-pi"    (mpfr? (mpfr-pi)) #t)
(check "mpfr? mpfr-e"     (mpfr? (mpfr-e)) #t)
(check "mpfr? mpfr-phi"   (mpfr? (mpfr-phi)) #t)

(check "number? mpfr"     (number? (mpfr 1.0)) #t)
(check "real? mpfr"       (real? (mpfr 1.0)) #t)
(check "inexact? mpfr"    (inexact? (mpfr 1.0)) #t)
(check "exact? mpfr"      (exact? (mpfr 1.0)) #f)

;;; ----------------------------------------------------------------------
;;; Precision context
;;; ----------------------------------------------------------------------
(check "mpfr-precision 256 pi"
       (mpfr-precision (with-precision 256 (mpfr-pi))) 256)
(check "mpfr-precision 512 pi"
       (mpfr-precision (with-precision 512 (mpfr-pi))) 512)

;;; ----------------------------------------------------------------------
;;; High-precision constants approximate the IEEE values
;;; ----------------------------------------------------------------------
(check-approx "mpfr-pi ≈ acos(-1)"
              (inexact (with-precision 100 (mpfr-pi)))
              (acos -1.0) 1e-14)
(check-approx "mpfr-e ≈ e"
              (inexact (with-precision 100 (mpfr-e)))
              2.718281828459045 1e-14)
(check-approx "mpfr-phi ≈ (1+√5)/2"
              (inexact (with-precision 100 (mpfr-phi)))
              1.618033988749895 1e-14)

;;; ----------------------------------------------------------------------
;;; YBC 7289: √2 to ~10 digits of accuracy
;;; ----------------------------------------------------------------------
(check-approx "mpfr √2"
              (inexact (with-precision 200 (mpfr-sqrt (mpfr 2))))
              (sqrt 2.0) 1e-14)

;;; ----------------------------------------------------------------------
;;; Mixing MPFR with fixnum / rational
;;; ----------------------------------------------------------------------
(check-approx "(mpfr 1/3) + (mpfr 2/3) ≈ 1"
              (inexact (with-precision 100
                         (+ (mpfr 1/3) (mpfr 2/3))))
              1.0 1e-28)

(check-approx "(mpfr 1) * (mpfr 2) = 2"
              (inexact (* (mpfr 1) (mpfr 2)))
              2.0 1e-14)

;;; ----------------------------------------------------------------------
;;; MPFR-specific transcendentals
;;; ----------------------------------------------------------------------
(check-approx "mpfr-gamma 5 = 24"
              (inexact (with-precision 100 (mpfr-gamma (mpfr 5))))
              24.0 1e-12)
(check-approx "mpfr-erf 0 = 0"
              (inexact (with-precision 100 (mpfr-erf (mpfr 0))))
              0.0 1e-14)
(check-approx "mpfr-zeta 2 ≈ π²/6"
              (inexact (with-precision 100 (mpfr-zeta (mpfr 2))))
              (/ (* 3.141592653589793 3.141592653589793) 6.0) 1e-12)

;;; ----------------------------------------------------------------------
;;; Comparison and predicates
;;; ----------------------------------------------------------------------
(check "= mpfr 1 1"   (= (mpfr 1) (mpfr 1)) #t)
(check "< mpfr 1 2"   (< (mpfr 1) (mpfr 2)) #t)
(check "positive? mpfr" (positive? (mpfr 1)) #t)
(check "negative? mpfr" (negative? (mpfr -1)) #t)
(check "zero? mpfr"   (zero? (mpfr 0)) #t)

;;; ----------------------------------------------------------------------
;;; current-precision and changing rounding mode (no-throw smoke test)
;;; ----------------------------------------------------------------------
(check-pred "current-precision is integer"
            (integer? (current-precision)))

;;; ----------------------------------------------------------------------
;;; Standard arithmetic dispatches to MPFR when operand is MPFR
;;; ----------------------------------------------------------------------
(check-pred "mpfr? of (+ mpfr fixnum)"
            (mpfr? (+ (mpfr 1) 2)))
(check-pred "mpfr? of (* mpfr rational)"
            (mpfr? (* (mpfr 3) 1/3)))
(check-pred "mpfr? of (- mpfr flonum)"
            (mpfr? (- (mpfr 5.0) 1.0)))
(check-pred "mpfr? of (/ mpfr mpfr)"
            (mpfr? (/ (mpfr 1) (mpfr 3))))
(check-pred "mpfr? of (sqrt mpfr)"
            (mpfr? (sqrt (mpfr 2))))
(check-pred "mpfr? of (exp mpfr)"
            (mpfr? (exp (mpfr 1))))
(check-pred "mpfr? of (log mpfr)"
            (mpfr? (log (mpfr 1))))
(check-pred "mpfr? of (sin mpfr)"
            (mpfr? (sin (mpfr 0))))
(check-pred "mpfr? of (cos mpfr)"
            (mpfr? (cos (mpfr 0))))
(check-approx "fixnum + mpfr precision preserved"
              (inexact (with-precision 200
                         (- (+ 1 (mpfr-pi)) (mpfr-pi))))
              1.0 1e-50)

;;; ----------------------------------------------------------------------
;;; exact->inexact promotes to MPFR inside with-precision
;;; ----------------------------------------------------------------------
(check-pred "inexact 1/3 → mpfr inside with-precision"
            (with-precision 256 (mpfr? (inexact 1/3))))
(check-pred "inexact integer → mpfr inside with-precision"
            (with-precision 128 (mpfr? (inexact 7))))

;;; ----------------------------------------------------------------------
;;; number->string on MPFR
;;; ----------------------------------------------------------------------
(check-pred "number->string mpfr gives string"
            (string? (number->string (mpfr-pi))))
(check-pred "number->string mpfr-pi starts with 3"
            (char=? #\3 (string-ref (number->string (mpfr-pi)) 0)))

;;; ----------------------------------------------------------------------
;;; number->string binary fix (radix 2)
;;; ----------------------------------------------------------------------
(check "number->string 255 base 2"  (number->string 255 2)  "11111111")
(check "number->string 10 base 2"   (number->string 10 2)   "1010")
(check "number->string 1 base 2"    (number->string 1 2)    "1")
(check "number->string 0 base 2"    (number->string 0 2)    "0")
(check "number->string 255 base 16" (number->string 255 16) "ff")
(check "number->string 255 base 8"  (number->string 255 8)  "377")

;;; ----------------------------------------------------------------------
;;; Additional transcendentals and rounding
;;; ----------------------------------------------------------------------
(check-pred "floor mpfr → mpfr"   (mpfr? (floor (mpfr 2.7))))
(check-pred "ceiling mpfr → mpfr" (mpfr? (ceiling (mpfr 2.1))))
(check-pred "truncate mpfr → mpfr" (mpfr? (truncate (mpfr -2.9))))
(check-approx "floor mpfr 2.7"    (floor (mpfr 2.7))    2.0 1e-14)
(check-approx "ceiling mpfr 2.1"  (ceiling (mpfr 2.1))  3.0 1e-14)
(check-approx "truncate mpfr -2.9" (truncate (mpfr -2.9)) -2.0 1e-14)
(check-approx "mpfr-erfc 0 = 1"
              (with-precision 100 (mpfr-erfc (mpfr 0))) 1.0 1e-14)
(check-approx "mpfr-hypot 3 4 = 5"
              (with-precision 100 (mpfr-hypot (mpfr 3) (mpfr 4))) 5.0 1e-14)
(check-approx "mpfr-fma 2 3 4 = 10"
              (with-precision 100 (mpfr-fma (mpfr 2) (mpfr 3) (mpfr 4))) 10.0 1e-14)

;;; ----------------------------------------------------------------------
;;; Precision propagation in binary ops
;;; ----------------------------------------------------------------------
(check "max prec in add"
       (mpfr-precision (+ (mpfr 1 256) (mpfr 1 128))) 256)
(check "max prec in mul"
       (mpfr-precision (* (mpfr 2 512) (mpfr 3 128))) 512)

;;; ----------------------------------------------------------------------
;;; Interval arithmetic
;;; ----------------------------------------------------------------------
(check "interval? yes"   (interval? (make-interval 1 2)) #t)
(check "interval? no"    (interval? 1.5) #f)

(check-approx "interval-width [1,3] = 2"
              (inexact (interval-width (make-interval 1 3)))
              2.0 1e-14)

(check-approx "interval-midpoint [1,3] = 2"
              (inexact (interval-midpoint (make-interval 1 3)))
              2.0 1e-14)

(check "interval-contains? in"  (interval-contains? (make-interval 1 3) 2) #t)
(check "interval-contains? out" (interval-contains? (make-interval 1 3) 5) #f)

;;; (interval 5) is the point interval [5,5]; its width is zero.
(check-approx "interval point width 0"
              (inexact (interval-width (interval 5)))
              0.0 1e-14)

;;; ----------------------------------------------------------------------
;;; Summary
;;; ----------------------------------------------------------------------
(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
