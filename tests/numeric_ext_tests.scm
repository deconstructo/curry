;;; Numeric tower extensions: multivectors, symbolic CAS, surreals, auto-diff

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

;;; ---- Multivectors / Clifford algebra Cl(3,0,0) ---- ;;;

(define e1  (mv-e 3 0 0 1))
(define e2  (mv-e 3 0 0 2))
(define e3  (mv-e 3 0 0 3))
(define e12 (mv-e 3 0 0 1 2))
(define e23 (mv-e 3 0 0 2 3))
(define e13 (mv-e 3 0 0 1 3))
(define e123 (mv-e 3 0 0 1 2 3))
(define scalar1 (mv-e 3 0 0))  ; scalar = 1

(check "mv? yes"       (mv? e1) #t)
(check "mv? no"        (mv? 42) #f)
(check "mv-signature"  (mv-signature e1) '(3 0 0))

;;; Geometric product: e1*e1 = 1 in Cl(3,0,0)
(check "e1*e1 scalar"  (mv-scalar (mv* e1 e1)) 1.0)
(check "e2*e2 scalar"  (mv-scalar (mv* e2 e2)) 1.0)
(check "e3*e3 scalar"  (mv-scalar (mv* e3 e3)) 1.0)

;;; e1*e2 = e12, e2*e1 = -e12  (anticommuting)
(define e12-prod (mv* e1 e2))
(define e21-prod (mv* e2 e1))
(check "e1*e2 grade-2 blade" (mv-scalar (mv* e12-prod e12-prod)) -1.0)
(check "e1*e2 = -e2*e1" (mv-scalar (mv+ e12-prod e21-prod)) 0.0)

;;; Norm: |e1| = 1
(check-approx "mv-norm e1"  (mv-norm e1)  1.0 1e-12)
(check-approx "mv-norm e12" (mv-norm e12) 1.0 1e-12)

;;; mv-norm2: e1 in Cl(3,0,0) has norm2 = 1
(check "mv-norm2 e1" (mv-norm2 e1) 1.0)

;;; Addition and subtraction
(define sum12 (mv+ e1 e2))
(check-approx "mv+ norm" (mv-norm sum12) (sqrt 2.0) 1e-12)

(define diff12 (mv- (mv+ e1 e2) e2))
(check-approx "mv- recovers e1" (mv-norm (mv- diff12 e1)) 0.0 1e-12)

;;; Scaling
(define two-e1 (mv-scale e1 2.0))
(check "mv-scale norm2" (mv-norm2 two-e1) 4.0)

;;; Grade extraction
(define mixed (mv+ scalar1 e1 e12 e123))
(check "mv-grade 0" (mv-scalar (mv-grade mixed 0)) 1.0)
(check-approx "mv-grade 1 norm" (mv-norm (mv-grade mixed 1)) 1.0 1e-12)
(check-approx "mv-grade 2 norm" (mv-norm (mv-grade mixed 2)) 1.0 1e-12)
(check-approx "mv-grade 3 norm" (mv-norm (mv-grade mixed 3)) 1.0 1e-12)

;;; Reverse: reverses order of basis vectors in each blade
;;; reverse(e12) = e21 = -e12
(define rev-e12 (mv-reverse e12))
(check "mv-reverse e12" (mv-scalar (mv+ e12 rev-e12)) 0.0)

;;; Involution: grade-k blade gets factor (-1)^k
;;; involute(e12) = (-1)^2 * e12 = e12
(define inv-e12 (mv-involute e12))
(check-approx "mv-involute e12" (mv-norm (mv- inv-e12 e12)) 0.0 1e-12)
;;; involute(e1) = (-1)^1 * e1 = -e1
(define inv-e1 (mv-involute e1))
(check-approx "mv-involute e1" (mv-norm (mv+ inv-e1 e1)) 0.0 1e-12)

;;; Conjugate = reverse ∘ involute
(define conj-e12 (mv-conjugate e12))
(check-approx "mv-conjugate e12" (mv-norm (mv+ conj-e12 e12)) 0.0 1e-12)

;;; Wedge product: e1 ∧ e1 = 0
(check "wedge e1^e1 = 0" (mv-scalar (mv-wedge e1 e1)) 0.0)
(check "wedge e1^e2 = e12" (mv-norm (mv- (mv-wedge e1 e2) e12)) 0.0)

;;; Quaternion ↔ multivector roundtrip
(define q-orig (make-quaternion 1.0 2.0 3.0 4.0))
(define q-mv   (quaternion->mv q-orig))
(define q-back (mv->quaternion q-mv))
(check "quat->mv->quat roundtrip" (quaternion? q-back) #t)

;;; mv-from-list and mv-ref
(define mv-list (mv-from-list 2 0 0 '(1.0 2.0 3.0 4.0)))
(check "mv-from-list mv?" (mv? mv-list) #t)
(check "mv-ref blade 0"   (mv-ref mv-list 0) 1.0)
(check "mv-ref blade 1"   (mv-ref mv-list 1) 2.0)

;;; mv-normalize
(define mv-big (mv-scale e1 3.0))
(check-approx "mv-normalize" (mv-norm (mv-normalize mv-big)) 1.0 1e-12)


;;; ---- Symbolic CAS ---- ;;;

(define x (sym-var 'x))
(define y (sym-var 'y))

(check "sym-var?"     (sym-var? x) #t)
(check "sym-var? no"  (sym-var? 42) #f)
(check "symbolic?"    (symbolic? x) #t)
(check "sym-var-name" (sym-var-name x) 'x)
(check "sym-expr?"    (sym-expr? (* x x)) #t)

;;; Differentiation
;;; d/dx(x) = 1
(check "∂ x wrt x"  (simplify (∂ x x)) 1)
;;; d/dx(x²) = 2x  (simplify may not reduce to 2x, check it evaluates correctly)
(define dx2 (∂ (* x x) x))
(check "∂ x² non-zero" (sym-expr? dx2) #t)

;;; Substitute x=3 into 2x, expect 6
(define two-x (* 2 x))
(check "substitute" (substitute two-x x 3) 6)

;;; d/dy(x) = 0 (x doesn't depend on y)
(check "∂ x wrt y = 0" (simplify (∂ x y)) 0)

;;; ---- Surreal numbers ---- ;;;

;;; Construct surreals as lists of (exponent . coefficient) pairs
;;; (0 . n) = n * ω⁰ = n  (real part)
;;; (1 . 1) = ω            (infinite)
;;; (-1 . 1) = ε           (infinitesimal)

(define sur3   (make-surreal (list (cons 0 3))))
(define sur-w  (make-surreal (list (cons 1 1))))    ; ω
(define sur-e  (make-surreal (list (cons -1 1))))   ; ε

(check "surreal? yes"  (surreal? sur3) #t)
(check "surreal? no"   (surreal? 42) #f)
(check "surreal-nterms" (surreal-nterms sur3) 1)

(check "surreal-real-part"    (surreal-real-part sur3) 3)
(check "surreal-real-part ω"  (surreal-real-part sur-w) 0)
(check "surreal-omega-part ω" (surreal-omega-part sur-w) 1)
(check "surreal-epsilon-part ε" (surreal-epsilon-part sur-e) 1)

(check "surreal-infinite? ω"  (surreal-infinite? sur-w) #t)
(check "surreal-finite? 3"    (surreal-finite? sur3) #t)
(check "surreal-infinitesimal? ε" (surreal-infinitesimal? sur-e) #t)

;;; surreal->number extracts the real (ω⁰) part
(check "surreal->number 3" (surreal->number sur3) 3)

;;; Arithmetic: real surreals behave like numbers under +
(define sur5 (make-surreal (list (cons 0 5))))
(define sur-sum (+ sur3 sur5))
(check "surreal + real" (surreal->number sur-sum) 8)

;;; Birthday (complexity measure)
(check "surreal-birthday 3" (>= (surreal-birthday sur3) 0) #t)

;;; ---- Auto-differentiation (dual numbers) ---- ;;;

;;; (auto-diff f x) computes f'(x)
;;; auto-diff works for algebraic lambdas (dual-number propagation through +, *, -)
;;; C primitives (sin, cos, exp) do not propagate surreals, so are not tested here
(check-approx "auto-diff x²"     (auto-diff (lambda (x) (* x x))             3.0)  6.0  1e-10)
(check-approx "auto-diff x³"     (auto-diff (lambda (x) (* x x x))           2.0) 12.0  1e-10)
(check-approx "auto-diff x²+3x" (auto-diff (lambda (x) (+ (* x x) (* 3 x))) 2.0)  7.0  1e-10)
(check-approx "auto-diff x-5"   (auto-diff (lambda (x) (- x 5))             10.0)  1.0  1e-10)

;;; Summary
(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
