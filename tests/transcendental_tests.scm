;;; Phase 1 — Transcendental functions: symbolic differentiation, integration,
;;; infix/LaTeX output, numeric evaluation, and chain-rule tests.
;;;
;;; Covers: sinh cosh tanh  asin acos atan  asinh acosh atanh  cot sec csc

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

(define (check-approx label result expected eps)
  (if (< (abs (- result expected)) eps)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " got ") (write result)
             (display " expected ~") (write expected)
             (newline)
             (set! fail (+ fail 1)))))

;;; Compare two symbolic expressions by their infix string representation.
(define (check-infix label expr expected-str)
  (check label (sym->infix expr) expected-str))

;;; Verify a symbolic expression numerically at a sample point.
(define (check-numeric label expr var val expected eps)
  (check-approx label
    (let ((r (substitute expr var val)))
      (if (number? r) (exact->inexact r)
          (error "substitute did not produce a number" label)))
    expected eps))

;;; ---- Setup ----

(symbolic x y)
(define pi (* 4 (atan 1.0)))
(define eps 1e-12)

;;; ---- Numeric evaluation at known points ----

(check-approx "sinh(0) = 0"    (sinh 0)    0.0         eps)
(check-approx "cosh(0) = 1"    (cosh 0)    1.0         eps)
(check-approx "tanh(0) = 0"    (tanh 0)    0.0         eps)
(check-approx "asinh(0) = 0"   (asinh 0)   0.0         eps)
(check-approx "acosh(1) = 0"   (acosh 1)   0.0         eps)
(check-approx "atanh(0) = 0"   (atanh 0)   0.0         eps)
(check-approx "asin(0) = 0"    (asin 0)    0.0         eps)
(check-approx "acos(1) = 0"    (acos 1)    0.0         eps)
(check-approx "atan(0) = 0"    (atan 0)    0.0         eps)
(check-approx "asin(1) = π/2"  (asin 1)    (/ pi 2)    eps)
(check-approx "acos(0) = π/2"  (acos 0)    (/ pi 2)    eps)
(check-approx "atan(1) = π/4"  (atan 1)    (/ pi 4)    eps)
(check-approx "cot(π/4) = 1"   (cot (/ pi 4))  1.0     eps)
(check-approx "sec(0) = 1"     (sec 0)     1.0         eps)
(check-approx "csc(π/2) = 1"   (csc (/ pi 2))  1.0     eps)

;;; Pythagorean / reciprocal identities (numeric)
(check-approx "cosh²-sinh²=1 at 1.5"
  (- (* (cosh 1.5) (cosh 1.5)) (* (sinh 1.5) (sinh 1.5)))  1.0  1e-13)
(check-approx "sec²-tan²=1 at 0.7"
  (- (* (sec 0.7) (sec 0.7)) (* (tan 0.7) (tan 0.7)))  1.0  eps)
(check-approx "csc²-cot²=1 at 0.7"
  (- (* (csc 0.7) (csc 0.7)) (* (cot 0.7) (cot 0.7)))  1.0  eps)

;;; ---- Symbolic form construction ----

(check "sinh(x) is symbolic"  (symbolic? (sinh x))  #t)
(check "cosh(x) is symbolic"  (symbolic? (cosh x))  #t)
(check "tanh(x) is symbolic"  (symbolic? (tanh x))  #t)
(check "asin(x) is symbolic"  (symbolic? (asin x))  #t)
(check "acos(x) is symbolic"  (symbolic? (acos x))  #t)
(check "atan(x) is symbolic"  (symbolic? (atan x))  #t)
(check "asinh(x) is symbolic" (symbolic? (asinh x)) #t)
(check "acosh(x) is symbolic" (symbolic? (acosh x)) #t)
(check "atanh(x) is symbolic" (symbolic? (atanh x)) #t)
(check "cot(x) is symbolic"   (symbolic? (cot x))   #t)
(check "sec(x) is symbolic"   (symbolic? (sec x))   #t)
(check "csc(x) is symbolic"   (symbolic? (csc x))   #t)

;;; ---- Differentiation — exact symbolic equality (linear argument) ----

;; Simple cases where the result is a single symbolic node — compare directly
(check "d/dx sinh(x) = cosh(x)"  (∂ (sinh x) x)  (cosh x))
(check "d/dx cosh(x) = sinh(x)"  (∂ (cosh x) x)  (sinh x))

;; Compound results — compare via infix string
(check-infix "d/dx tanh(x) = 1/cosh²(x)"   (∂ (tanh x)  x)  "1/cosh(x)^2")
(check-infix "d/dx asin(x)"                 (∂ (asin x)  x)  "1/sqrt(1 - x^2)")
(check-infix "d/dx acos(x)"                 (∂ (acos x)  x)  "-(1/sqrt(1 - x^2))")
(check-infix "d/dx atan(x)"                 (∂ (atan x)  x)  "1/(1 + x^2)")
(check-infix "d/dx asinh(x)"                (∂ (asinh x) x)  "1/sqrt(1 + x^2)")
(check-infix "d/dx acosh(x)"                (∂ (acosh x) x)  "1/sqrt(x^2 - 1)")
(check-infix "d/dx atanh(x)"                (∂ (atanh x) x)  "1/(1 - x^2)")
(check-infix "d/dx cot(x) = -csc²(x)"      (∂ (cot x)   x)  "-(1/sin(x)^2)")
(check-infix "d/dx sec(x) = sec·tan"        (∂ (sec x)   x)  "sec(x) * tan(x)")
(check-infix "d/dx csc(x) = -csc·cot"      (∂ (csc x)   x)  "-(cot(x) * csc(x))")

;;; ---- Differentiation — numeric verification at x = 0.5 ----

(define t0 0.5)
(define h  1e-7)

(define (num-deriv f t)
  (/ (- (f (+ t h)) (f (- t h))) (* 2 h)))

(define (sym-deriv-at expr var val)
  (let ((d (∂ expr var)))
    (exact->inexact (substitute d var val))))

(check-approx "∂ sinh numerically at 0.5"
  (sym-deriv-at (sinh x) x t0) (num-deriv sinh t0) 1e-6)
(check-approx "∂ cosh numerically at 0.5"
  (sym-deriv-at (cosh x) x t0) (num-deriv cosh t0) 1e-6)
(check-approx "∂ tanh numerically at 0.5"
  (sym-deriv-at (tanh x) x t0) (num-deriv tanh t0) 1e-6)
(check-approx "∂ asin numerically at 0.5"
  (sym-deriv-at (asin x) x t0) (num-deriv asin t0) 1e-6)
(check-approx "∂ acos numerically at 0.5"
  (sym-deriv-at (acos x) x t0) (num-deriv acos t0) 1e-6)
(check-approx "∂ atan numerically at 0.5"
  (sym-deriv-at (atan x) x t0) (num-deriv atan t0) 1e-6)
(check-approx "∂ asinh numerically at 0.5"
  (sym-deriv-at (asinh x) x t0) (num-deriv asinh t0) 1e-6)
(check-approx "∂ acosh numerically at 1.5"
  (sym-deriv-at (acosh x) x 1.5) (num-deriv acosh 1.5) 1e-6)
(check-approx "∂ atanh numerically at 0.5"
  (sym-deriv-at (atanh x) x t0) (num-deriv atanh t0) 1e-6)
(check-approx "∂ cot numerically at 0.5"
  (sym-deriv-at (cot x) x t0) (num-deriv cot t0) 1e-6)
(check-approx "∂ sec numerically at 0.5"
  (sym-deriv-at (sec x) x t0) (num-deriv sec t0) 1e-6)
(check-approx "∂ csc numerically at 0.5"
  (sym-deriv-at (csc x) x t0) (num-deriv csc t0) 1e-6)

;;; ---- Differentiation — chain rule (non-linear argument) ----

;; d/dx sinh(x²) = 2x·cosh(x²) — compare infix after simplify
(check-infix "d/dx sinh(x²) chain"
  (simplify (∂ (sinh (* x x)) x))
  "2 * x * cosh(x^2)")

;; d/dx cosh(3x) = 3·sinh(3x)
(check-infix "d/dx cosh(3x) chain"
  (simplify (∂ (cosh (* 3 x)) x))
  "3 * sinh(3 * x)")

;; d/dx asin(2x) — numeric verification (simplifier doesn't expand (2x)²)
(check-approx "∂ asin(2x) chain at 0.3"
  (sym-deriv-at (asin (* 2 x)) x 0.3)
  (num-deriv (lambda (t) (asin (* 2 t))) 0.3)
  1e-6)

;; d/dx atan(x²) — numeric verification
(check-approx "∂ atan(x²) chain at 0.5"
  (sym-deriv-at (atan (* x x)) x t0)
  (num-deriv (lambda (t) (atan (* t t))) t0)
  1e-6)

;;; ---- Second derivatives ----

;; d²/dx² sinh(x) = sinh(x)   (follows from the mutual-diff property)
(check "d²/dx² sinh(x) = sinh(x)"
  (∂ (∂ (sinh x) x) x)  (sinh x))

;; d²/dx² cosh(x) = cosh(x)
(check "d²/dx² cosh(x) = cosh(x)"
  (∂ (∂ (cosh x) x) x)  (cosh x))

;;; ---- Integration — infix comparison ----

(check-infix "∫sinh(x) dx = cosh(x)"         (∫ (sinh x) x)  "cosh(x)")
(check-infix "∫cosh(x) dx = sinh(x)"         (∫ (cosh x) x)  "sinh(x)")
(check-infix "∫tanh(x) dx = log(cosh(x))"   (∫ (tanh x) x)  "log(cosh(x))")
(check-infix "∫cot(x) dx = log|sin(x)|"     (∫ (cot x)  x)  "log(|sin(x)|)")
(check-infix "∫sec(x) dx"                   (∫ (sec x)  x)  "log(|sec(x) + tan(x)|)")
(check-infix "∫csc(x) dx"                   (∫ (csc x)  x)  "-log(|csc(x) + cot(x)|)")
(check-infix "∫asin(x) dx (IBP)"            (∫ (asin x) x)  "x * asin(x) + sqrt(1 - x^2)")
(check-infix "∫acos(x) dx (IBP)"            (∫ (acos x) x)  "x * acos(x) - sqrt(1 - x^2)")
(check-infix "∫atan(x) dx (IBP)"            (∫ (atan x) x)  "x * atan(x) - log(1 + x^2)/2")
(check-infix "∫asinh(x) dx (IBP)"           (∫ (asinh x) x) "x * asinh(x) - sqrt(1 + x^2)")
(check-infix "∫acosh(x) dx (IBP)"           (∫ (acosh x) x) "x * acosh(x) - sqrt(x^2 - 1)")
(check-infix "∫atanh(x) dx (IBP)"           (∫ (atanh x) x) "x * atanh(x) + log(1 - x^2)/2")

;;; Scaled argument
(check-infix "∫sinh(2x) dx = cosh(2x)/2"   (∫ (sinh (* 2 x)) x)  "cosh(2 * x)/2")
(check-infix "∫cosh(3x) dx = sinh(3x)/3"   (∫ (cosh (* 3 x)) x)  "sinh(3 * x)/3")

;;; ---- Definite integrals at known values ----

;; ∫₀¹ sinh(x) dx = cosh(1) - cosh(0) = cosh(1) - 1
(check-approx "∫₀¹ sinh(x) dx = cosh(1)-1"
  (∫ (sinh x) x 0 1)  (- (cosh 1) 1)  eps)

;; ∫₀¹ cosh(x) dx = sinh(1)
(check-approx "∫₀¹ cosh(x) dx = sinh(1)"
  (∫ (cosh x) x 0 1)  (sinh 1)  eps)

;; ∫₀¹ atan'(x) dx = atan(1) - atan(0) = π/4
(check-approx "∫₀¹ 1/(1+x²) dx = π/4"
  (∫ (/ 1 (+ 1 (* x x))) x 0 1)  (/ pi 4)  eps)

;; ∫₀¹ asin(x) dx = [x·asin(x) + √(1-x²)]₀¹ = π/2 - 1
(check-approx "∫₀¹ asin(x) dx = π/2-1"
  (∫ (asin x) x 0 1)  (- (/ pi 2) 1)  eps)

;;; ---- sym->infix output ----

(check-infix "infix sinh(x)"  (sinh x)  "sinh(x)")
(check-infix "infix cosh(x)"  (cosh x)  "cosh(x)")
(check-infix "infix tanh(x)"  (tanh x)  "tanh(x)")
(check-infix "infix asin(x)"  (asin x)  "asin(x)")
(check-infix "infix acos(x)"  (acos x)  "acos(x)")
(check-infix "infix atan(x)"  (atan x)  "atan(x)")
(check-infix "infix asinh(x)" (asinh x) "asinh(x)")
(check-infix "infix acosh(x)" (acosh x) "acosh(x)")
(check-infix "infix atanh(x)" (atanh x) "atanh(x)")
(check-infix "infix cot(x)"   (cot x)   "cot(x)")
(check-infix "infix sec(x)"   (sec x)   "sec(x)")
(check-infix "infix csc(x)"   (csc x)   "csc(x)")

;;; ---- sym->latex output ----

(check "latex sinh"  (sym->latex (sinh x))  "\\sinh\\!\\left(x\\right)")
(check "latex cosh"  (sym->latex (cosh x))  "\\cosh\\!\\left(x\\right)")
(check "latex tanh"  (sym->latex (tanh x))  "\\tanh\\!\\left(x\\right)")
(check "latex asin"  (sym->latex (asin x))  "\\arcsin\\!\\left(x\\right)")
(check "latex acos"  (sym->latex (acos x))  "\\arccos\\!\\left(x\\right)")
(check "latex atan"  (sym->latex (atan x))  "\\arctan\\!\\left(x\\right)")
(check "latex asinh" (sym->latex (asinh x)) "\\operatorname{arcsinh}\\!\\left(x\\right)")
(check "latex acosh" (sym->latex (acosh x)) "\\operatorname{arccosh}\\!\\left(x\\right)")
(check "latex atanh" (sym->latex (atanh x)) "\\operatorname{arctanh}\\!\\left(x\\right)")
(check "latex cot"   (sym->latex (cot x))   "\\cot\\!\\left(x\\right)")
(check "latex sec"   (sym->latex (sec x))   "\\sec\\!\\left(x\\right)")
(check "latex csc"   (sym->latex (csc x))   "\\csc\\!\\left(x\\right)")
(check "latex atan′" (sym->latex (∂ (atan x) x))
       "\\frac{1}{1 + x^{2}}")

;;; ---- Substitution into transcendentals ----

(check-approx "substitute sinh(x) x=1"
  (exact->inexact (substitute (sinh x) x 1))  (sinh 1.0)  eps)

(check-approx "substitute atan(x) x=1"
  (exact->inexact (substitute (atan x) x 1))  (/ pi 4)  eps)

(check-approx "substitute cosh(3x) x=0.5"
  (exact->inexact (substitute (cosh (* 3 x)) x 0.5))  (cosh 1.5)  eps)

;;; ---- Summary ----

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed") (newline)
(if (> fail 0) (error "tests failed" fail))
