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
