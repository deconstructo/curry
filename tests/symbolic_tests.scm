;;; Symbolic CAS tests — covers sx_simplify, sx_diff, sx_substitute,
;;; and numeric predicates num_is_one / num_is_zero used by the simplifier.

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

;;; ---- Predicates and constructors ---- ;;;

(define x (sym-var 'x))
(define y (sym-var 'y))
(define z (sym-var 'z))

(check "sym-var? yes"     (sym-var? x)  #t)
(check "sym-var? no"      (sym-var? 42) #f)
(check "symbolic? var"    (symbolic? x) #t)
(check "symbolic? num"    (symbolic? 3) #f)
(check "sym-var-name"     (sym-var-name x) 'x)
(check "sym-expr? yes"    (sym-expr? (+ x 1)) #t)
(check "sym-expr? no"     (sym-expr? x)       #f)

;;; Arithmetic with symbolic arguments produces symbolic expressions
(check "sym + num is sym" (symbolic? (+ x 1)) #t)
(check "num + sym is sym" (symbolic? (+ 1 x)) #t)
(check "sym * sym is sym" (symbolic? (* x y)) #t)

;;; ---- Simplification: constant folding ---- ;;;

;;; Pure numeric expressions simplify to numbers
(check "simplify 2+3"   (simplify (+ 2 3))   5)
(check "simplify 6*7"   (simplify (* 6 7))   42)
(check "simplify 10-4"  (simplify (- 10 4))  6)
(check "simplify 8/4"   (simplify (/ 8 4))   2)
(check "simplify 2^10"  (simplify (expt 2 10)) 1024)

;;; ---- Simplification: identity rules ---- ;;;

;;; x + 0 → x
(check "x+0 simplifies"  (equal? (simplify (+ x 0))  x) #t)
(check "0+x simplifies"  (equal? (simplify (+ 0 x))  x) #t)
;;; x - 0 → x
(check "x-0 simplifies"  (equal? (simplify (- x 0))  x) #t)
;;; x * 1 → x
(check "x*1 simplifies"  (equal? (simplify (* x 1))  x) #t)
(check "1*x simplifies"  (equal? (simplify (* 1 x))  x) #t)
;;; x / 1 → x
(check "x/1 simplifies"  (equal? (simplify (/ x 1))  x) #t)
;;; x * 0 → 0
(check "x*0 → 0"         (simplify (* x 0))  0)
(check "0*x → 0"         (simplify (* 0 x))  0)
;;; x ^ 0 → 1
(check "x^0 → 1"         (simplify (expt x 0)) 1)
;;; x ^ 1 → x
(check "x^1 → x"         (equal? (simplify (expt x 1)) x) #t)

;;; ---- Simplification: rational and flonum ones ---- ;;;
;;; num_is_one must recognise 1/1 (rational), 1.0 (flonum), etc.
(check "x * 1/1 → x"    (equal? (simplify (* x 1/1))   x) #t)
(check "x * 1.0 → x"    (equal? (simplify (* x 1.0))   x) #t)
(check "x + 0/1 → x"    (equal? (simplify (+ x 0/1))   x) #t)

;;; ---- Simplification: coefficient folding ---- ;;;

;;; Numeric coefficients in MUL are accumulated
(check "2*3*x coeff" (let ((e (simplify (* 2 3 x))))
                       (symbolic? e)) #t)

;;; coefficient -1 becomes neg (real case)
(define neg-x (* -1 x))
(check "-1*x → neg form" (let ((s (simplify neg-x)))
                            (and (symbolic? s)
                                 (not (equal? s neg-x)))) #t)

;;; Numerics in ADD are accumulated at the front
(define e-add (simplify (+ x 3 2)))
(check "x+3+2 numeric folded" (symbolic? e-add) #t)
(check "x+3+2 evals to 8 at x=3"
       (substitute e-add x 3) 8)

;;; ---- Simplification: nested flattening ---- ;;;

;;; (+ (+ x 1) (+ y 2))  flattens to a single ADD
(define nested-add (+ (+ x 1) (+ y 2)))
(define flat-add   (simplify nested-add))
(check "nested add flattened — substitutable"
       (substitute (substitute flat-add x 10) y 20) 33)

;;; ---- Differentiation: basics ---- ;;;

;;; d/dx x = 1
(check "d/dx x"    (simplify (∂ x x))    1)
;;; d/dx y = 0 (independent)
(check "d/dx y=0"  (simplify (∂ y x))    0)
;;; d/dx 42 = 0
(check "d/dx 42=0" (simplify (∂ 42 x))   0)

;;; ---- Differentiation: polynomial rules ---- ;;;

;;; d/dx (x + 3) = 1
(check "d/dx (x+3)" (simplify (∂ (+ x 3) x)) 1)

;;; d/dx (2x) = 2
(define d-2x (simplify (∂ (* 2 x) x)))
(check "d/dx 2x = 2" d-2x 2)

;;; d/dx (x²) = 2x; verify by substituting x=5 → 10
(define d-x2 (simplify (∂ (* x x) x)))
(check "d/dx x² at x=5 = 10"
       (substitute d-x2 x 5) 10)

;;; d/dx (x³) = 3x²; verify at x=2 → 12
(define d-x3 (simplify (∂ (* x x x) x)))
(check "d/dx x³ at x=2 = 12"
       (substitute d-x3 x 2) 12)

;;; d/dx (x^5) = 5x^4; verify at x=2 → 80
(define d-x5 (simplify (∂ (expt x 5) x)))
(check "d/dx x^5 at x=2 = 80"
       (substitute d-x5 x 2) 80)

;;; ---- Differentiation: product rule ---- ;;;

;;; d/dx (x * y) = y  (x is the only dependency)
(define d-xy (simplify (∂ (* x y) x)))
(check "d/dx (x*y) = y" (equal? d-xy y) #t)

;;; d/dx (x * (x+1)) at x=3: f=x, g=x+1, f'g + fg' = (x+1) + x = 2x+1 → 7
(define d-prod (simplify (∂ (* x (+ x 1)) x)))
(check "d/dx x(x+1) at x=3 = 7"
       (substitute d-prod x 3) 7)

;;; ---- Differentiation: quotient rule ---- ;;;

;;; d/dx (1/x) = -1/x² ; at x=2 → -1/4
(define d-recip (simplify (∂ (/ 1 x) x)))
(check-approx "d/dx 1/x at x=2 = -0.25"
              (exact->inexact (substitute d-recip x 2)) -0.25 1e-12)

;;; d/dx (x / (x+1)) = 1/(x+1)²; at x=1 → 1/4
(define d-quot (simplify (∂ (/ x (+ x 1)) x)))
(check-approx "d/dx x/(x+1) at x=1 = 0.25"
              (exact->inexact (substitute d-quot x 1)) 0.25 1e-12)

;;; ---- Differentiation: chain rule — transcendentals ---- ;;;

;;; d/dx sin(x) = cos(x)
(define d-sin (simplify (∂ (sin x) x)))
(check-approx "d/dx sin(x) at x=0 = 1.0"
              (substitute d-sin x 0.0) 1.0 1e-12)
(check-approx "d/dx sin(x) at x=π/2 = 0.0"
              (substitute d-sin x (/ (acos -1) 2)) 0.0 1e-12)

;;; d/dx cos(x) = -sin(x)
(define d-cos (simplify (∂ (cos x) x)))
(check-approx "d/dx cos(x) at x=0 = 0.0"
              (substitute d-cos x 0.0) 0.0 1e-12)
(check-approx "d/dx cos(x) at x=π/2 ≈ -1.0"
              (substitute d-cos x (/ (acos -1) 2)) -1.0 1e-12)

;;; d/dx exp(x) = exp(x); at x=1 → e
(define d-exp (simplify (∂ (exp x) x)))
(check-approx "d/dx exp(x) at x=1 = e"
              (substitute d-exp x 1.0) (exp 1.0) 1e-12)

;;; d/dx log(x) = 1/x; at x=2 → 0.5
(define d-log (simplify (∂ (log x) x)))
(check-approx "d/dx log(x) at x=2 = 0.5"
              (substitute d-log x 2.0) 0.5 1e-12)

;;; d/dx √x = 1/(2√x); at x=4 → 0.25
(define d-sqrt (simplify (∂ (sqrt x) x)))
(check-approx "d/dx sqrt(x) at x=4 = 0.25"
              (substitute d-sqrt x 4.0) 0.25 1e-12)

;;; d/dx tan(x) = 1/cos²(x); at x=0 → 1
(define d-tan (simplify (∂ (tan x) x)))
(check-approx "d/dx tan(x) at x=0 = 1.0"
              (substitute d-tan x 0.0) 1.0 1e-12)

;;; ---- Differentiation: chain rule — composition ---- ;;;

;;; d/dx sin(x²) = 2x·cos(x²); at x=1 → 2·cos(1)
(define d-sin-x2 (simplify (∂ (sin (* x x)) x)))
(check-approx "d/dx sin(x²) at x=1 = 2cos(1)"
              (substitute d-sin-x2 x 1.0) (* 2.0 (cos 1.0)) 1e-12)

;;; d/dx exp(3x) = 3·exp(3x); at x=0 → 3
(define d-exp-3x (simplify (∂ (exp (* 3 x)) x)))
(check-approx "d/dx exp(3x) at x=0 = 3"
              (substitute d-exp-3x x 0.0) 3.0 1e-10)

;;; ---- Differentiation: multivariate — partial derivatives ---- ;;;

;;; ∂/∂x (x²y + y³) = 2xy; at x=3,y=4 → 24
(define f-xy (+ (* x x y) (* y y y)))
(define df-dx (simplify (∂ f-xy x)))
(check "∂/∂x (x²y+y³) at x=3,y=4 = 24"
       (substitute (substitute df-dx x 3) y 4) 24)

;;; ∂/∂y (x²y + y³) = x² + 3y²; at x=2,y=1 → 4+3 = 7
(define df-dy (simplify (∂ f-xy y)))
(check "∂/∂y (x²y+y³) at x=2,y=1 = 7"
       (substitute (substitute df-dy x 2) y 1) 7)

;;; ---- Substitution ---- ;;;

(check "substitute x→5 in x"        (substitute x x 5)          5)
(check "substitute x→5 in y"        (substitute y x 5)          y)
(check "substitute x→3 in x²"       (substitute (* x x) x 3)    9)
(check "substitute x→2 in x²+1"     (substitute (+ (* x x) 1) x 2) 5)
(check "substitute nested"
       (substitute (+ (* x x) (* 3 x) 2) x 4) 30)

;;; ---- Auto-differentiation (dual numbers / surreals) ---- ;;;

;;; auto-diff propagates through algebraic operations only (not transcendentals,
;;; which convert to double and lose the dual epsilon part).
(check-approx "auto-diff x²"       (auto-diff (lambda (t) (* t t))        3.0)  6.0  1e-10)
(check-approx "auto-diff x³"       (auto-diff (lambda (t) (* t t t))      2.0) 12.0  1e-10)
(check-approx "auto-diff x²+3x"    (auto-diff (lambda (t) (+ (* t t) (* 3 t))) 2.0) 7.0 1e-10)
(check-approx "auto-diff x-5"      (auto-diff (lambda (t) (- t 5))       10.0)  1.0  1e-10)
(check-approx "auto-diff 1/(x+1)"  (auto-diff (lambda (t) (/ 1 (+ t 1))) 1.0) -0.25 1e-10)

;;; ---- Phase 4: IBP integration (polynomial × trig/exp/log) ---- ;;;

;;; Verify antiderivatives numerically: F'(t) = f(t) at sample point t
(define (check-antideriv label f-sym F-sym t eps)
  (let ((dF (simplify (∂ F-sym x))))
    (check-approx label
                  (substitute dF x t)
                  (substitute f-sym x t)
                  eps)))

;;; ∫x·sin(x) dx = -x·cos(x) + sin(x)
(define ibp-xsinx (∫ (* x (sin x)) x))
(check-antideriv "∫x·sin(x) antideriv at x=1" (* x (sin x)) ibp-xsinx 1.0 1e-9)
(check-antideriv "∫x·sin(x) antideriv at x=2" (* x (sin x)) ibp-xsinx 2.0 1e-9)

;;; ∫x·cos(x) dx = x·sin(x) + cos(x)
(define ibp-xcosx (∫ (* x (cos x)) x))
(check-antideriv "∫x·cos(x) antideriv at x=1" (* x (cos x)) ibp-xcosx 1.0 1e-9)

;;; ∫x·exp(x) dx = x·exp(x) - exp(x)
(define ibp-xexpx (∫ (* x (exp x)) x))
(check-antideriv "∫x·exp(x) antideriv at x=1" (* x (exp x)) ibp-xexpx 1.0 1e-9)
(check-antideriv "∫x·exp(x) antideriv at x=2" (* x (exp x)) ibp-xexpx 2.0 1e-9)

;;; ∫x·ln(x) dx = x²/2·ln(x) - x²/4  (verified at x=1,2)
(define ibp-xlnx (∫ (* x (log x)) x))
(check-antideriv "∫x·ln(x) antideriv at x=1" (* x (log x)) ibp-xlnx 1.0 1e-9)
(check-antideriv "∫x·ln(x) antideriv at x=2" (* x (log x)) ibp-xlnx 2.0 1e-9)

;;; ∫x²·ln(x) dx = x³/3·ln(x) - x³/9
(define ibp-x2lnx (∫ (* (expt x 2) (log x)) x))
(check-antideriv "∫x²·ln(x) antideriv at x=2" (* (expt x 2) (log x)) ibp-x2lnx 2.0 1e-9)

;;; ---- Phase 4: trig power reductions ---- ;;;

;;; ∫sin²(x) dx = x/2 - sin(2x)/4
(define int-sin2 (∫ (expt (sin x) 2) x))
(check-antideriv "∫sin²(x) antideriv at x=1" (expt (sin x) 2) int-sin2 1.0 1e-9)
(check-antideriv "∫sin²(x) antideriv at x=π/2" (expt (sin x) 2) int-sin2 1.5707963 1e-9)

;;; ∫cos²(x) dx = x/2 + sin(2x)/4
(define int-cos2 (∫ (expt (cos x) 2) x))
(check-antideriv "∫cos²(x) antideriv at x=1" (expt (cos x) 2) int-cos2 1.0 1e-9)

;;; ∫sin²(2x) dx = x/2 - sin(4x)/8
(define int-sin2-2x (∫ (expt (sin (* 2 x)) 2) x))
(check-antideriv "∫sin²(2x) antideriv at x=1" (expt (sin (* 2 x)) 2) int-sin2-2x 1.0 1e-9)

;;; ---- Phase 4: quadratic denominator ---- ;;;

;;; ∫1/(x²+1) dx = atan(x)
(define int-1-x2p1 (∫ (/ 1 (+ (expt x 2) 1)) x))
(check-antideriv "∫1/(x²+1) antideriv at x=1" (/ 1 (+ (expt x 2) 1)) int-1-x2p1 1.0 1e-9)
(check-approx "∫1/(x²+1) = atan(x) at x=1"
              (substitute int-1-x2p1 x 1.0) (atan 1.0) 1e-9)

;;; ∫1/(x²+4) dx = (1/2)·atan(x/2)
(define int-1-x2p4 (∫ (/ 1 (+ (expt x 2) 4)) x))
(check-antideriv "∫1/(x²+4) antideriv at x=1" (/ 1 (+ (expt x 2) 4)) int-1-x2p4 1.0 1e-9)

;;; ∫1/(x²+2x+2) dx = atan(x+1) via completing the square (x²+2x+2 = (x+1)²+1)
(define int-quad-bc (∫ (/ 1 (+ (expt x 2) (* 2 x) 2)) x))
(check-antideriv "∫1/(x²+2x+2) antideriv at x=0"
                 (/ 1 (+ (expt x 2) (* 2 x) 2)) int-quad-bc 0.0 1e-9)
(check-antideriv "∫1/(x²+2x+2) antideriv at x=1"
                 (/ 1 (+ (expt x 2) (* 2 x) 2)) int-quad-bc 1.0 1e-9)

;;; ---- Phase 4: limits ---- ;;;

;;; Classic L'Hôpital limits
(check-approx "lim x→0 sin(x)/x = 1"
              (limit (/ (sin x) x) x 0) 1.0 1e-12)
(check-approx "lim x→0 (eˣ−1)/x = 1"
              (limit (/ (- (exp x) 1) x) x 0) 1.0 1e-12)
(check-approx "lim x→0 (1−cos(x))/x² = 1/2"
              (limit (/ (- 1 (cos x)) (expt x 2)) x 0) 0.5 1e-12)
(check-approx "lim x→1 (x²−1)/(x−1) = 2"
              (limit (/ (- (expt x 2) 1) (- x 1)) x 1) 2.0 1e-12)

;;; Infinity limits
(check "lim x→∞ 1/x = 0"
       (limit (/ 1 x) x +inf.0) 0)

;;; Direct substitution (non-indeterminate)
(check-approx "lim x→2 (x+3) = 5"
              (limit (+ x 3) x 2) 5.0 1e-12)
(check-approx "lim x→0 sin(x) = 0"
              (limit (sin x) x 0) 0.0 1e-12)

;;; Three-level L'Hôpital: (x−sin(x))/x³ = 1/6
(check-approx "lim x→0 (x−sin(x))/x³ = 1/6"
              (limit (/ (- x (sin x)) (expt x 3)) x 0) (/ 1.0 6.0) 1e-9)

;;; ---- Vector calculus ---- ;;;

(define xv (sym-var 'x))
(define yv (sym-var 'y))
(define zv (sym-var 'z))
(define vrs (list xv yv zv))

;;; grad(x²+y²+z²) = (2x, 2y, 2z)
(let ((g (grad (+ (expt xv 2) (expt yv 2) (expt zv 2)) vrs)))
  (check-approx "grad(r²) x-component at (1,2,3)"
    (substitute (substitute (substitute (car g) xv 1) yv 2) zv 3) 2.0 1e-12)
  (check-approx "grad(r²) y-component at (1,2,3)"
    (substitute (substitute (substitute (cadr g) xv 1) yv 2) zv 3) 4.0 1e-12)
  (check-approx "grad(r²) z-component at (1,2,3)"
    (substitute (substitute (substitute (caddr g) xv 1) yv 2) zv 3) 6.0 1e-12))

;;; div(x²,y²,z²) = 2x+2y+2z; at (1,2,3) = 12
(check-approx "div(x²,y²,z²) at (1,2,3) = 12"
  (let ((d (divergence (list (expt xv 2) (expt yv 2) (expt zv 2)) vrs)))
    (substitute (substitute (substitute d xv 1) yv 2) zv 3))
  12.0 1e-12)

;;; curl of conservative field = 0
(let ((c (curl (list (* yv zv) (* xv zv) (* xv yv)) vrs)))
  (check "curl(yz,xz,xy) = (0,0,0)"
    (map (lambda (e) (substitute (substitute (substitute e xv 1) yv 2) zv 3)) c)
    '(0 0 0)))

;;; curl(-y, x, 0) = (0,0,2)
(let ((c (curl (list (- yv) xv 0) vrs)))
  (check "curl(-y,x,0) = (0,0,2)"
    (map (lambda (e) (substitute (substitute (substitute e xv 0) yv 0) zv 0)) c)
    '(0 0 2)))

;;; laplacian(x²+y²+z²) = 6
(check "laplacian(r²) = 6"
  (laplacian (+ (expt xv 2) (expt yv 2) (expt zv 2)) vrs) 6)

;;; Identity: div(curl(F)) = 0 for any F
(let* ((F (list (expt xv 2) (* xv yv) (* xv zv)))
       (curlF (curl F vrs))
       (d (divergence curlF vrs)))
  (check "div(curl(F)) = 0 (vector identity)" d 0))

;;; Identity: curl(grad(f)) = (0,0,0) for any scalar f — result is symbolic 0 each
(let* ((f (+ (expt xv 2) (* xv yv) (expt zv 2)))
       (gf (grad f vrs))
       (c (curl gf vrs)))
  (check "curl(grad(f)) = (0,0,0) (vector identity)" c '(0 0 0)))

;;; dot-product: (x,y,z)·(x,y,z) = x²+y²+z²; at (1,1,1) = 3
(let ((dp (dot-product (list xv yv zv) (list xv yv zv))))
  (check-approx "dot-product (x,y,z)·(x,y,z) at (1,1,1) = 3"
    (substitute (substitute (substitute dp xv 1.0) yv 1.0) zv 1.0) 3.0 1e-12))

;;; cross-product: (1,0,0)×(0,1,0) = (0,0,1)
(check "cross-product (1,0,0)×(0,1,0)" (cross-product '(1 0 0) '(0 1 0)) '(0 0 1))

;;; Maxwell plane wave verification (c=1 units, vacuum)
;;; E = (0, sin(x-t), 0),  B = (0, 0, sin(x-t))
(let* ((tv (sym-var 't))
       (arg (- xv tv))
       (E (list 0 (sin arg) 0))
       (B (list 0 0 (sin arg)))
       (xyz (list xv yv zv))
       (divE (divergence E xyz))
       (divB (divergence B xyz))
       (curlE (curl E xyz))
       (dBdt  (map (lambda (b) (simplify (∂ b tv))) B))
       (curlB (curl B xyz))
       (dEdt  (map (lambda (e) (simplify (∂ e tv))) E))
       (faraday (map (lambda (a b) (simplify (+ a b))) curlE dBdt))
       (ampere  (map (lambda (a b) (simplify (- a b))) curlB dEdt)))
  (check "Maxwell: Gauss div(E)=0"  divE 0)
  (check "Maxwell: monopoles div(B)=0" divB 0)
  (check "Maxwell: Faraday curl(E)+∂B/∂t=0"  faraday '(0 0 0))
  (check "Maxwell: Ampere curl(B)-∂E/∂t=0"   ampere  '(0 0 0)))

;;; ---- Summary ---- ;;;

(newline)
(display "symbolic tests: ")
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (error "some symbolic tests failed"))
