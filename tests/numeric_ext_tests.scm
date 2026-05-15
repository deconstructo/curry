;;; Numeric tower extensions: multivectors, symbolic CAS, surreals, auto-diff
;;;
;;; Covers: Clifford algebra Cl(3,0,0), symbolic differentiation, symbolic
;;; integration, complex symbolic operators, Wirtinger calculus, numeric tower
;;; compatibility, surreal numbers, and automatic differentiation.

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


;;; =========================================================================
;;; Multivectors / Clifford algebra Cl(3,0,0)
;;; =========================================================================

(define e1   (mv-e 3 0 0 1))
(define e2   (mv-e 3 0 0 2))
(define e3   (mv-e 3 0 0 3))
(define e12  (mv-e 3 0 0 1 2))
(define e23  (mv-e 3 0 0 2 3))
(define e13  (mv-e 3 0 0 1 3))
(define e123 (mv-e 3 0 0 1 2 3))
(define scalar1 (mv-e 3 0 0))

(check "mv? yes"      (mv? e1) #t)
(check "mv? no"       (mv? 42) #f)
(check "mv-signature" (mv-signature e1) '(3 0 0))

;;; Geometric product: eᵢ·eᵢ = 1 in Cl(3,0,0)
(check "e1*e1 scalar" (mv-scalar (mv* e1 e1)) 1.0)
(check "e2*e2 scalar" (mv-scalar (mv* e2 e2)) 1.0)
(check "e3*e3 scalar" (mv-scalar (mv* e3 e3)) 1.0)

;;; e1*e2 = e12, e2*e1 = −e12  (anticommuting)
(define e12-prod (mv* e1 e2))
(define e21-prod (mv* e2 e1))
(check "e1*e2 grade-2 blade" (mv-scalar (mv* e12-prod e12-prod)) -1.0)
(check "e1*e2 = -e2*e1"      (mv-scalar (mv+ e12-prod e21-prod)) 0.0)

(check-approx "mv-norm e1"  (mv-norm e1)  1.0 1e-12)
(check-approx "mv-norm e12" (mv-norm e12) 1.0 1e-12)
(check "mv-norm2 e1"        (mv-norm2 e1) 1.0)

(define sum12 (mv+ e1 e2))
(check-approx "mv+ norm" (mv-norm sum12) (sqrt 2.0) 1e-12)

(define diff12 (mv- (mv+ e1 e2) e2))
(check-approx "mv- recovers e1" (mv-norm (mv- diff12 e1)) 0.0 1e-12)

(define two-e1 (mv-scale e1 2.0))
(check "mv-scale norm2" (mv-norm2 two-e1) 4.0)

;;; Grade extraction
(define mixed (mv+ scalar1 e1 e12 e123))
(check "mv-grade 0"         (mv-scalar (mv-grade mixed 0)) 1.0)
(check-approx "mv-grade 1 norm" (mv-norm (mv-grade mixed 1)) 1.0 1e-12)
(check-approx "mv-grade 2 norm" (mv-norm (mv-grade mixed 2)) 1.0 1e-12)
(check-approx "mv-grade 3 norm" (mv-norm (mv-grade mixed 3)) 1.0 1e-12)

;;; Algebraic operations
(check "mv-reverse e12"    (mv-scalar (mv+ e12 (mv-reverse  e12))) 0.0)
(check-approx "mv-involute e12" (mv-norm (mv- (mv-involute e12) e12)) 0.0 1e-12)
(check-approx "mv-involute e1"  (mv-norm (mv+ (mv-involute e1)  e1))  0.0 1e-12)
(check-approx "mv-conjugate e12" (mv-norm (mv+ (mv-conjugate e12) e12)) 0.0 1e-12)

(check "wedge e1^e1 = 0"  (mv-scalar (mv-wedge e1 e1)) 0.0)
(check "wedge e1^e2 = e12" (mv-norm (mv- (mv-wedge e1 e2) e12)) 0.0)

;;; Quaternion ↔ multivector roundtrip
(define q-orig (make-quaternion 1.0 2.0 3.0 4.0))
(define q-back (mv->quaternion (quaternion->mv q-orig)))
(check "quat->mv->quat roundtrip" (quaternion? q-back) #t)

;;; mv-from-list and mv-ref
(define mv-list (mv-from-list 2 0 0 '(1.0 2.0 3.0 4.0)))
(check "mv-from-list mv?" (mv? mv-list) #t)
(check "mv-ref blade 0"   (mv-ref mv-list 0) 1.0)
(check "mv-ref blade 1"   (mv-ref mv-list 1) 2.0)

(check-approx "mv-normalize" (mv-norm (mv-normalize (mv-scale e1 3.0))) 1.0 1e-12)


;;; =========================================================================
;;; Symbolic CAS — variables and expressions
;;; =========================================================================

(define x (sym-var 'x))
(define y (sym-var 'y))

(check "sym-var?"     (sym-var? x)      #t)
(check "sym-var? no"  (sym-var? 42)     #f)
(check "symbolic?"    (symbolic? x)     #t)
(check "sym-var-name" (sym-var-name x)  'x)
(check "sym-expr?"    (sym-expr? (* x x)) #t)

;;; Arithmetic builds expression trees
(check "x+1 sym-expr"  (sym-expr? (+ x 1))  #t)
(check "x*y sym-expr"  (sym-expr? (* x y))  #t)

;;; substitute evaluates
(check "substitute 2x→6"    (substitute (* 2 x)   x 3) 6)
(check "substitute x²→9"    (substitute (* x x)   x 3) 9)
(check "substitute x+y→5"   (substitute (substitute (+ x y) x 2) y 3) 5)

;;; Simplification identities
(check "simplify x+0"  (simplify (+ x 0)) x)
(check "simplify x*1"  (simplify (* x 1)) x)
(check "simplify 0*x"  (simplify (* 0 x)) 0)
(check "simplify x^0"  (simplify (expt x 0)) 1)
(check "simplify x^1"  (simplify (expt x 1)) x)


;;; =========================================================================
;;; Symbolic differentiation  ∂
;;; =========================================================================

;;; Basic rules
(check "∂x/∂x = 1"         (simplify (∂ x x))         1)
(check "∂x/∂y = 0"         (simplify (∂ x y))         0)
(check "∂c/∂x = 0"         (simplify (∂ 5 x))         0)
(check "∂(x+y)/∂x = 1"     (simplify (∂ (+ x y) x))  1)
(check "∂(x*y)/∂x = y"     (simplify (∂ (* x y) x))  y)

;;; Power rule: ∂x²/∂x = 2x  (check via substitute)
(define dx2 (∂ (* x x) x))
(check "∂x² is sym-expr"   (sym-expr? dx2)            #t)
(check "∂x² at x=3 = 6"    (substitute dx2 x 3)       6)

;;; Product rule: ∂(x·x·x)/∂x at x=2 = 12
(check "∂x³ at x=2 = 12"
  (substitute (∂ (* x x x) x) x 2) 12)

;;; Chain rule through transcendentals
(check "∂sin(x)/∂x = cos(x)"   (simplify (∂ (sin x) x)) (cos x))
(check "∂cos(x)/∂x = -sin(x)"  (simplify (∂ (cos x) x)) (- (sin x)))
(check "∂exp(x)/∂x = exp(x)"   (simplify (∂ (exp x) x)) (exp x))
(check "∂log(x)/∂x = 1/x"      (simplify (∂ (log x) x)) (/ 1 x))

;;; Quotient rule: ∂(1/x)/∂x = -1/x²  (check numerically)
(check-approx "∂(1/x) at x=2"
  (substitute (∂ (/ 1 x) x) x 2.0) -0.25 1e-12)


;;; =========================================================================
;;; Symbolic integration  ∫
;;; =========================================================================

;;; Antiderivative correctness verified by differentiating back
(define ix   (∫ x x))
(define ix2  (∫ (expt x 2) x))
(define isin (∫ (sin x) x))
(define icos (∫ (cos x) x))
(define iexp (∫ (exp x) x))
(define ilog (∫ (log x) x))

(check "∫x dx / ∂ roundtrip"    (simplify (∂ ix   x)) x)
(check "∫x² dx / ∂ roundtrip"   (simplify (∂ ix2  x)) (expt x 2))
(check "∫sin(x) / ∂ roundtrip"  (simplify (∂ isin x)) (sin x))
(check "∫cos(x) / ∂ roundtrip"  (simplify (∂ icos x)) (cos x))
(check "∫exp(x) / ∂ roundtrip"  (simplify (∂ iexp x)) (exp x))

;;; ∫ln(x) dx = x·ln(x)−x  (∂ doesn't fully simplify x·(1/x)−1, check numerically)
(check-approx "∫ln(x) / ∂ at x=2"
  (substitute (simplify (∂ ilog x)) x 2.0) (log 2.0) 1e-12)

;;; Integration rules
(check "∫c dx = c·x"           (∫ 5 x)          (* 5 x))
(check "∫y dx = y·x"           (∫ y x)          (* y x))  ; y constant wrt x
(check "∫(x+x²) / ∂ check"
  (simplify (∂ (∫ (+ x (expt x 2)) x) x))
  (simplify (+ x (expt x 2))))

;;; Linear substitution: ∫sin(2x) dx = −cos(2x)/2
(check "∫sin(2x) / ∂ roundtrip"
  (simplify (∂ (∫ (sin (* 2 x)) x) x)) (sin (* 2 x)))

;;; ASCII alias
(check "integrate alias" (integrate x x) (∫ x x))

;;; Definite integrals — exact results across numeric types
(check "∫₀¹  x dx = 1/2"          (∫ x x 0 1)   1/2)       ; exact rational
(check "∫₀²  x² dx = 8/3"         (∫ (expt x 2) x 0 2) 8/3)
(check "∫₀³  x dx = 9/2"          (∫ x x 0 3)   9/2)
(check "∫₀^½ x dx = 1/8 exact"    (∫ x x 0 1/2) 1/8)       ; rational bounds
(check "∫₁/₃^²/₃ x² dx = 7/81"   (∫ (expt x 2) x 1/3 2/3) 7/81)
(check "∫₀^2.5 x dx = 3.125"      (∫ x x 0 2.5) 3.125)     ; flonum bounds
(check "∫₀^i x dx = -1/2"         (∫ x x 0 (make-rectangular 0 1))   ; complex bounds
  (make-rectangular -1/2 0))
(check "∫₀^10¹² x dx exact bignum"  ; bignum result
  (∫ x x 0 1000000000000)
  500000000000000000000000)


;;; =========================================================================
;;; Complex symbolic operators  conj  real-part  imag-part
;;; =========================================================================

(define z (sym-var 'z))

;;; These return symbolic expressions on sym-vars (not silently evaluated)
(check "conj z sym-expr"      (sym-expr? (conj z))      #t)
(check "real-part z sym-expr" (sym-expr? (real-part z)) #t)
(check "imag-part z sym-expr" (sym-expr? (imag-part z)) #t)

;;; Conjugate is also available as `conjugate`
(check "conjugate = conj" (conj z) (conjugate z))

;;; Simplification identities
(check "conj(conj z) = z"       (conj (conj z))           z)
(check "conj(real z) = real z"  (conj (real-part z))      (real-part z))
(check "conj(imag z) = imag z"  (conj (imag-part z))      (imag-part z))
(check "imag(real z) = 0"       (imag-part (real-part z)) 0)
(check "imag(imag z) = 0"       (imag-part (imag-part z)) 0)
(check "real(conj z) = real z"  (real-part (conj z))      (real-part z))
(check "imag(conj z) = -imag z" (imag-part (conj z))      (- (imag-part z)))

;;; Numeric evaluation via substitute
(define w34 (make-rectangular 3 4))
(check "real-part at 3+4i" (substitute (real-part z) z w34) 3)
(check "imag-part at 3+4i" (substitute (imag-part z) z w34) 4)
(check "conj at 3+4i"      (substitute (conj z)      z w34) (make-rectangular 3 -4))

;;; Numeric conjugate on concrete values
(check "conjugate 3+4i"     (conjugate (make-rectangular 3 4)) (make-rectangular 3 -4))
(check "conjugate real = self" (conjugate 5)                   5)
(check "conjugate rational"    (conjugate 3/4)                 3/4)

;;; Differentiation with respect to a real variable
(check "∂conj(x)/∂x = 1"    (simplify (∂ (conj x) x))      1)  ; conj(1)=1
(check "∂real(x)/∂x = 1"    (simplify (∂ (real-part x) x)) 1)  ; real(1)=1
(check "∂imag(x)/∂x = 0"    (simplify (∂ (imag-part x) x)) 0)  ; imag(1)=0

;;; ∂conj(x²)/∂x = conj(∂x²/∂x) = conj(2x)  (x real)
(check "∂conj(x²)/∂x is conj form"
  (sym-expr? (∂ (conj (* x x)) x)) #t)

;;; Integration with real variable
(check "∫conj(x)dx = conj(x²/2)" (∫ (conj x) x)      (conj (/ (expt x 2) 2)))
(check "∫real(x)dx = real(x²/2)" (∫ (real-part x) x) (real-part (/ (expt x 2) 2)))
(check "∫imag(x)dx = imag(x²/2)" (∫ (imag-part x) x) (imag-part (/ (expt x 2) 2)))


;;; =========================================================================
;;; Wirtinger calculus  wirtinger-d  wirtinger-dbar
;;;
;;; Treats z and z̄ = conj(z) as independent complex variables.
;;; A function f is holomorphic iff (wirtinger-dbar f z) = 0.
;;; =========================================================================

;;; Fundamental rules for z itself
(check "∂z/∂z = 1"       (wirtinger-d    z z) 1)
(check "∂z/∂z̄ = 0"      (wirtinger-dbar z z) 0)

;;; Fundamental rules for conj(z) = z̄
(check "∂z̄/∂z = 0"      (wirtinger-d    (conj z) z) 0)
(check "∂z̄/∂z̄ = 1"     (wirtinger-dbar (conj z) z) 1)

;;; z² is holomorphic: ∂z²/∂z = 2z,  ∂z²/∂z̄ = 0
(check "∂z²/∂z non-zero" (sym-expr? (wirtinger-d    (* z z) z)) #t)
(check "∂z²/∂z̄ = 0"     (simplify  (wirtinger-dbar (* z z) z))  0)

;;; |z|² = z·z̄ is not holomorphic: ∂|z|²/∂z = z̄,  ∂|z|²/∂z̄ = z
(define zz-bar (* z (conj z)))
(check "∂|z|²/∂z = conj(z)" (simplify (wirtinger-d    zz-bar z)) (conj z))
(check "∂|z|²/∂z̄ = z"      (simplify (wirtinger-dbar zz-bar z)) z)

;;; exp(z) is holomorphic
(check "∂exp(z)/∂z = exp(z)" (simplify (wirtinger-d    (exp z) z)) (exp z))
(check "∂exp(z)/∂z̄ = 0"     (simplify (wirtinger-dbar (exp z) z)) 0)

;;; sin(z) is holomorphic
(check "∂sin(z)/∂z̄ = 0"    (simplify (wirtinger-dbar (sin z) z)) 0)

;;; ∂Re(z)/∂z = 1/2,  ∂Im(z)/∂z = −i/2  (standard Wirtinger results)
(check "∂Re(z)/∂z = 1/2"
  (simplify (wirtinger-d (real-part z) z)) 1/2)
(check "∂Im(z)/∂z = -i/2"
  (simplify (wirtinger-d (imag-part z) z)) (make-rectangular 0 -1/2))

;;; Numeric verification: ∂|z|²/∂z̄ evaluated at z = 2+i should equal z
(check "∂|z|²/∂z̄ at 2+i = 2+i"
  (substitute (wirtinger-dbar zz-bar z) z (make-rectangular 2 1))
  (make-rectangular 2 1))

;;; Product rule propagates correctly
(define z3 (* z z z))
(check "∂z³/∂z̄ = 0 (holomorphic)" (simplify (wirtinger-dbar z3 z)) 0)

;;; conj rule: ∂conj(f)/∂z = conj(∂f/∂z̄)
;;; For f = z²: ∂conj(z²)/∂z = conj(∂z²/∂z̄) = conj(0) = 0
(check "∂conj(z²)/∂z = 0"
  (simplify (wirtinger-d (conj (* z z)) z)) 0)


;;; =========================================================================
;;; Numeric tower: exact arithmetic through the CAS
;;; =========================================================================

;;; expt stays exact for integer/rational bases
(check "expt 1/2 2 exact"   (exact? (expt 1/2 2))  #t)
(check "expt 1/2 2 = 1/4"   (expt 1/2 2)           1/4)
(check "expt 2/3 3 = 8/27"  (expt 2/3 3)           8/27)
(check "expt 2 -3 = 1/8"    (expt 2 -3)            1/8)
(check "expt 1/2 -2 = 4"    (expt 1/2 -2)          4)

;;; Complex expt uses repeated squaring via num_mul
(check "expt 2+i 2 = 3+4i"  (expt (make-rectangular 2 1) 2) (make-rectangular 3 4))
(check "expt 2+i 3 = 2+11i" (expt (make-rectangular 2 1) 3) (make-rectangular 2 11))

;;; Complex division
(check "/ 2+11i by 3"    (/ (make-rectangular 2 11) 3) (make-rectangular 2/3 11/3))
(check "/ 1+i by 1-i = i" (/ (make-rectangular 1 1) (make-rectangular 1 -1))
  (make-rectangular 0 1))

;;; equal? works for complex, symbolic, bignum
(check "equal? complex"   (equal? (make-rectangular 1 2) (make-rectangular 1 2)) #t)
(check "equal? symbolic"  (equal? (sin x) (sin x)) #t)
(check "equal? bignum"
  (equal? (expt 2 64) (* 2 (expt 2 63))) #t)


;;; =========================================================================
;;; Surreal numbers
;;; =========================================================================

;;; (exponent . coefficient): (0 . n) = n·ω⁰ = n, (1 . 1) = ω, (-1 . 1) = ε
(define sur3   (make-surreal (list (cons 0 3))))
(define sur-w  (make-surreal (list (cons 1 1))))    ; ω  (infinite)
(define sur-e  (make-surreal (list (cons -1 1))))   ; ε  (infinitesimal)

(check "surreal? yes"  (surreal? sur3)  #t)
(check "surreal? no"   (surreal? 42)    #f)
(check "surreal-nterms" (surreal-nterms sur3) 1)

(check "surreal-real-part 3"      (surreal-real-part    sur3) 3)
(check "surreal-real-part ω"      (surreal-real-part    sur-w) 0)
(check "surreal-omega-part ω"     (surreal-omega-part   sur-w) 1)
(check "surreal-epsilon-part ε"   (surreal-epsilon-part sur-e) 1)

(check "surreal-infinite? ω"      (surreal-infinite?      sur-w) #t)
(check "surreal-finite? 3"        (surreal-finite?        sur3)  #t)
(check "surreal-infinitesimal? ε" (surreal-infinitesimal? sur-e) #t)

(check "surreal->number 3"  (surreal->number sur3) 3)
(check "surreal + real"
  (surreal->number (+ sur3 (make-surreal (list (cons 0 5))))) 8)
(check "surreal-birthday ≥ 0" (>= (surreal-birthday sur3) 0) #t)


;;; =========================================================================
;;; Auto-differentiation  (dual-number / surreal forward mode)
;;;
;;; (auto-diff f x) evaluates f(x+ε) and returns the ε coefficient = f'(x).
;;; Works for algebraic lambdas; C primitives (sin, cos, exp) are excluded
;;; because they don't propagate surreals through their C implementations.
;;; =========================================================================

(check-approx "auto-diff x²"     (auto-diff (lambda (t) (* t t))             3.0)  6.0  1e-10)
(check-approx "auto-diff x³"     (auto-diff (lambda (t) (* t t t))           2.0) 12.0  1e-10)
(check-approx "auto-diff x²+3x"  (auto-diff (lambda (t) (+ (* t t) (* 3 t))) 2.0)  7.0  1e-10)
(check-approx "auto-diff x-5"    (auto-diff (lambda (t) (- t 5))            10.0)  1.0  1e-10)


;;; =========================================================================
;;; Phase 2 — expand / degree / collect / leading-coeff
;;; =========================================================================

;;; expand: distribute * over +
(check "expand constant"    (expand 5) 5)
(check "expand var"         (expand x) x)
(check "expand (* 2 x)"     (expand (* 2 x)) (* 2 x))

;;; (x+1)*(x+2) = x²+3x+2  — verified via substitute
(define ex12 (expand (* (+ x 1) (+ x 2))))
(check     "expand (x+1)(x+2) is sym-expr" (sym-expr? ex12) #t)
(check-approx "expand (x+1)(x+2) at x=3"  (substitute ex12 x 3.0) 20.0 1e-12)
(check-approx "expand (x+1)(x+2) at x=0"  (substitute ex12 x 0.0)  2.0 1e-12)

;;; (x+1)² = x²+2x+1
(define ex-sq (expand (expt (+ x 1) 2)))
(check-approx "expand (x+1)^2 at x=5"  (substitute ex-sq x 5.0) 36.0 1e-12)
(check-approx "expand (x+1)^2 at x=0"  (substitute ex-sq x 0.0)  1.0 1e-12)

;;; (x+1)³ = x³+3x²+3x+1
(define ex-cube (expand (expt (+ x 1) 3)))
(check-approx "expand (x+1)^3 at x=2"  (substitute ex-cube x 2.0) 27.0 1e-12)
(check-approx "expand (x+1)^3 at x=0"  (substitute ex-cube x 0.0)  1.0 1e-12)

;;; -(x+y) → -x + -y
(define ex-neg (expand (- (+ x y))))
(check-approx "expand -(x+y) at x=1,y=2"
  (substitute (substitute ex-neg x 1.0) y 2.0) -3.0 1e-12)

;;; Binomial with two variables: (x+y)*(x-y) = x²-y²
(define ex-diff2 (expand (* (+ x y) (- x y))))
(check-approx "expand (x+y)(x-y) at x=3,y=1"
  (substitute (substitute ex-diff2 x 3.0) y 1.0) 8.0 1e-12)

;;; degree
(check "degree constant"           (degree 5 x)                   0)
(check "degree var"                (degree x x)                   1)
(check "degree other var"          (degree y x)                   0)
(check "degree x^3"                (degree (expt x 3) x)          3)
(check "degree 2*x^2"              (degree (* 2 (expt x 2)) x)    2)
(check "degree x^2+3x+1"          (degree (+ (expt x 2) (* 3 x) 1) x) 2)
(check "degree x^3+x"             (degree (+ (expt x 3) x) x)    3)
(check "degree (x+1)*(x+2)"       (degree (* (+ x 1) (+ x 2)) x) 2)

;;; leading-coeff
(check "leading-coeff constant 5"       (leading-coeff 5 x)               5)
(check "leading-coeff x"                (leading-coeff x x)                1)
(check "leading-coeff 3*x"              (leading-coeff (* 3 x) x)          3)
(check "leading-coeff x^2+3x+1"        (leading-coeff (+ (expt x 2) (* 3 x) 1) x) 1)
(check "leading-coeff 2*x^3+x"         (leading-coeff (+ (* 2 (expt x 3)) x) x) 2)
(check-approx "leading-coeff (x+1)(x+2)"
  (substitute (leading-coeff (* (+ x 1) (+ x 2)) x) x 0.0) 1.0 1e-12)

;;; collect: group like-degree terms
(define c1 (collect (+ (* 2 x) (* 3 x)) x))
(check-approx "collect 2x+3x = 5x at x=2"  (substitute c1 x 2.0) 10.0 1e-12)

(define c2 (collect (+ (expt x 2) (* 2 x) (expt x 2) x) x))
(check-approx "collect x²+2x+x²+x at x=3 = 27"
  (substitute c2 x 3.0) 27.0 1e-12)

;;; collect of expanded (x+1)² = x²+2x+1 gives a collected polynomial
(define c3 (collect ex-sq x))
(check "degree of collected (x+1)^2" (degree c3 x) 2)
(check "leading-coeff of collected (x+1)^2" (leading-coeff c3 x) 1)

;;; roundtrip: expand then differentiate = differentiate directly
(define poly (expand (expt (+ x 1) 3)))
(check-approx "d/dx (x+1)^3 expanded, at x=0"
  (substitute (simplify (∂ poly x)) x 0.0) 3.0 1e-12)
(check-approx "d/dx (x+1)^3 direct, at x=0"
  (substitute (simplify (∂ (expt (+ x 1) 3) x)) x 0.0) 3.0 1e-12)


;;; =========================================================================
;;; Taylor series: (series f x point n)
;;; =========================================================================

;;; exp around 0 to order 4: 1 + x + x²/2 + x³/6 + x⁴/24
(define exp-s4 (series (exp x) x 0 4))
(check "exp series is sym-expr"   (sym-expr? exp-s4) #t)
(check-approx "exp series at x=0"   (substitute exp-s4 x 0.0) 1.0  1e-12)
(check-approx "exp series at x=0.1" (substitute exp-s4 x 0.1) (exp 0.1) 1e-6)
(check-approx "exp series at x=0.5" (substitute exp-s4 x 0.5) (exp 0.5) 0.01)

;;; sin around 0 to order 5: x - x³/6 + x⁵/120
(define sin-s5 (series (sin x) x 0 5))
(check-approx "sin series at x=0"   (substitute sin-s5 x 0.0) 0.0      1e-12)
(check-approx "sin series at x=0.1" (substitute sin-s5 x 0.1) (sin 0.1) 1e-9)

;;; cos around 0 to order 4: 1 - x²/2 + x⁴/24
(define cos-s4 (series (cos x) x 0 4))
(check-approx "cos series at x=0"   (substitute cos-s4 x 0.0) 1.0      1e-12)
(check-approx "cos series at x=0.1" (substitute cos-s4 x 0.1) (cos 0.1) 1e-7)

;;; log(1+x) around 0 to order 4: x - x²/2 + x³/3 - x⁴/4
(define log1p-s4 (series (log (+ 1 x)) x 0 4))
(check-approx "log(1+x) series at x=0"   (substitute log1p-s4 x 0.0) 0.0       1e-12)
(check-approx "log(1+x) series at x=0.3" (substitute log1p-s4 x 0.3) (log 1.3) 5e-4)

;;; expansion around non-zero point: exp around x=1
(define exp-at1 (series (exp x) x 1 3))
(check-approx "exp series around 1, at x=1"
  (substitute exp-at1 x 1.0) (exp 1.0) 1e-12)
(check-approx "exp series around 1, at x=1.1"
  (substitute exp-at1 x 1.1) (exp 1.1) 0.001)

;;; constant function — all derivatives are zero, series = constant
(define c5 (series 5 x 0 3))
(check-approx "series of constant 5 at x=0" (substitute c5 x 0.0) 5.0 1e-12)
(check-approx "series of constant 5 at x=7" (substitute c5 x 7.0) 5.0 1e-12)

;;; polynomial — series of x² around 0 recovers x² exactly
(define x2-s3 (series (expt x 2) x 0 3))
(check-approx "series x^2 at x=3"  (substitute x2-s3 x  3.0) 9.0 1e-12)
(check-approx "series x^2 at x=-2" (substitute x2-s3 x -2.0) 4.0 1e-12)

;;; order 0 — just f(point)
(define s0 (series (sin x) x 0 0))
(check-approx "series sin order 0 at x=0" (substitute s0 x 0.0) 0.0 1e-12)
(define s0c (series (cos x) x 0 0))
(check-approx "series cos order 0 at x=0" (substitute s0c x 0.0) 1.0 1e-12)

;;; =========================================================================
;;; Summary
;;; =========================================================================

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
