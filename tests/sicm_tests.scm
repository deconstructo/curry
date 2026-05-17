;;; sicm_tests.scm — (curry sicm) module: SICM mechanics interface

(import (scheme base))
(import (scheme inexact))
(import (curry sicm))

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

(define (check-num label got expected tol)
  (let ((err (abs (- (inexact got) (inexact expected)))))
    (if (<= err tol)
        (begin (display "PASS: ") (display label) (newline)
               (set! pass (+ pass 1)))
        (begin (display "FAIL: ") (display label)
               (display " — got ") (display got)
               (display "  expected ") (display expected)
               (display "  err=") (display err) (newline)
               (set! fail (+ fail 1))))))

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

(define t (sym-var 't))
(define m (sym-var 'm))
(define k (sym-var 'k))
(define g (sym-var 'g))

;;; ════════════════════════════════════════════════════════════
;;; § 1  literal-function and D
;;; ════════════════════════════════════════════════════════════

(define q (literal-function 'q))

(check-infix "q(t)" (q t) "q(t)")
(check-infix "Dq(t)" ((D q) t) "q_t(t)")
(check-infix "D2q(t)" ((D (D q)) t) "q_t_t(t)")

;;; ════════════════════════════════════════════════════════════
;;; § 2  Gamma — path functor
;;; ════════════════════════════════════════════════════════════

(define local ((Gamma q) t))

(check "Gamma dimension"   (dimension local) 3)
(check-infix "time slot"   (time local)       "t")
(check-infix "coord slot"  (coordinate local) "q(t)")
(check-infix "veloc slot"  (velocity local)   "q_t(t)")

;;; ════════════════════════════════════════════════════════════
;;; § 3  Lagrange-equations — free particle
;;;       L = ½ m qdot²  →  EOM: m·q'' = 0
;;; ════════════════════════════════════════════════════════════

(define eom-free ((Lagrange-equations (L-free-particle m)) q))

;;; Residual is -(m·q'') = 0
(check-infix "free EOM" (eom-free t) "-(m * q_t_t(t))")

;;; ════════════════════════════════════════════════════════════
;;; § 4  Lagrange-equations — harmonic oscillator
;;;       L = ½mqdot² − ½kq²  →  EOM: mq'' + kq = 0
;;; ════════════════════════════════════════════════════════════

(define eom-ho ((Lagrange-equations (L-harmonic m k)) q))

;;; Residual: -(k·q(t)) - m·q''(t) = 0
(check-infix "harmonic EOM" (eom-ho t) "-(k * q(t)) - m * q_t_t(t)")

;;; ════════════════════════════════════════════════════════════
;;; § 5  Lagrange-equations — uniform gravity
;;;       L = ½mqdot² − mgq  →  EOM: mq'' + mg = 0
;;; ════════════════════════════════════════════════════════════

(define eom-grav ((Lagrange-equations (L-uniform-acceleration m g)) q))

(check-infix "gravity EOM" (eom-grav t) "-(g * m) - m * q_t_t(t)")

;;; ════════════════════════════════════════════════════════════
;;; § 6  Energy (Legendre transform)
;;; ════════════════════════════════════════════════════════════

;;; Harmonic oscillator: E = ½m·qdot² + ½k·q²
(define E-ho ((Lagrangian->energy (L-harmonic m k)) local))
(check-infix "HO energy" (simplify (expand E-ho)) "1/2 * m * q_t(t)^2 + 1/2 * k * q(t)^2")

;;; Free particle: E = ½m·qdot²
(define E-free ((Lagrangian->energy (L-free-particle m)) local))
(check-infix "free energy" (simplify (expand E-free)) "1/2 * m * q_t(t)^2")

;;; ════════════════════════════════════════════════════════════
;;; § 7  compose and square
;;; ════════════════════════════════════════════════════════════

(check "compose f g" ((compose (lambda (x) (* x x)) (lambda (x) (+ x 1))) 3) 16)
(check "compose 3-way" ((compose car cdr cdr) '(1 2 3)) 3)
(check "square scalar" (square 5) 25)
(check "square up" (square (up 3 4)) 25)
(check "square up-3" (square (up 1 2 2)) 9)

;;; ════════════════════════════════════════════════════════════
;;; § 8  Gamma-bar — higher-order local tuple
;;; ════════════════════════════════════════════════════════════

(define local3 ((Gamma-bar q 3) t))
(check "Gamma-bar dim" (dimension local3) 5)     ; t + 3+1 derivs
(check-infix "Gamma-bar accel" (acceleration local3) "q_t_t(t)")

;;; ════════════════════════════════════════════════════════════
;;; § 9  Numerical verification
;;; ════════════════════════════════════════════════════════════

;;; ∂L/∂qdot for free particle with m=2 at qdot=3: expect 6
(define (L-2 local) (* 2 (square (velocity local))))
(check-num "dL/dqdot numeric" (((partial 2) L-2) (up 0.0 0.0 3.0)) 12.0 1e-12)

;;; Harmonic EOM at concrete local — just verify it runs without error
(define lo-num (up 0.0 1.0 0.0))
(define f-local ((Euler-Lagrange-operator (L-harmonic 1 1)) lo-num))
(check-num "EL-operator numeric HO" (inexact f-local) -1.0 1e-12)

;;; ════════════════════════════════════════════════════════════
;;; Summary
;;; ════════════════════════════════════════════════════════════

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
