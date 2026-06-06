;;; Number theory tests — primality, factoring, divisor functions, modular
;;; arithmetic, combinatorics, continued fractions.  Always built (GMP only).

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

;;; ----------------------------------------------------------------------
;;; Primality
;;; ----------------------------------------------------------------------
(check "prime? 2"        (prime? 2)  #t)
(check "prime? 17"       (prime? 17) #t)
(check "prime? 15"       (prime? 15) #f)
(check "prime? 1"        (prime? 1)  #f)
(check "prime? 0"        (prime? 0)  #f)
(check "prime? 97"       (prime? 97) #t)
(check "prime? 561"      (prime? 561) #f) ; Carmichael number, not prime

(check "next-prime 10"   (next-prime 10) 11)
(check "next-prime 11"   (next-prime 11) 13)
(check "next-prime 100"  (next-prime 100) 101)

(check "prev-prime 11"   (prev-prime 11) 7)
(check "prev-prime 100"  (prev-prime 100) 97)

;;; ----------------------------------------------------------------------
;;; Factoring
;;; ----------------------------------------------------------------------
(check "factor 12"       (factor 12)  '(2 2 3))
(check "factor 360"      (factor 360) '(2 2 2 3 3 5))
(check "factor 1"        (factor 1)   '())
(check "factor 13"       (factor 13)  '(13))
(check "factor 100"      (factor 100) '(2 2 5 5))

(check "prime-factors 12"   (prime-factors 12)  '(2 3))
(check "prime-factors 360"  (prime-factors 360) '(2 3 5))
(check "prime-factors 100"  (prime-factors 100) '(2 5))

;;; ----------------------------------------------------------------------
;;; Arithmetic functions
;;; ----------------------------------------------------------------------
(check "totient 1"       (totient 1)   1)
(check "totient 6"       (totient 6)   2)
(check "totient 7"       (totient 7)   6)
(check "totient 36"      (totient 36)  12)
(check "totient 100"     (totient 100) 40)

(check "mobius 1"        (mobius 1)  1)
(check "mobius 2"        (mobius 2)  -1)
(check "mobius 4"        (mobius 4)  0)
(check "mobius 6"        (mobius 6)  1)
(check "mobius 30"       (mobius 30) -1) ; 3 distinct primes
(check "mobius 12"       (mobius 12) 0)  ; 2² is a squared factor

(check "divisors 12"     (divisors 12)  '(1 2 3 4 6 12))
(check "divisors 1"      (divisors 1)   '(1))
(check "divisors 28"     (divisors 28)  '(1 2 4 7 14 28))

(check "divisor-count 12"   (divisor-count 12)  6)
(check "divisor-count 1"    (divisor-count 1)   1)
(check "divisor-sum 12"     (divisor-sum 12)    28)
(check "divisor-sum 6"      (divisor-sum 6)     12)

(check "perfect? 6"      (perfect? 6)  #t)
(check "perfect? 28"     (perfect? 28) #t)
(check "perfect? 10"     (perfect? 10) #f)
(check "abundant? 12"    (abundant? 12) #t)
(check "deficient? 10"   (deficient? 10) #t)

(check "omega 12"        (omega 12)     2)
(check "omega 30"        (omega 30)     3)
(check "big-omega 12"    (big-omega 12) 3)
(check "big-omega 360"   (big-omega 360) 6)

(check "carmichael 7"    (carmichael 7)  6)
(check "carmichael 8"    (carmichael 8)  2)
(check "carmichael 15"   (carmichael 15) 4)

;;; ----------------------------------------------------------------------
;;; Modular arithmetic
;;; ----------------------------------------------------------------------
(check "mod-expt 2 10 1000"  (mod-expt 2 10 1000) 24)
(check "mod-expt 3 5 7"      (mod-expt 3 5 7)     5)
(check "mod-expt big"        (mod-expt 7 100 101) 1) ; Fermat: 7^100 mod 101

(check "mod-inverse 3 7"     (mod-inverse 3 7)   5)
(check "mod-inverse 5 11"    (mod-inverse 5 11)  9)

(check "jacobi-symbol 5 9"   (jacobi-symbol 5 9)  1)
(check "jacobi-symbol 2 7"   (jacobi-symbol 2 7)  1)
(check "jacobi-symbol 3 5"   (jacobi-symbol 3 5)  -1)

(check "legendre-symbol 2 7" (legendre-symbol 2 7) 1)

(check-pred "extended-gcd gcd"
            (call-with-values
              (lambda () (extended-gcd 35 15))
              (lambda (g s t)
                (and (= g 5) (= (+ (* 35 s) (* 15 t)) 5)))))

(check "chinese-remainder simple"
       (chinese-remainder '(2 3 2) '(3 5 7)) 23)

;;; ----------------------------------------------------------------------
;;; Sequences
;;; ----------------------------------------------------------------------
(check "fibonacci 0"     (fibonacci 0)   0)
(check "fibonacci 1"     (fibonacci 1)   1)
(check "fibonacci 10"    (fibonacci 10)  55)
(check "fibonacci 20"    (fibonacci 20)  6765)
(check "fibonacci 50"    (fibonacci 50)  12586269025)
(check "fibonacci 100"   (fibonacci 100) 354224848179261915075)

(check "lucas 0"         (lucas 0) 2)
(check "lucas 1"         (lucas 1) 1)
(check "lucas 5"         (lucas 5) 11)
(check "lucas 10"        (lucas 10) 123)

(check "binomial 10 3"   (binomial 10 3) 120)
(check "binomial 5 2"    (binomial 5 2)  10)
(check "binomial 0 0"    (binomial 0 0)  1)
(check "binomial 100 50" (binomial 100 50)
       100891344545564193334812497256)

(check "catalan 0"       (catalan 0) 1)
(check "catalan 5"       (catalan 5) 42)
(check "catalan 10"      (catalan 10) 16796)

(check "bernoulli 0"     (bernoulli 0)  1)
(check "bernoulli 1"     (bernoulli 1)  -1/2)
(check "bernoulli 2"     (bernoulli 2)  1/6)
(check "bernoulli 4"     (bernoulli 4)  -1/30)
(check "bernoulli 6"     (bernoulli 6)  1/42)

(check "euler-number 0"  (euler-number 0) 1)
(check "euler-number 2"  (euler-number 2) -1)
(check "euler-number 4"  (euler-number 4) 5)
(check "euler-number 6"  (euler-number 6) -61)

(check "stirling1 3 2"   (stirling1 3 2) 3)
(check "stirling2 4 2"   (stirling2 4 2) 7)
(check "stirling2 5 3"   (stirling2 5 3) 25)

(check "bell 0"          (bell 0) 1)
(check "bell 1"          (bell 1) 1)
(check "bell 5"          (bell 5) 52)
(check "bell 7"          (bell 7) 877)

(check "partition-count 0"  (partition-count 0)  1)
(check "partition-count 5"  (partition-count 5)  7)
(check "partition-count 10" (partition-count 10) 42)
(check "partition-count 20" (partition-count 20) 627)

;;; ----------------------------------------------------------------------
;;; Continued fractions
;;; ----------------------------------------------------------------------
(check "continued-fraction 3/7"  (continued-fraction 3/7) '(0 2 3))
(check "continued-fraction 22/7" (continued-fraction 22/7) '(3 7))

;;; convergents of [3; 7, 15, 1, ...]: 3/1, 22/7, 333/106, 355/113
(check "convergents (3 7)"     (convergents '(3 7))    '(3 22/7))
(check "convergents (3 7 15)"  (convergents '(3 7 15)) '(3 22/7 333/106))

;;; 22/7 is the best with denom ≤ 7; 311/99 is best with denom ≤ 100
(check "best-rational-approx π 7"
       (best-rational-approx 3.141592653589793 7) 22/7)
(check "best-rational-approx π 100"
       (best-rational-approx 3.141592653589793 100) 311/99)

;;; ----------------------------------------------------------------------
;;; Number predicates
;;; ----------------------------------------------------------------------
(check "squarefree? 6"   (squarefree? 6)  #t)
(check "squarefree? 12"  (squarefree? 12) #f)
(check "squarefree? 30"  (squarefree? 30) #t)

(check-pred "perfect-power? 64"
            (call-with-values
              (lambda () (perfect-power? 64))
              (lambda (b e) (and (= b 2) (>= e 2)))))

(check "perfect-power? 12"   (perfect-power? 12)  #f)

(check "smooth? 30 5"     (smooth? 30 5)  #t) ; primes 2,3,5
(check "smooth? 30 3"     (smooth? 30 3)  #f) ; has factor 5

;;; ----------------------------------------------------------------------
;;; Summary
;;; ----------------------------------------------------------------------
(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
