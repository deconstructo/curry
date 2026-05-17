;;; trig_tests.scm — trig simplification: Pythagorean identity and double-angle rules

(import (scheme base))
(import (scheme inexact))

(define pass 0)
(define fail 0)

(define (check label got expected)
  (if (equal? got expected)
      (begin (display "PASS: ") (display label) (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: ") (display label)
             (display " — got ") (write got)
             (display "  expected ") (write expected) (newline)
             (set! fail (+ fail 1)))))

(define (check-infix label expr expected-str)
  (let ((got (sym->infix expr)))
    (if (equal? got expected-str)
        (begin (display "PASS: ") (display label) (newline)
               (set! pass (+ pass 1)))
        (begin (display "FAIL: ") (display label)
               (display " — got \"") (display got)
               (display "\" expected \"") (display expected-str)
               (display "\"") (newline)
               (set! fail (+ fail 1))))))

(define x     (sym-var 'x))
(define theta (sym-var 'theta))
(define r     (sym-var 'r))
(define omega (sym-var 'omega))

;;; ════════════════════════════════════════════════════════════
;;; § 1  Pythagorean identity via simplify
;;; ════════════════════════════════════════════════════════════

(check "sin²+cos²=1"
       (simplify (+ (expt (sin x) 2) (expt (cos x) 2)))
       1)

(check "cos²+sin²=1  (reversed)"
       (simplify (+ (expt (cos x) 2) (expt (sin x) 2)))
       1)

(check "3·(sin²+cos²)=3"
       (simplify (* 3 (+ (expt (sin x) 2) (expt (cos x) 2))))
       3)

;;; sin²(θ)+cos²(θ) — different variable
(check "sin²θ+cos²θ=1"
       (simplify (+ (expt (sin theta) 2) (expt (cos theta) 2)))
       1)

;;; Sum with extra terms: x + sin²(x) + cos²(x) = x + 1
(check-infix "x + sin²+cos²"
             (simplify (+ x (expt (sin x) 2) (expt (cos x) 2)))
             "1 + x")

;;; ════════════════════════════════════════════════════════════
;;; § 2  trigsimp — complementary identities
;;; ════════════════════════════════════════════════════════════

;;; 1 − sin²(x) = cos²(x)
(check "1-sin²=cos²"
       (trigsimp (- 1 (expt (sin x) 2)))
       (expt (cos x) 2))

;;; 1 − cos²(x) = sin²(x)
(check "1-cos²=sin²"
       (trigsimp (- 1 (expt (cos x) 2)))
       (expt (sin x) 2))

;;; 2 − sin²(x) = 1 + cos²(x)
(check-infix "2-sin²=1+cos²"
             (trigsimp (- 2 (expt (sin x) 2)))
             "1 + cos(x)^2")

;;; ════════════════════════════════════════════════════════════
;;; § 3  trigsimp — double-angle identities
;;; ════════════════════════════════════════════════════════════

;;; cos²(x) − sin²(x) = cos(2x)
(check "cos²-sin²=cos2x"
       (trigsimp (- (expt (cos x) 2) (expt (sin x) 2)))
       (cos (* 2 x)))

;;; sin²(x) − cos²(x) = −cos(2x)
(check-infix "sin²-cos²=-cos2x"
             (trigsimp (- (expt (sin x) 2) (expt (cos x) 2)))
             "-cos(2 * x)")

;;; 2·sin(x)·cos(x) = sin(2x)
(check "2sincos=sin2x"
       (trigsimp (* 2 (sin x) (cos x)))
       (sin (* 2 x)))

;;; Double angle applied recursively: 2·sin(2x)·cos(2x) = sin(4x)
(check-infix "2sin2xcos2x=sin4x"
             (trigsimp (* 2 (sin (* 2 x)) (cos (* 2 x))))
             "sin(4 * x)")

;;; ════════════════════════════════════════════════════════════
;;; § 4  SICM kinetic energy: T = ½ r² ω² (sin²θ + cos²θ) = ½ r² ω²
;;; ════════════════════════════════════════════════════════════

;;; Planar rigid-body kinetic energy in polar coordinates.
;;; vx = r·ω·sin(θ), vy = r·ω·cos(θ)
;;; T = ½(vx²+vy²) = ½ r² ω² (sin²θ+cos²θ) = ½ r² ω²
(define KE
  (* 1/2
     (expt r 2)
     (expt omega 2)
     (+ (expt (sin theta) 2)
        (expt (cos theta) 2))))

(check-infix "SICM KE simplifies to ½r²ω²"
             (simplify KE)
             "1/2 * r^2 * omega^2")

;;; ════════════════════════════════════════════════════════════
;;; § 5  Interaction with ∂
;;; ════════════════════════════════════════════════════════════

;;; ∂(sin²x + cos²x)/∂x = 0  (constant 1)
(check "∂(sin²+cos²)/∂x=0"
       (simplify (∂ (+ (expt (sin x) 2) (expt (cos x) 2)) x))
       0)

;;; ∂(1-sin²x)/∂x after trigsimp = ∂(cos²x)/∂x = -sin(2x)
(let* ((cs (trigsimp (- 1 (expt (sin x) 2))))
       (d  (trigsimp (simplify (∂ cs x)))))
  (check-infix "∂(cos²x)/∂x = -sin(2x)"
               d
               "-sin(2 * x)"))

;;; ════════════════════════════════════════════════════════════
;;; § 6  Numeric evaluation after trigsimp
;;; ════════════════════════════════════════════════════════════

;;; trigsimp(1 - sin²(x)) = cos²(x); evaluating at x=1 should match
(let* ((sym-result (trigsimp (- 1 (expt (sin x) 2))))
       (at1 (substitute sym-result x 1.0))
       (expected (expt (cos 1.0) 2))
       (err (abs (- (inexact at1) expected))))
  (if (<= err 1e-12)
      (begin (display "PASS: numeric cos²(1.0)") (newline)
             (set! pass (+ pass 1)))
      (begin (display "FAIL: numeric cos²(1.0) — got ") (display at1)
             (display "  expected ") (display expected) (newline)
             (set! fail (+ fail 1)))))

;;; ════════════════════════════════════════════════════════════
;;; Summary
;;; ════════════════════════════════════════════════════════════

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
