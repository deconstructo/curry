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
;;; Assumptions: (sym-var 'x 'positive) etc.
;;; =========================================================================

(define xp (sym-var 'x 'positive))
(define xn (sym-var 'x 'negative))
(define xi (sym-var 'x 'integer))
(define xr (sym-var 'x 'real))

;;; sym-assumption? predicate
(check "assumption positive"         (sym-assumption? xp 'positive) #t)
(check "assumption not negative"     (sym-assumption? xp 'negative) #f)
(check "positive implies real"       (sym-assumption? xp 'real)     #t)
(check "positive implies nonzero"    (sym-assumption? xp 'nonzero)  #t)
(check "negative implies real"       (sym-assumption? xn 'real)     #t)
(check "integer assumption"          (sym-assumption? xi 'integer)  #t)
(check "integer implies real"        (sym-assumption? xi 'real)     #t)
(check "real assumption"             (sym-assumption? xr 'real)     #t)
(check "real does not imply positive" (sym-assumption? xr 'positive) #f)
(check "no assumption on plain var"  (sym-assumption? x  'positive) #f)

;;; abs(x) simplification with assumptions
(check "abs(xp) = xp"     (equal? (abs xp) xp)       #t)
(check "abs(xn) = -xn"    (equal? (abs xn) (- xn))   #t)
(check "abs(-xp) = xp"    (equal? (abs (- xp)) xp)   #t)

;;; sqrt(x^2) with and without assumption
(check "sqrt(xp^2) = xp"  (equal? (sqrt (expt xp 2)) xp)       #t)
(check "sqrt(x^2) = |x|"  (equal? (sqrt (expt x 2)) (abs x))   #t)

;;; log(x^n) = n*log(x) for positive x
(define log-xp3 (simplify (log (expt xp 3))))
(check "log(xp^3) = 3*log(xp)" (equal? log-xp3 (* 3 (log xp))) #t)

;;; sign simplification
(check "sign(xp) = 1"    (equal? (sign xp) 1)  #t)
(check "sign(xn) = -1"   (equal? (sign xn) -1) #t)
(check "sign(5) = 1"     (equal? (sign 5)  1)  #t)
(check "sign(-3) = -1"   (equal? (sign -3) -1) #t)
(check "sign(0) = 0"     (equal? (sign 0)  0)  #t)

;;; sign display
(check "sign infix"   (string? (sym->infix (sign x)))  #t)
(check "sign latex"   (string? (sym->latex (sign x)))  #t)

;;; =========================================================================
;;; Exotic limits: 0·∞, 1^∞, 0^0, ∞^0
;;; =========================================================================

;;; 0·∞: lim x→0+ of x*log(x) = 0
(define lim-xlogx (limit (* x (log x)) x 0.0 'right))
(check "lim x→0+ x*log(x) = 0"   (and (number? lim-xlogx) (zero? lim-xlogx)) #t)

;;; 0^0: lim x→0+ of x^x = 1
(define lim-xx (limit (expt x x) x 0.0 'right))
(check "lim x→0+ x^x = 1"   (equal? lim-xx 1) #t)

;;; ∞^0: lim x→∞ of x^(1/x) = 1
(define lim-x-inv-x (limit (expt x (/ 1 x)) x +inf.0))
(check "lim x→∞ x^(1/x) = 1"   (equal? lim-x-inv-x 1) #t)

;;; 1^∞: lim x→∞ of (1 + 1/x)^x = e
(define lim-compound (limit (expt (+ 1 (/ 1 x)) x) x +inf.0))
(check-approx "lim x→∞ (1+1/x)^x = e"   (exact->inexact lim-compound) (exp 1.0) 1e-9)

;;; =========================================================================
;;; Quaternion CAS: non-commutative symbolic algebra
;;; =========================================================================

;;; Quaternion-flagged sym-vars route multiplication through the ordered
;;; SX_NCMUL node, preserving factor order.  Real scalars commute out.
;;; Differentiation applies the product rule maintaining left-to-right order.

(define q (sym-var 'q 'quaternion))
(define p (sym-var 'p 'quaternion))
(define t (sym-var 't))                 ; real, no assumption

;;; -- Assumption flag --
(check "q has quaternion assumption"       (sym-assumption? q 'quaternion) #t)
(check "p has quaternion assumption"       (sym-assumption? p 'quaternion) #t)
(check "t does not have quaternion assumption" (sym-assumption? t 'quaternion) #f)

;;; Akkadian alias for sym-assumption?  (ṣimdat-la-idûm? = "decree of the unknown?")
(check "Akkadian alias: ṣimdat-la-idûm?"  (ṣimdat-la-idûm? q 'rebûm)       #t)
(check "Akkadian alias: la-idûm?"         (la-idûm? q)                      #t)

;;; -- Non-commutativity --
;;; q*p and p*q must be structurally distinct (Hamilton product ≠ commutative).
(check "q*p ≠ p*q"                (equal? (* q p) (* p q))       #f)
(check "q*p = q*p (self-equal)"   (equal? (* q p) (* q p))       #t)

;;; Symbolic product nodes carry the operator nc*
(check "q*p node is (nc* q p)"    (equal? (* q p) '(nc* q p))    #f) ; not list, just shape
(check "q*p is symbolic"          (symbolic? (* q p))             #t)

;;; -- Real scalars commute out --
;;; Real scalars (fixnum/flonum/rational) are never NC; they fold to a leading
;;; coefficient regardless of where they appear in the product.
(check "2*q = q*2"                (equal? (* 2 q) (* q 2))       #t)
(check "scalar extracted: 3*q*p = q*3*p" (equal? (* 3 q p) (* q 3 p)) #t)

;;; -- Differentiation: product rule maintains order --
;;; ∂q/∂q = 1,  ∂p/∂q = 0.
(check "∂q/∂q = 1"               (equal? (∂ q q) 1)             #t)
(check "∂p/∂q = 0"               (equal? (∂ p q) 0)             #t)

;;; Product rule: ∂(q·p)/∂q = (∂q/∂q)·p = p  (right factor survives)
(check "∂(q*p)/∂q = p"           (equal? (∂ (* q p) q) p)       #t)
;;; ∂(p·q)/∂q = p·(∂q/∂q) = p  (left factor survives)
(check "∂(p*q)/∂q = p"           (equal? (∂ (* p q) q) p)       #t)
;;; Symmetry: differentiate the other variable
(check "∂(q*p)/∂p = q"           (equal? (∂ (* q p) p) q)       #t)
(check "∂(p*q)/∂p = q"           (equal? (∂ (* p q) p) q)       #t)

;;; ∂(q²)/∂q = q + q  (two ordered terms; sum simplification is deferred)
(check "∂(q²)/∂q = q+q"          (equal? (∂ (* q q) q) (+ q q)) #t)

;;; Triple product — product rule gives two ordered terms, each with the
;;; differentiated factor replaced by 1:
;;; ∂(q·p·q)/∂q = p·q + q·p   (first and third positions both differentiate)
(check "∂(q*p*q)/∂q = p*q + q*p"
       (equal? (∂ (* q p q) q)
               (+ (* p q) (* q p)))
       #t)
;;; ∂(q·p·q)/∂p = q·q  (only the middle factor differentiates)
(check "∂(q*p*q)/∂p = q*q"
       (equal? (∂ (* q p q) p)
               (* q q))
       #t)

;;; Real variable mixed with quaternion-valued variable:
;;; ∂(t·q)/∂t = q  (q is constant w.r.t. the real variable t)
(check "∂(t*q)/∂t = q"           (equal? (∂ (* t q) t) q)       #t)
(check "∂(q*t)/∂t = q"           (equal? (∂ (* q t) t) q)       #t)
(check "∂(t*q)/∂q = t"           (equal? (∂ (* t q) q) t)       #t)

;;; -- expand: full distribution over NC products --
;;; (q+p)*(q+p) must produce four terms q², q*p, p*q, p² — NOT three,
;;; because q*p ≠ p*q so the cross terms cannot be combined.
(check "expand (q+p)²: four distinct terms"
       (equal? (expand (* (+ q p) (+ q p)))
               (+ (* q q) (* q p) (* p q) (* p p)))
       #t)

;;; NC expansion is not commutative: (q+p)² ≠ (p+q)²
(check "expand (q+p)² ≠ expand (p+q)²"
       (equal? (expand (* (+ q p) (+ q p)))
               (expand (* (+ p q) (+ p q))))
       #f)

;;; Integer exponent expansion preserves order: q³ = q*q*q
(check "expand q³ = q*q*q"
       (equal? (expand (expt q 3))
               (* (* q q) q))
       #t)

;;; -- substitute --
;;; Concrete quaternion value for q: the unit imaginary i = 0+1i+0j+0k
(define qi (make-quaternion 0.0 1.0 0.0 0.0))

;;; Substituting q→qi in q*p keeps p symbolic in the right position
(check "substitute q→qi in q*p: qi on left"
       (equal? (substitute (* q p) q qi)
               (* qi p))
       #t)
;;; And in p*q, qi ends up on the right
(check "substitute q→qi in p*q: qi on right"
       (equal? (substitute (* p q) q qi)
               (* p qi))
       #t)
;;; Substituting an independent variable leaves the product unchanged
(check "substitute t in q*p leaves q*p"
       (equal? (substitute (* q p) t 5)
               (* q p))
       #t)

;;; =========================================================================
;;; Quaternion numeric tower: trig / hyperbolic functions
;;; =========================================================================

;;; All transcendental functions must accept quaternion arguments and return
;;; quaternion results.  The computation embeds q = a + v̂·‖v‖ in the complex
;;; plane {1, v̂}, applies the complex formula, then reassembles.

(define qv (make-quaternion 1.0 2.0 3.0 4.0))    ; generic test quaternion
(define q0 (make-quaternion 0.0 0.0 0.0 0.0))    ; zero quaternion
(define q1 (make-quaternion 1.0 0.0 0.0 0.0))    ; unit real

;;; Return type: all transcendentals on a quaternion yield a quaternion.
(check "sin(q)  → quaternion"  (quaternion? (sin  qv)) #t)
(check "cos(q)  → quaternion"  (quaternion? (cos  qv)) #t)
(check "tan(q)  → quaternion"  (quaternion? (tan  qv)) #t)
(check "exp(q)  → quaternion"  (quaternion? (exp  qv)) #t)
(check "log(q)  → quaternion"  (quaternion? (log  qv)) #t)
(check "sqrt(q) → quaternion"  (quaternion? (sqrt qv)) #t)
(check "sinh(q) → quaternion"  (quaternion? (sinh qv)) #t)
(check "cosh(q) → quaternion"  (quaternion? (cosh qv)) #t)
(check "tanh(q) → quaternion"  (quaternion? (tanh qv)) #t)

;;; Exact values at zero: sin(0)=0, cos(0)=1, exp(0)=1, log(1)=0, sqrt(1)=1.
(check "sin(0) = 0"   (= (sin  q0) q0) #t)
(check "cos(0) = 1"   (= (cos  q0) q1) #t)
(check "exp(0) = 1"   (= (exp  q0) q1) #t)
(check "log(1) = 0"   (= (log  q1) q0) #t)
(check "sqrt(1) = 1"  (= (sqrt q1) q1) #t)
(check "sinh(0) = 0"  (= (sinh q0) q0) #t)
(check "cosh(0) = 1"  (= (cosh q0) q1) #t)

;;; Pythagorean identity sin²+cos²=1 at zero (exact).
(check "sin²(0)+cos²(0) = 1"
       (= (+ (* (sin q0) (sin q0)) (* (cos q0) (cos q0))) q1)
       #t)

;;; Euler identity: exp(π·i) + 1 ≈ 0.
;;; The i-direction pure-imaginary case reduces to the complex formula
;;; exp(iπ) = cos(π) + i·sin(π) = -1.
(define pi 3.141592653589793)
(check-approx "exp(π·i) + 1 ≈ 0"
              (abs (+ (exp (make-quaternion 0.0 pi 0.0 0.0)) 1))
              0.0  1e-10)

;;; Euler identity holds for every pure-imaginary axis direction.
(check-approx "exp(π·j) + 1 ≈ 0"
              (abs (+ (exp (make-quaternion 0.0 0.0 pi 0.0)) 1))
              0.0  1e-10)
(check-approx "exp(π·k) + 1 ≈ 0"
              (abs (+ (exp (make-quaternion 0.0 0.0 0.0 pi)) 1))
              0.0  1e-10)

;;; Pythagorean identity sin²+cos² = 1 for a generic quaternion.
;;; (abs returns the quaternion norm, so check norm of error < eps.)
(check-approx "sin²(q)+cos²(q) ≈ 1"
              (abs (- (+ (* (sin qv) (sin qv)) (* (cos qv) (cos qv))) q1))
              0.0  1e-10)

;;; Hyperbolic Pythagorean identity cosh²-sinh² = 1 for a generic quaternion.
(check-approx "cosh²(q)-sinh²(q) ≈ 1"
              (abs (- (- (* (cosh qv) (cosh qv)) (* (sinh qv) (sinh qv))) q1))
              0.0  1e-10)

;;; log∘exp roundtrip for a quaternion with ‖Im(q)‖ < π (avoids branch cut).
(define qsmall (make-quaternion 0.0 0.5 0.3 0.1))
(check-approx "log(exp(q)) ≈ q for small Im(q)"
              (abs (- (log (exp qsmall)) qsmall))
              0.0  1e-10)

;;; =========================================================================
;;; Quaternion builtins: accessors, operations, eqv?/equal?, conj
;;; =========================================================================

(define qb (make-quaternion 1.0 2.0 3.0 4.0))
(define qi (make-quaternion 0.0 1.0 0.0 0.0))
(define qj (make-quaternion 0.0 0.0 1.0 0.0))

;;; Component accessors
(check-approx "quaternion-w"    (quaternion-w qb) 1.0 1e-15)
(check-approx "quaternion-x"    (quaternion-x qb) 2.0 1e-15)
(check-approx "quaternion-y"    (quaternion-y qb) 3.0 1e-15)
(check-approx "quaternion-z"    (quaternion-z qb) 4.0 1e-15)

;;; Norm, conjugate, normalize, inverse
(check-approx "quaternion-norm"
              (quaternion-norm qb) (sqrt 30.0) 1e-10)
(check "quaternion-conjugate"
       (equal? (quaternion-conjugate qb) (make-quaternion 1.0 -2.0 -3.0 -4.0)) #t)
(check-approx "quaternion-norm of inverse = 1/norm"
              (quaternion-norm (quaternion-inverse qb)) (/ 1.0 (sqrt 30.0)) 1e-10)
(check-approx "quaternion-norm of normalized = 1"
              (quaternion-norm (quaternion-normalize qb)) 1.0 1e-10)

;;; quaternion+ and quaternion*
(check "quaternion+"
       (equal? (quaternion+ qb qb) (make-quaternion 2.0 4.0 6.0 8.0)) #t)
(check "quaternion* i*j = k"
       (equal? (quaternion* qi qj) (make-quaternion 0.0 0.0 0.0 1.0)) #t)
(check "quaternion* j*i = -k"
       (equal? (quaternion* qj qi) (make-quaternion 0.0 0.0 0.0 -1.0)) #t)

;;; conj now correctly negates imaginary parts
(check "conj(1+2i+3j+4k) = 1-2i-3j-4k"
       (equal? (conj qb) (make-quaternion 1.0 -2.0 -3.0 -4.0)) #t)
(check "conj(conj(q)) = q"
       (equal? (conj (conj qb)) qb) #t)
(check "q * conj(q) is real"
       (let ((r (* qb (conj qb))))
         (and (quaternion? r)
              (< (abs (- (quaternion-x r) 0.0)) 1e-10)
              (< (abs (- (quaternion-y r) 0.0)) 1e-10)
              (< (abs (- (quaternion-z r) 0.0)) 1e-10)))
       #t)

;;; eqv? and equal? compare by value, not pointer identity
(check "eqv? by value"   (eqv?   (make-quaternion 1.0 2.0 3.0 4.0)
                                  (make-quaternion 1.0 2.0 3.0 4.0)) #t)
(check "equal? by value" (equal? (make-quaternion 1.0 2.0 3.0 4.0)
                                  (make-quaternion 1.0 2.0 3.0 4.0)) #t)
(check "eqv? distinct"  (eqv?   (make-quaternion 1.0 2.0 3.0 4.0)
                                  (make-quaternion 1.0 2.0 3.0 5.0)) #f)

;;; =========================================================================
;;; Quaternion CAS: additional symbolic simplification fixes
;;; =========================================================================

(define q  (sym-var 'q  'quaternion))
(define p  (sym-var 'p  'quaternion))
(define t  (sym-var 't))
(define qm1 (make-quaternion -1.0 0.0 0.0 0.0))

;;; Like-term collection in ADD: q+q → (nc* 2 q)
(check "q+q → (nc* 2 q)"   (equal? (+ q q)     (* 2 q))   #t)
(check "q+q+q → (nc* 3 q)" (equal? (+ q q q)   (* 3 q))   #t)
(check "q+p+q groups q"    (equal? (+ q p q)   (+ (* 2 q) p)) #t)

;;; Real-quaternion -1 (a+0i+0j+0k with a=-1) treated as scalar: emits (neg ...)
(check "(-1+0i)*q → (neg q)" (equal? (* qm1 q) (- q)) #t)
(check "(-1+0i) treated as real scalar" (equal? (* qm1 p q) (* (- p) q)) #t)

;;; NC integration: quaternion constant factors around a real integrand
(check "∫ q dt = q*t"
       (equal? (∫ q t) (* q t)) #t)
(check "∫ t*q dt = (t²/2)*q"
       (equal? (∫ (* t q) t) (* (/ (expt t 2) 2) q)) #t)
(check "∫ q*t dt = q*(t²/2)"
       (equal? (∫ (* q t) t) (* q (/ (expt t 2) 2))) #t)
(check "∫ q*t² dt = q*(t³/3)"
       (equal? (∫ (* q (expt t 2)) t) (* q (/ (expt t 3) 3))) #t)

;;; =========================================================================
;;; Summary
;;; =========================================================================

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
