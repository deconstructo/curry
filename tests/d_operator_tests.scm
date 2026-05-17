;;; D operator tests — functional derivative
;;;
;;; (D f) returns a function g such that (g x) = f'(x).
;;; Works on concrete functions, closures, sym-fns, and higher-order.

(import (scheme base))
(import (scheme inexact))

(define pass 0)
(define fail 0)
(define pi (acos -1.0))

(define (check label got expected tol)
  (let ((err (abs (- (inexact got) (inexact expected)))))
    (if (<= err tol)
        (begin (display "PASS: ") (display label) (newline)
               (set! pass (+ pass 1)))
        (begin (display "FAIL: ") (display label)
               (display " — got ") (display got)
               (display "  expected ") (display expected)
               (display "  err=") (display err) (newline)
               (set! fail (+ fail 1))))))

(define (check= label got expected)
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
               (display "\" expected \"") (display expected-str) (display "\"") (newline)
               (set! fail (+ fail 1))))))

(define x (sym-var 'x))
(define t (sym-var 't))

;;; ════════════════════════════════════════════════════════════
;;; § 1  D on built-in numeric functions
;;; ════════════════════════════════════════════════════════════

;;; (D sin) → cos
(let ((Dsin (D sin)))
  (check  "D sin at 0"       (Dsin 0.0)       1.0         1e-12)
  (check  "D sin at pi/2"    (Dsin (/ pi 2))  0.0         1e-10)
  (check  "D sin at pi/4"    (Dsin (/ pi 4))  (cos (/ pi 4)) 1e-12))

;;; (D cos) → -sin
(let ((Dcos (D cos)))
  (check  "D cos at 0"       (Dcos 0.0)       0.0    1e-12)
  (check  "D cos at pi/2"    (Dcos (/ pi 2))  -1.0   1e-10)
  (check  "D cos at pi/4"    (Dcos (/ pi 4))  (- (sin (/ pi 4))) 1e-12))

;;; (D exp) → exp
(let ((Dexp (D exp)))
  (check  "D exp at 0"       (Dexp 0.0)   1.0              1e-12)
  (check  "D exp at 1"       (Dexp 1.0)   (exp 1.0)        1e-12)
  (check  "D exp at 2"       (Dexp 2.0)   (exp 2.0)        1e-12))

;;; ════════════════════════════════════════════════════════════
;;; § 2  D on closures
;;; ════════════════════════════════════════════════════════════

;;; (D (lambda (x) (* x x))) → 2x
(let ((Df (D (lambda (v) (* v v)))))
  (check  "D x^2 at 0"   (Df 0.0)  0.0   1e-12)
  (check  "D x^2 at 3"   (Df 3.0)  6.0   1e-12)
  (check  "D x^2 at -2"  (Df -2.0) -4.0  1e-12))

;;; (D (lambda (x) (+ (* x x x) x))) → 3x²+1
(let ((Df (D (lambda (v) (+ (* v v v) v)))))
  (check  "D x^3+x at 2"  (Df 2.0)  13.0  1e-12)
  (check  "D x^3+x at 0"  (Df 0.0)   1.0  1e-12))

;;; (D (lambda (x) (exp (* 2 x)))) → 2·e^{2x}  (chain rule)
(let ((Df (D (lambda (v) (exp (* 2 v))))))
  (check  "D e^{2x} at 0"  (Df 0.0)  2.0           1e-12)
  (check  "D e^{2x} at 1"  (Df 1.0)  (* 2 (exp 2)) 1e-10))

;;; ════════════════════════════════════════════════════════════
;;; § 3  D producing symbolic output (argument is a sym-var)
;;; ════════════════════════════════════════════════════════════

(check-infix "D sin sym"    ((D sin) x)          "cos(x)")
(check-infix "D cos sym"    ((D cos) x)          "-sin(x)")
(check-infix "D exp sym"    ((D exp) x)          "exp(x)")

(check-infix "D x^2 sym"
  ((D (lambda (v) (* v v))) x)   "2 * x")

;;; x*x doesn't collapse further; verify numerically
(let ((Df (D (lambda (v) (* v v v)))))
  (check "D x^3 at 2 numeric"  (Df 2.0)  12.0  1e-12)
  (check "D x^3 at 3 numeric"  (Df 3.0)  27.0  1e-12))

;;; ════════════════════════════════════════════════════════════
;;; § 4  Higher-order D
;;; ════════════════════════════════════════════════════════════

;;; (D (D sin)) → -sin
(let ((D2sin (D (D sin))))
  (check  "D² sin at 0"      (D2sin 0.0)      0.0          1e-12)
  (check  "D² sin at pi/2"   (D2sin (/ pi 2)) -1.0         1e-10)
  (check  "D² sin at pi/4"   (D2sin (/ pi 4)) (- (sin (/ pi 4))) 1e-12))

(check-infix "D² sin sym"  ((D (D sin)) x)  "-sin(x)")
(check-infix "D² cos sym"  ((D (D cos)) x)  "-cos(x)")
(check-infix "D² exp sym"  ((D (D exp)) x)  "exp(x)")

;;; (D (D (D sin))) → -cos
(check-infix "D³ sin sym"  ((D (D (D sin))) x)  "-cos(x)")

;;; ════════════════════════════════════════════════════════════
;;; § 5  D on symbolic functions (T_SYMFN)
;;; ════════════════════════════════════════════════════════════

;;; phi(t) — single-parameter sym-fn
(define phi (sym-fn 'phi t))

;;; (D phi) applied to t → phi_t(t)
(check-infix "D phi sym-fn at t"  ((D phi) t)  "phi_t(t)")

;;; (D phi) applied to a concrete value uses substitution
;;; result should still be symbolic: phi_t(0)
(let ((result ((D phi) 0)))
  (check= "D phi at 0 symbolic?" (symbolic? result) #t))

;;; (D (D phi)) → phi_t_t
(check-infix "D² phi sym-fn"  ((D (D phi)) t)  "phi_t_t(t)")

;;; ════════════════════════════════════════════════════════════
;;; § 6  D interacts with existing ∂
;;; ════════════════════════════════════════════════════════════

;;; (D f) should agree with (∂ (f x) x) at the expression level
(let* ((expr-via-partial  (∂ (sin x) x))       ; cos(x)
       (expr-via-D        ((D sin) x)))          ; also cos(x)
  (check= "D vs ∂: sin"  expr-via-D expr-via-partial))

(let* ((expr-via-partial  (∂ (* x x) x))       ; 2*x
       (expr-via-D        ((D (lambda (v) (* v v))) x)))
  (check= "D vs ∂: x^2"  expr-via-D expr-via-partial))

;;; ════════════════════════════════════════════════════════════
;;; Summary
;;; ════════════════════════════════════════════════════════════

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
