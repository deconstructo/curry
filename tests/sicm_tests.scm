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
;;; § 10  literal-function* — multi-argument symbolic functions
;;; ════════════════════════════════════════════════════════════

(define x (sym-var 'x))
(define y (sym-var 'y))
(define f2 (literal-function* 'f 2))

;;; Applied to two sym-vars it produces a symbolic expression
(check-infix "literal-function* 2-arg" (f2 x y) "f(x, y)")

;;; D differentiates a single-var literal-function*;
;;; subscript comes from the internal argument variable name (_arg0)
(define h (literal-function* 'h 1))
(check-infix "literal-function* 1-arg at t" (h t) "h(t)")
(check-infix "D of literal-function* 1-arg" ((D h) t) "h__arg0(t)")

;;; ════════════════════════════════════════════════════════════
;;; § 11  square with down tuples
;;; ════════════════════════════════════════════════════════════

(check "square down-2" (square (down 3 4)) 25)
(check "square down-3" (square (down 1 2 2)) 9)

;;; ════════════════════════════════════════════════════════════
;;; § 12  Lagrangian->V and Lagrangian->T
;;;        For L-harmonic: V = ½kq², T = ½m·qdot²
;;; ════════════════════════════════════════════════════════════

(define local-ho ((Gamma q) t))

;;; V at the local tuple — should equal ½kq²
(define V-ho ((Lagrangian->V (L-harmonic m k)) local-ho))
(check-infix "Lagrangian->V HO" (simplify (expand V-ho))
             "1/2 * k * q(t)^2")

;;; T at the local tuple — should equal ½m·qdot²
(define T-ho ((Lagrangian->T (L-harmonic m k)) local-ho))
(check-infix "Lagrangian->T HO" (simplify (expand T-ho))
             "1/2 * m * q_t(t)^2")

;;; V of free particle = 0
(define V-free ((Lagrangian->V (L-free-particle m)) local-ho))
(check-infix "Lagrangian->V free" (simplify V-free) "0")

;;; T of free particle = ½m·qdot²
(define T-free ((Lagrangian->T (L-free-particle m)) local-ho))
(check-infix "Lagrangian->T free" (simplify (expand T-free))
             "1/2 * m * q_t(t)^2")

;;; ════════════════════════════════════════════════════════════
;;; § 13  make-Lagrangian
;;;        Build a custom T−V and verify Lagrange equations
;;; ════════════════════════════════════════════════════════════

;;; Simple pendulum (small angle): T = ½ml²qdot², V = ½mgl²q²  (linearised)
;;; EOM: ml²q'' + mgl²q = 0  →  q'' + g·q = 0
(define l (sym-var 'l))
(define (T-pendulum local)
  (* 1/2 m (square l) (square (velocity local))))
(define (V-pendulum q-coord)
  (* 1/2 m g (square l) (square q-coord)))

(define L-pendulum (make-Lagrangian T-pendulum
                                    (lambda (local)
                                      (V-pendulum (coordinate local)))))
(define eom-pendulum ((Lagrange-equations L-pendulum) q))
(check-infix "pendulum EOM"
             (eom-pendulum t)
             "-(g * l^2 * m * q(t)) - l^2 * m * q_t_t(t)")

;;; ════════════════════════════════════════════════════════════
;;; § 14  Euler-Lagrange-operator — additional Lagrangians
;;; ════════════════════════════════════════════════════════════

;;; Free particle at (t=0, q=1, qdot=0): ∂L/∂q=0, ∂L/∂qdot=0 → residual=0
(define lo-free (up 0.0 1.0 0.0))
(check-num "EL-operator free particle" ((Euler-Lagrange-operator (L-free-particle 2.0)) lo-free) 0.0 1e-12)

;;; Gravity at (t=0, q=0, qdot=0): ∂L/∂q − ∂L/∂qdot = −mg − 0 = −9.8
(check-num "EL-operator gravity" ((Euler-Lagrange-operator (L-uniform-acceleration 1.0 9.8)) (up 0.0 0.0 0.0)) -9.8 1e-12)

;;; ════════════════════════════════════════════════════════════
;;; § 15  Numeric Lagrange-equations
;;; ════════════════════════════════════════════════════════════

;;; Harmonic oscillator with m=1, k=4: EOM = -4q(t) - q''(t)
;;; At a concrete numeric path q(t)=sin(2t): q''=-4sin(2t), EOM ≡ 0
;;; We can't fully evaluate without a concrete path, but we can check
;;; that Lagrange-equations returns a callable for numeric Lagrangians.
(define (L-ho-numeric local)
  (- (* 0.5 (square (velocity local)))
     (* 2.0 (square (coordinate local)))))

(define (q-numeric t) (sin (* 2.0 t)))

;;; The EL residual for q(t)=sin(2t) under L=½qdot²−2q² should be ≈0
;;; because d²/dt²(sin(2t)) = -4sin(2t) and ∂L/∂q = -4q
;;; so EOM: -4q - q'' = -4sin - (-4sin) = 0
(define eom-numeric ((Lagrange-equations L-ho-numeric) q-numeric))
(check-num "numeric Lagrange-equations on exact solution" (eom-numeric 1.0) 0.0 1e-10)
(check-num "numeric Lagrange-equations at t=0.5" (eom-numeric 0.5) 0.0 1e-10)

;;; ════════════════════════════════════════════════════════════
;;; § 16  Multi-DOF: 2D harmonic oscillator Lagrange-equations
;;; ════════════════════════════════════════════════════════════

(define x-fn (literal-function 'x))
(define y-fn (literal-function 'y))
(define q-path-2d (lambda (tau) (up (x-fn tau) (y-fn tau))))

(define eom-2d-ho ((Lagrange-equations (L-harmonic-nd m k)) q-path-2d))
(define result-2d (eom-2d-ho t))

(check "2D HO EOM is down-tuple"  (down? result-2d) #t)
(check "2D HO EOM dimension"      (dimension result-2d) 2)
(check-infix "2D HO EOM x-component" (ref result-2d 0) "-(k * x(t)) - m * x_t_t(t)")
(check-infix "2D HO EOM y-component" (ref result-2d 1) "-(k * y(t)) - m * y_t_t(t)")

;;; 2D free particle: EOM = -m*x'' = 0, -m*y'' = 0
(define eom-2d-free ((Lagrange-equations (L-free-particle-nd m)) q-path-2d))
(define result-2d-free (eom-2d-free t))
(check-infix "2D free EOM x" (ref result-2d-free 0) "-(m * x_t_t(t))")
(check-infix "2D free EOM y" (ref result-2d-free 1) "-(m * y_t_t(t))")

;;; ════════════════════════════════════════════════════════════
;;; § 17  Kepler problem in polar coordinates
;;; ════════════════════════════════════════════════════════════

(define GM (sym-var 'GM))
(define r-fn   (literal-function 'r))
(define theta-fn (literal-function 'θ))
(define q-polar (lambda (tau) (up (r-fn tau) (theta-fn tau))))

(define eom-kepler ((Lagrange-equations (L-Kepler-polar m GM)) q-polar))
(define result-kepler (eom-kepler t))

(check-infix "Kepler radial EOM"
             (simplify (expand (ref result-kepler 0)))
             "m * r(t) * θ_t(t)^2 + -GM/r(t)^2 - m * r_t_t(t)")
(check-infix "Kepler angular EOM"
             (simplify (expand (ref result-kepler 1)))
             "-2 * m * r(t) * r_t(t) * θ_t(t) - m * r(t)^2 * θ_t_t(t)")

;;; ════════════════════════════════════════════════════════════
;;; § 18  Hamiltonian mechanics — Legendre transform
;;; ════════════════════════════════════════════════════════════

;;; sym-vars for Hamiltonian state
(define q-sym (sym-var 'q))
(define p-sym (sym-var 'p))

;;; 1-DOF harmonic oscillator: H = p²/(2m) + ½kq²
(define H-ho-fn (Lagrangian->Hamiltonian (L-harmonic m k)))
(check-infix "1D HO Hamiltonian"
             (H-ho-fn (up t q-sym p-sym))
             "p^2/(2 * m) + 1/2 * k * q^2")

;;; 1-DOF free particle: H = p²/(2m)
(define H-free-fn (Lagrangian->Hamiltonian (L-free-particle m)))
(check-infix "1D free Hamiltonian"
             (H-free-fn (up t q-sym p-sym))
             "p^2/(2 * m)")

;;; ════════════════════════════════════════════════════════════
;;; § 19  Hamilton equations
;;; ════════════════════════════════════════════════════════════

(define H-state-1d (up t q-sym p-sym))
(define dH-1d ((Hamilton-equations H-ho-fn) H-state-1d))

(check-infix "1D HO dq/dt" (simplify (ref dH-1d 0)) "p/m")
(check-infix "1D HO dp/dt" (simplify (ref dH-1d 1)) "-(k * q)")

;;; 2-DOF case
(define qx (sym-var 'qx))
(define qy (sym-var 'qy))
(define px (sym-var 'px))
(define py (sym-var 'py))
(define H-2d-fn (Lagrangian->Hamiltonian (L-harmonic-nd m k)))
(define H-state-2d (up t (up qx qy) (down px py)))
(define dH-2d ((Hamilton-equations H-2d-fn) H-state-2d))

(check-infix "2D HO dqx/dt" (simplify (ref (ref dH-2d 0) 0)) "px/m")
(check-infix "2D HO dqy/dt" (simplify (ref (ref dH-2d 0) 1)) "py/m")
(check-infix "2D HO dpx/dt" (simplify (ref (ref dH-2d 1) 0)) "-(k * qx)")
(check-infix "2D HO dpy/dt" (simplify (ref (ref dH-2d 1) 1)) "-(k * qy)")

;;; ════════════════════════════════════════════════════════════
;;; § 20  Poisson bracket
;;; ════════════════════════════════════════════════════════════

(define pb-q (lambda (s) (coordinate s)))
(define pb-p (lambda (s) (momentum s)))

;;; {q, H} = dH/dp = p/m  (Hamilton's equation for q)
(check-infix "{q,H}=dq/dt"
             (simplify ((Poisson-bracket pb-q H-ho-fn) H-state-1d))
             "p/m")

;;; {p, H} = -dH/dq = -kq  (Hamilton's equation for p)
(check-infix "{p,H}=dp/dt"
             (simplify ((Poisson-bracket pb-p H-ho-fn) H-state-1d))
             "-(k * q)")

;;; Canonical commutation: {q, p} = 1
(check "{q,p}=1"
       ((Poisson-bracket pb-q pb-p) H-state-1d)
       1)

;;; ════════════════════════════════════════════════════════════
;;; § 21  momentum selector
;;; ════════════════════════════════════════════════════════════

(check "momentum selector" (momentum (up t q-sym p-sym)) p-sym)

;;; ════════════════════════════════════════════════════════════
;;; Summary
;;; ════════════════════════════════════════════════════════════

(newline)
(display pass) (display " passed, ")
(display fail) (display " failed")
(newline)
(if (> fail 0) (exit 1) (exit 0))
