;;; Tests for Phase 4e: Risch integration (rational functions + log polynomials)

(define passed 0)
(define failed 0)

(define-syntax assert-equal
  (syntax-rules ()
    [(assert-equal label a b)
     (let ([va a] [vb b])
       (if (equal? va vb)
           (set! passed (+ passed 1))
           (begin (set! failed (+ failed 1))
                  (display "FAIL: ") (display label) (newline)
                  (display "  expected: ") (display vb) (newline)
                  (display "  got:      ") (display va) (newline))))]))

;;; Verify antiderivative numerically: F'(t) ≈ f(t) for several t
(define (check-antiderivative label f F var)
  (let* ([pts '(2 3 5 7 11)]
         [h 1/1000]
         [ok (for-all
               (lambda (pt)
                 (let* ([Fph (simplify (substitute F var (+ pt h)))]
                        [Fmh (simplify (substitute F var (- pt h)))]
                        [fpt (simplify (substitute f var pt))])
                   (and (number? Fph) (number? Fmh) (number? fpt)
                        (< (abs (- (/ (- Fph Fmh) (* 2 h)) fpt)) 1/100))))
               pts)])
    (if ok
        (set! passed (+ passed 1))
        (begin (set! failed (+ failed 1))
               (display "FAIL (numerical): ") (display label) (newline)))))

(define (for-all pred lst)
  (if (null? lst) #t (and (pred (car lst)) (for-all pred (cdr lst)))))

(define x (sym-var 'x))

;;; ============================================================
;;; 1. Rational functions — basic cases
;;; ============================================================

;;; 1/(x-1) → log|x-1|
(let ([F (∫ (/ 1 (- x 1)) x)])
  (check-antiderivative "1/(x-1)" (/ 1 (- x 1)) F x))

;;; 1/(x^2-1) → 1/2*log|x-1| - 1/2*log|x+1|
(let ([F (∫ (/ 1 (- (expt x 2) 1)) x)])
  (check-antiderivative "1/(x^2-1)" (/ 1 (- (expt x 2) 1)) F x)
  (assert-equal "1/(x^2-1) is a sum (not unevaluated)"
    (sym-expr? F) #t))

;;; 1/(x^2+1) → atan(x)
(assert-equal "1/(x^2+1) → atan(x)"
  (∫ (/ 1 (+ (expt x 2) 1)) x)
  (atan x))

;;; x/(x^2+1) → 1/2*log(x^2+1)
(assert-equal "x/(x^2+1) → 1/2*log|x^2+1|"
  (∫ (/ x (+ (expt x 2) 1)) x)
  (* 1/2 (log (abs (+ 1 (expt x 2))))))

;;; 1/(x*(x-1)) via partial fractions
(let ([F (∫ (/ 1 (* x (- x 1))) x)])
  (check-antiderivative "1/(x*(x-1))" (/ 1 (* x (- x 1))) F x))

;;; 2/(x^2+4) → atan(x/2)
(let ([F (∫ (/ 2 (+ (expt x 2) 4)) x)])
  (check-antiderivative "2/(x^2+4)" (/ 2 (+ (expt x 2) 4)) F x))

;;; ============================================================
;;; 2. Higher-degree denominators
;;; ============================================================

;;; 1/(x^3-x) = 1/(x*(x-1)*(x+1)) via partial fractions
(let ([F (∫ (/ 1 (- (expt x 3) x)) x)])
  (check-antiderivative "1/(x^3-x)" (/ 1 (- (expt x 3) x)) F x))

;;; ============================================================
;;; 3. Linear + quadratic numerators
;;; ============================================================

;;; (2x+1)/(x^2+x) — rational with linear numerator
(let* ([f (/ (+ (* 2 x) 1) (* x (+ x 1)))]
       [F (∫ f x)])
  (check-antiderivative "(2x+1)/(x*(x+1))" f F x))

;;; (x+2)/(x^2+1)
(let* ([f (/ (+ x 2) (+ (expt x 2) 1))]
       [F (∫ f x)])
  (check-antiderivative "(x+2)/(x^2+1)" f F x))

;;; ============================================================
;;; 4. Log of polynomial
;;; ============================================================

;;; ∫ log(x) dx = x*log(x) - x  (already in old integrator, verify still works)
(let ([F (∫ (log x) x)])
  (check-antiderivative "log(x)" (log x) F x))

;;; ∫ log(x^2+1) dx = x*log(x^2+1) - 2x + 2*atan(x)
(let ([F (∫ (log (+ (expt x 2) 1)) x)])
  (check-antiderivative "log(x^2+1)" (log (+ (expt x 2) 1)) F x)
  (assert-equal "log(x^2+1) antiderivative is not unevaluated"
    (sym-expr? F) #t))

;;; ∫ log(x-1) dx
(let ([F (∫ (log (- x 1)) x)])
  (check-antiderivative "log(x-1)" (log (- x 1)) F x))

;;; ============================================================
;;; 5. Polynomial / rational
;;; ============================================================

;;; ∫ (x^2+1)/(x+1) dx — polynomial part + remainder
(let* ([f (/ (+ (expt x 2) 1) (+ x 1))]
       [F (∫ f x)])
  (check-antiderivative "(x^2+1)/(x+1)" f F x))

;;; ============================================================
;;; Report
;;; ============================================================
(display "sx_risch tests: ")
(display passed) (display " passed, ")
(display failed) (display " failed")
(newline)
(if (> failed 0)
    (error "sx_risch test failures" failed)
    (display "OK\n"))
